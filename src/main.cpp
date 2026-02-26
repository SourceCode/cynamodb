#include <iostream>
#include <simdjson.h>
#include <cynamodb/http/server.hpp>
#include <cynamodb/core/memory_resource.hpp>
#include <cynamodb/core/scheduler.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <algorithm>
#include <charconv>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

namespace {

[[maybe_unused]] std::optional<uint32_t> parse_uint_env(std::string_view raw, uint32_t min_val, uint32_t max_val) {
    uint32_t value = 0;
    auto res = std::from_chars(raw.data(), raw.data() + raw.size(), value);
    if (res.ec != std::errc() || res.ptr != raw.data() + raw.size() || value < min_val || value > max_val) {
        return std::nullopt;
    }
    return value;
}

} // namespace

int main() {
    std::cout << "cynamoDB starting..." << std::endl;
    std::cout << "simdjson active implementation: " << simdjson::get_active_implementation()->name() << std::endl;
    
    cynamodb::core::MemoryManager::initialize();
    
    try {
        const char* bind_addr_env = std::getenv("CYNAMODB_BIND_ADDR");
        const char* port_env = std::getenv("CYNAMODB_PORT");
        const char* threads_env = std::getenv("CYNAMODB_THREADS");

        std::string bind_addr = bind_addr_env ? bind_addr_env : "0.0.0.0";
        unsigned short port = 8000;
        int threads = static_cast<int>(std::thread::hardware_concurrency());

        if (port_env) {
            if (auto parsed = parse_uint_env(port_env, 1, 65535)) {
                port = static_cast<unsigned short>(*parsed);
            }
        }

        if (threads_env) {
            if (auto parsed = parse_uint_env(threads_env, 1, 1024)) {
                threads = static_cast<int>(*parsed);
            }
        }

        boost::asio::io_context ioc{threads};
        cynamodb::core::WorkStealingScheduler scheduler(threads);
        
        // Start server
        std::cout << "Listening on " << bind_addr << ":" << port << " with " << threads << " threads" << std::endl;
        
        // Block until SIGINT or SIGTERM
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&](auto, int) { ioc.stop(); });
        
        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
