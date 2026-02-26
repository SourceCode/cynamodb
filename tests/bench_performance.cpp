#include <iostream>
#include <chrono>
#include <vector>
#include <numeric>
#include <algorithm>
#include <cynamodb/json/serializer.hpp>
#include <cynamodb/core/types.hpp>

using namespace cynamodb::core;
using namespace cynamodb::json;

void benchmark_json_serialization() {
    std::cout << "Benchmarking JSON Serialization..." << std::endl;
    
    std::map<std::string, std::shared_ptr<AttributeValue>, StringViewLess> item;
    auto val = std::make_shared<AttributeValue>();
    val->type = AttributeType::S;
    val->value = String("test-value-12345");
    item["pk"] = val;
    
    const int iterations = 100000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        std::string s = JsonSerializer::serialize_item(item);
        // Prevent compiler from optimizing away the loop
        if (s.empty()) std::cout << "Error" << std::endl;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Total time: " << duration << " us" << std::endl;
    std::cout << "  Avg time:   " << static_cast<double>(duration) / iterations << " us" << std::endl;
}

int main() {
    benchmark_json_serialization();
    return 0;
}
