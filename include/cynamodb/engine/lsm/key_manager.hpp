#pragma once

#include <string>
#include <map>
#include <memory>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cctype>
#include <limits>
#include <optional>
#include <string_view>
#include <cynamodb/core/types.hpp>
#include <cynamodb/core/schema.hpp>

namespace cynamodb::engine::lsm {

class KeyManager {
public:
    static constexpr size_t kMaxEncodedKeyBytes = 1024U * 1024U;
    static constexpr size_t kMaxKeyComponentBytes = 512U * 1024U;
    static constexpr size_t kMaxKeyNameBytes = 255;
    static constexpr size_t kMaxInputKeyAttributes = 64;
    static constexpr size_t kMaxNumericKeyDigits = 38;
    static constexpr size_t kMaxKeySchemaEntries = 2;

    static std::string encode_composite_key(
        const core::TableDefinition& table_def,
        const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& key_attrs) {
        const auto names = resolve_key_names(table_def.key_schema);
        if (!names) return {};
        return encode_from_names(names->first, names->second, key_attrs);
    }

private:
    static bool has_nul_byte(std::string_view value) { return value.find('\0') != std::string_view::npos; }
    static bool has_control_chars(std::string_view value) { return std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20; }); }

    static std::optional<std::pair<std::string, std::string>> resolve_key_names(const std::vector<core::KeySchemaElement>& key_schema) {
        if (key_schema.empty() || key_schema.size() > kMaxKeySchemaEntries) return std::nullopt;
        std::string pk_name = key_schema[0].attribute_name;
        std::string sk_name = key_schema.size() > 1 ? key_schema[1].attribute_name : "";
        return std::make_pair(pk_name, sk_name);
    }

    static std::string encode_from_names(const std::string& pk_name, const std::string& sk_name, const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& key_attrs) {
        auto pk_it = key_attrs.find(pk_name);
        if (pk_it == key_attrs.end()) return {};
        std::string pk_str = attribute_to_string(*pk_it->second);
        if (pk_str.empty()) return {};
        
        std::string sk_str;
        if (!sk_name.empty()) {
            auto sk_it = key_attrs.find(sk_name);
            if (sk_it != key_attrs.end()) sk_str = attribute_to_string(*sk_it->second);
        }

        std::string out;
        uint16_t pk_len = static_cast<uint16_t>(pk_str.size());
        out.append(reinterpret_cast<const char*>(&pk_len), sizeof(pk_len));
        out.append(pk_str);
        out.append(sk_str);
        return out;
    }

    static std::string attribute_to_string(const core::AttributeValue& val) {
        if (val.type == core::AttributeType::S) {
            const auto& s = std::get<core::String>(val.value);
            return std::string(s);
        }
        if (val.type == core::AttributeType::N) {
            const auto& n = std::get<core::String>(val.value);
            return std::string(n);
        }
        return {};
    }
};

} // namespace cynamodb::engine::lsm
