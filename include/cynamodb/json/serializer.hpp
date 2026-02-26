#pragma once

#include <string>
#include <string_view>
#include <span>
#include <expected>
#include <simdjson.h>
#include <cynamodb/core/types.hpp>
#include <cynamodb/core/schema.hpp>

namespace cynamodb::json {

enum class SerializationError { BufferFull, InvalidType, DepthLimitExceeded };

class JsonWriter {
public:
    explicit JsonWriter(std::span<char> buffer) noexcept : buffer_(buffer), offset_(0), depth_(0) {}

    [[nodiscard]] std::expected<void, SerializationError> write_string(std::string_view str) noexcept;
    [[nodiscard]] std::expected<void, SerializationError> write_escaped_string(std::string_view str) noexcept;
    [[nodiscard]] std::expected<void, SerializationError> write_number(uint64_t num) noexcept;
    [[nodiscard]] std::expected<void, SerializationError> write_number(int64_t num) noexcept;
    [[nodiscard]] std::expected<void, SerializationError> write_number(double num) noexcept;
    [[nodiscard]] std::expected<void, SerializationError> write_bool(bool b) noexcept;
    [[nodiscard]] std::expected<void, SerializationError> write_null() noexcept;
    [[nodiscard]] std::expected<void, SerializationError> write_raw(std::string_view raw) noexcept;
    [[nodiscard]] std::expected<void, SerializationError> write_char(char c) noexcept;

    [[nodiscard]] std::expected<void, SerializationError> write(const core::AttributeValue& val) noexcept;

    size_t get_offset() const noexcept { return offset_; }
    void reset() noexcept { offset_ = 0; depth_ = 0; }
    
    int depth() const noexcept { return depth_; }
    void inc_depth() noexcept { ++depth_; }
    void dec_depth() noexcept { --depth_; }

private:
    std::span<char> buffer_;
    size_t offset_;
    int depth_;
};

class JsonParser {
public:
    static core::AttributeValue parse_attribute_value(simdjson::dom::element el);
    static core::TableDefinition parse_table_definition(simdjson::dom::element el);
};

class JsonSerializer {
public:
    static std::string serialize_attribute_value(const core::AttributeValue& val);
    static std::string serialize_item(const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item);
    static std::string serialize_error(const std::string& type, const std::string& message);
    static size_t calculate_item_size(const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item);
private:
    static size_t calculate_attr_size(const core::AttributeValue& val);
};

} // namespace cynamodb::json
