#include <cynamodb/http/server.hpp>
#include <boost/asio/dispatch.hpp>
#include <iostream>
#include <random>

namespace cynamodb::http {

HttpServer::HttpServer(Context& ctx)
    : ctx_(ctx), ioc_(1), acceptor_(net::make_strand(ioc_)) {}

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
    res.set(http::field::server, "cynamoDB/0.1.0");
    res.set(http::field::content_type, "application/x-amz-json-1.0");
    
    // Request ID
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    char req_id[17];
    snprintf(req_id, sizeof(req_id), "%016llx", dist(rng));
    res.set("x-amzn-RequestId", req_id);

    if (req_.target() == "/health") {
        res.body() = "{\"status\":\"healthy\"}";
    } else {
        res.body() = "{}";
    }
    
    res.prepare_payload();
    do_write(std::move(res));
}

void HttpSession::do_write(http::response<http::string_body> res) {
    auto sp = std::make_shared<http::response<http::string_body>>(std::move(res));
    http::async_write(stream_, *sp,
                      [self = shared_from_this(), sp](beast::error_code ec, std::size_t bytes) {
                          self->on_write(ec, bytes, sp->need_eof());
                      });
}

void HttpSession::on_write(beast::error_code ec, std::size_t bytes_transferred, bool close) {
    (void)bytes_transferred;
    if (ec) return;
    if (close) {
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
        return;
    }
    do_read();
}

} // namespace cynamodb::http
