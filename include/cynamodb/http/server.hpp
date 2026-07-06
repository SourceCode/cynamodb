#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <cynamodb/context.hpp>
#include <cynamodb/api/handlers.hpp>

namespace cynamodb::http {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpServer {
public:
    // io_threads must equal the number of threads that will call run(): it sizes the
    // io_context's concurrency hint. A hint of 1 enables Asio's single-threaded
    // scheduler optimizations (no internal locking); running multiple threads on such
    // a context serializes all work onto one core. Pass the real thread count so the
    // multi-threaded, lock-protected scheduler is used when io_threads > 1.
    HttpServer(Context& ctx, int io_threads);
    ~HttpServer();

    void run(const std::string& address, unsigned short port, int threads);
    void stop();

private:
    void do_accept();
    void on_accept(beast::error_code ec, tcp::socket socket);

    Context& ctx_;
    net::io_context ioc_;
    tcp::acceptor acceptor_;
    std::vector<std::thread> worker_threads_;
    std::atomic<bool> running_{false};
};

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(tcp::socket socket, Context& ctx);
    void run();

private:
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void handle_request();
    // Returns an error result when CYNAMODB_REQUIRE_AUTH is on and the request
    // lacks a parseable SigV4 Authorization header; nullopt means allow.
    std::optional<api::ApiResult> check_auth();
    void do_write(http::response<http::string_body> res);
    void on_write(beast::error_code ec, std::size_t bytes_transferred, bool close);

    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    Context& ctx_;
};

} // namespace cynamodb::http
