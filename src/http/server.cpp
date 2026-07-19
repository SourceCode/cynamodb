#include <cynamodb/http/server.hpp>
#include <cynamodb/api/dispatcher.hpp>
#include <cynamodb/api/handlers.hpp>
#include <cynamodb/auth/sigv4.hpp>
#include <cynamodb/json/serializer.hpp>
#include <boost/asio/dispatch.hpp>
#include <iostream>
#include <random>
#include <string>
#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace {

constexpr size_t kHeapTrimResponseThreshold = 1024U * 1024U;

void trim_heap_after_completed_bulk_response(bool should_trim) {
#ifdef __GLIBC__
    if (should_trim) malloc_trim(0);
#else
    (void)should_trim;
#endif
}

} // namespace

namespace cynamodb::http {

HttpServer::HttpServer(Context& ctx, int io_threads)
    // Size the io_context to the actual worker-thread count. io_context(1) selects the
    // single-threaded scheduler (lock-free, one-thread-only); with N>1 threads that
    // serializes every request onto one core and collapses under concurrent load.
    : ctx_(ctx), ioc_(io_threads > 1 ? io_threads : 1), acceptor_(net::make_strand(ioc_)) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::run(const std::string& address, unsigned short port, int threads) {
    auto const addr = net::ip::make_address(address);
    beast::error_code ec;

    acceptor_.open(addr.is_v4() ? tcp::v4() : tcp::v6(), ec);
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    acceptor_.bind({addr, port}, ec);
    acceptor_.listen(net::socket_base::max_listen_connections, ec);

    if (ec) {
        std::cerr << "HttpServer error: " << ec.message() << std::endl;
        return;
    }

    running_ = true;
    do_accept();

    worker_threads_.reserve(threads);
    for (int i = 0; i < threads; ++i) {
        worker_threads_.emplace_back([this] { ioc_.run(); });
    }
}

void HttpServer::stop() {
    if (!running_.exchange(false)) return;
    ioc_.stop();
    for (auto& t : worker_threads_) {
        if (t.joinable()) t.join();
    }
}

void HttpServer::do_accept() {
    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(&HttpServer::on_accept, this));
}

void HttpServer::on_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) {
        std::cerr << "Accept error: " << ec.message() << std::endl;
    } else {
        std::make_shared<HttpSession>(std::move(socket), ctx_)->run();
    }
    if (running_) do_accept();
}

// HttpSession implementation
HttpSession::HttpSession(tcp::socket socket, Context& ctx)
    : stream_(std::move(socket)), ctx_(ctx) {
    (void)ctx_;
}

void HttpSession::run() {
    net::dispatch(stream_.get_executor(),
                  beast::bind_front_handler(&HttpSession::do_read, shared_from_this()));
}

void HttpSession::do_read() {
    req_ = {};
    stream_.expires_after(std::chrono::seconds(30));
    http::async_read(stream_, buffer_, req_,
                     beast::bind_front_handler(&HttpSession::on_read, shared_from_this()));
}

void HttpSession::on_read(beast::error_code ec, std::size_t bytes_transferred) {
    (void)bytes_transferred;
    if (ec == http::error::end_of_stream) {
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
        return;
    }
    if (ec) return;

    handle_request();
}

void HttpSession::handle_request() {
    http::response<http::string_body> res{http::status::ok, req_.version()};
    res.set(http::field::server, "cynamoDB/2.5.4");

    // Echo the DynamoDB JSON protocol version the client used (1.0 or 1.1); default
    // to 1.0. Input is otherwise content-type-lenient, matching AWS.
    auto ct_view = req_[http::field::content_type];
    const bool json_11 =
        std::string_view(ct_view.data(), ct_view.size()).find("json-1.1") != std::string_view::npos;
    res.set(http::field::content_type,
            json_11 ? "application/x-amz-json-1.1" : "application/x-amz-json-1.0");

    // Request ID
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    char req_id[17];
    snprintf(req_id, sizeof(req_id), "%016llx", static_cast<unsigned long long>(dist(rng)));
    res.set("x-amzn-RequestId", req_id);

    const auto method = req_.method();
    const bool is_health = req_.target() == "/health";

    if (is_health && (method == http::verb::get || method == http::verb::head ||
                      method == http::verb::post)) {
        res.body() = "{\"status\":\"healthy\"}";
    } else if (method != http::verb::post) {
        // The DynamoDB data plane only accepts POST; reject other methods cleanly
        // with 405 instead of routing them through the operation dispatcher.
        res.result(http::status::method_not_allowed);
        res.set(http::field::allow, "POST");
        res.set("x-amzn-ErrorType", "com.amazonaws.dynamodb.v20120810#UnknownOperationException");
        res.body() = json::JsonSerializer::serialize_error(
            "com.amazonaws.dynamodb.v20120810#UnknownOperationException",
            "Only POST requests are supported by the cynamoDB endpoint");
    } else if (auto auth_err = check_auth(); auth_err.has_value()) {
        // Opt-in SigV4 enforcement rejected this request before dispatch.
        res.result(static_cast<http::status>(auth_err->status));
        res.set("x-amzn-ErrorType", auth_err->error_type);
        res.body() = std::move(auth_err->body);
    } else {
        // DynamoDB protocol: POST "/" with the operation in the X-Amz-Target
        // header and a JSON body. Route to the operation handler.
        auto target_view = req_["X-Amz-Target"];
        std::string target(target_view.data(), target_view.size());
        api::Operation op = api::ApiDispatcher::parse_target(target);

        api::ApiResult result =
            api::handle_operation(*ctx_.table_manager, *ctx_.storage_engine, op, req_.body(),
                                  ctx_.capacity_manager.get(), ctx_.stream_manager.get(),
                                  ctx_.backup_manager.get());

        res.result(static_cast<http::status>(result.status));
        if (!result.error_type.empty()) {
            res.set("x-amzn-ErrorType", result.error_type);
        }
        res.body() = std::move(result.body);
    }

    res.prepare_payload();
    do_write(std::move(res));
}

