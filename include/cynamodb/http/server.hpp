#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <memory>
#include <string>
#include <vector>
#include <cynamodb/context.hpp>

namespace cynamodb::http {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpServer {
public:
    explicit HttpServer(Context& ctx);
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
    void do_write(http::response<http::string_body> res);
    void on_write(beast::error_code ec, std::size_t bytes_transferred, bool close);

    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    Context& ctx_;
};

} // namespace cynamodb::http
