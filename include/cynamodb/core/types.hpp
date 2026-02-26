#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <memory>
#include <optional>
#include <memory_resource>
#include <string_view>

namespace cynamodb::core {

struct StringViewLess {
    using is_transparent = void;
    bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        return lhs < rhs;
    }
};

using String = std::pmr::string;
template <typename T>
using Vector = std::pmr::vector<T>;
template <typename K, typename V>
using Map = std::pmr::map<K, V, StringViewLess>;

enum class AttributeType {
    S,    // String
    N,    // Number (stored as string in DynamoDB API)
    B,    // Binary
    BOOL, // Boolean
    NUL,  // Null
    M,    // Map
    L,    // List
    SS,   // String Set
    NS,   // Number Set
    BS    // Binary Set
};

struct AttributeValue;

using MapValue = std::pmr::map<String, std::shared_ptr<AttributeValue>, StringViewLess>;
using ListValue = std::pmr::vector<std::shared_ptr<AttributeValue>>;
struct StringSet { std::pmr::vector<String> values; };
struct NumberSet { std::pmr::vector<String> values; };
struct BinarySet { std::pmr::vector<std::pmr::vector<uint8_t>> values; };

struct AttributeValue {
    std::variant<
        String,                        // S, N
        std::pmr::vector<uint8_t>,     // B
        bool,                          // BOOL
        std::monostate,                // NULL
        MapValue,                      // M
        ListValue,                     // L
        StringSet,                     // SS
        NumberSet,                     // NS
        BinarySet                      // BS
    > value;

    AttributeType type;

    AttributeValue() = default;
    AttributeValue(std::pmr::memory_resource* res) : value(String(res)) {}
};

} // namespace cynamodb::core