std::optional<api::ApiResult> HttpSession::check_auth() {
    if (!ctx_.require_auth) return std::nullopt;

    auto make_error = [](const std::string& type, const std::string& message) {
        return api::ApiResult{
            400, "com.amazonaws.dynamodb.v20120810#" + type,
            json::JsonSerializer::serialize_error("com.amazonaws.dynamodb.v20120810#" + type, message)};
    };

    auto auth_view = req_[http::field::authorization];
    if (auth_view.empty()) {
        return make_error("MissingAuthenticationTokenException",
                          "Request is missing Authentication Token");
    }
    // Keep the header bytes alive for the lifetime of the SigV4 string_views.
    const std::string auth_header(auth_view.data(), auth_view.size());
    auto parsed = auth::SigV4Parser::parse_authorization_header(auth_header);
    if (!parsed) {
        return make_error("IncompleteSignatureException",
                          "Authorization header requires a valid AWS4-HMAC-SHA256 credential and signature");
    }

    // Resolve the secret for the presented access key.
    auto secret = ctx_.credential_store->secret_for(std::string(parsed->access_key));
    if (!secret) {
        return make_error("UnrecognizedClientException",
                          "The security token included in the request is invalid (unknown access key)");
    }

    // Build the canonical-request inputs from the live HTTP request. DynamoDB uses
    // POST "/" with no query string; the payload is the raw body.
    auth::SigV4Credential credential;
    credential.access_key = core::String(parsed->access_key);
    credential.secret_key = core::String(*secret);

    auth::SigV4VerifyRequest vr;
    vr.method = "POST";
    vr.canonical_uri = "/";
    vr.canonical_query_string = "";
    vr.payload = req_.body();
    for (const auto& name : parsed->signed_headers) {
        auto hv = req_[std::string(name)];  // beast lookup is case-insensitive
        vr.signed_header_values[name] = std::string_view(hv.data(), hv.size());
    }

    auto verified = auth::SigV4Verifier::verify_request(*parsed, credential, vr);
    if (!verified) {
        const auto err = verified.error();
        if (err == auth::SigV4VerifyError::MissingSignedHeader) {
            return make_error("IncompleteSignatureException", "A required signed header was not present");
        }
        return make_error("InvalidSignatureException",
                          "The request signature we calculated does not match the signature you provided");
    }
    return std::nullopt;
}

void HttpSession::do_write(http::response<http::string_body> res) {
    const bool should_trim = res.body().capacity() >= kHeapTrimResponseThreshold ||
                             req_.body().capacity() >= kHeapTrimResponseThreshold;
    auto sp = std::make_shared<http::response<http::string_body>>(std::move(res));
    http::async_write(stream_, *sp,
                      [self = shared_from_this(), sp, should_trim]
                      (beast::error_code ec, std::size_t bytes) mutable {
                          const bool close = sp->need_eof();
                          sp.reset();
                          self->on_write(ec, bytes, close);
                          // on_write starts the next read, which clears req_; the response
                          // holder above is also gone. This is the first point where a trim
                          // can release the bulk-read high-water allocation.
                          trim_heap_after_completed_bulk_response(should_trim);
                      });
}

void HttpSession::on_write(beast::error_code ec, std::size_t bytes_transferred, bool close) {
    (void)bytes_transferred;
    if (ec) return;
    if (close) {
        req_ = {};
        buffer_.consume(buffer_.size());
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
        return;
    }
    do_read();
}

} // namespace cynamodb::http
