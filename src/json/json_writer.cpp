#include <cynamodb/json/serializer.hpp>
#include <cynamodb/utils/base64.hpp>
#include <charconv>
#include <string>
#include <variant>

namespace cynamodb::json {

std::expected<void, SerializationError> JsonWriter::write_raw(std::string_view raw) noexcept {
    if (offset_ + raw.size() > buffer_.size()) return std::unexpected(SerializationError::BufferFull);
    std::copy(raw.begin(), raw.end(), buffer_.begin() + offset_);
    offset_ += raw.size();
    return {};
}

std::expected<void, SerializationError> JsonWriter::write_char(char c) noexcept {
    if (offset_ + 1 > buffer_.size()) return std::unexpected(SerializationError::BufferFull);
    buffer_[offset_++] = c;
    return {};
}

std::expected<void, SerializationError> JsonWriter::write_string(std::string_view str) noexcept {
    if (auto res = write_char('"'); !res) return res;
    if (auto res = write_escaped_string(str); !res) return res;
    return write_char('"');
}

std::expected<void, SerializationError> JsonWriter::write_escaped_string(std::string_view str) noexcept {
    for (char c : str) {
        if (static_cast<unsigned char>(c) < 0x20 || c == '"' || c == '\\') {
            switch (c) {
                case '"': if (auto r = write_raw("\\\""); !r) return r; break;
                case '\\': if (auto r = write_raw("\\\\"); !r) return r; break;
                case '\b': if (auto r = write_raw("\\b"); !r) return r; break;
                case '\f': if (auto r = write_raw("\\f"); !r) return r; break;
                case '\n': if (auto r = write_raw("\\n"); !r) return r; break;
                case '\r': if (auto r = write_raw("\\r"); !r) return r; break;
                case '\t': if (auto r = write_raw("\\t"); !r) return r; break;
                default:
                    static constexpr char kHex[] = "0123456789abcdef";
                    char buf[6] = {'\\', 'u', '0', '0', kHex[(c >> 4) & 0xf], kHex[c & 0xf]};
                    if (auto r = write_raw(std::string_view(buf, 6)); !r) return r;
            }
        } else {
            if (auto r = write_char(c); !r) return r;
        }
    }
    return {};
}

std::expected<void, SerializationError> JsonWriter::write_number(uint64_t num) noexcept {
    if (offset_ + 24 > buffer_.size()) return std::unexpected(SerializationError::BufferFull);
    auto [ptr, ec] = std::to_chars(buffer_.data() + offset_, buffer_.data() + buffer_.size(), num);
    if (ec != std::errc()) return std::unexpected(SerializationError::BufferFull);
    offset_ = ptr - buffer_.data();
    return {};
}

std::expected<void, SerializationError> JsonWriter::write_number(int64_t num) noexcept {
    if (offset_ + 24 > buffer_.size()) return std::unexpected(SerializationError::BufferFull);
    auto [ptr, ec] = std::to_chars(buffer_.data() + offset_, buffer_.data() + buffer_.size(), num);
    if (ec != std::errc()) return std::unexpected(SerializationError::BufferFull);
    offset_ = ptr - buffer_.data();
    return {};
}

std::expected<void, SerializationError> JsonWriter::write_number(double num) noexcept {
    if (offset_ + 32 > buffer_.size()) return std::unexpected(SerializationError::BufferFull);
    auto [ptr, ec] = std::to_chars(buffer_.data() + offset_, buffer_.data() + buffer_.size(), num);
    if (ec != std::errc()) return std::unexpected(SerializationError::BufferFull);
    offset_ = ptr - buffer_.data();
    return {};
}

std::expected<void, SerializationError> JsonWriter::write_bool(bool b) noexcept {
    return b ? write_raw("true") : write_raw("false");
}

std::expected<void, SerializationError> JsonWriter::write_null() noexcept {
    return write_raw("null");
}

std::expected<void, SerializationError> JsonWriter::write(const core::AttributeValue& val) noexcept {
    if (depth_ > 32) return std::unexpected(SerializationError::DepthLimitExceeded);
    inc_depth();
    
    auto res = write_raw("{");
    if (!res) return res;

    switch (val.type) {
        case core::AttributeType::S:
            if (auto r = write_raw("\"S\":"); !r) return r;
            if (auto r = write_string(std::get<core::String>(val.value)); !r) return r;
            break;
        case core::AttributeType::N:
            if (auto r = write_raw("\"N\":"); !r) return r;
            if (auto r = write_string(std::get<core::String>(val.value)); !r) return r;
            break;
        case core::AttributeType::BOOL:
            if (auto r = write_raw("\"BOOL\":"); !r) return r;
            if (auto r = write_bool(std::get<bool>(val.value)); !r) return r;
            break;
        case core::AttributeType::NUL:
            if (auto r = write_raw("\"NULL\":true"); !r) return r;
            break;
        case core::AttributeType::B: {
            const auto& b = std::get<std::pmr::vector<uint8_t>>(val.value);
            if (auto r = write_raw("\"B\":"); !r) return r;
            const std::string enc = utils::base64_encode_bytes(b);  // wire format is base64
            if (auto r = write_string(enc); !r) return r;
            break;
        }
        case core::AttributeType::SS: {
            if (auto r = write_raw("\"SS\":["); !r) return r;
            const auto& ss = std::get<core::StringSet>(val.value);
            for (size_t i = 0; i < ss.values.size(); ++i) {
                if (i > 0) {
                    if (auto r = write_char(','); !r) return r;
                }
                if (auto r = write_string(ss.values[i]); !r) return r;
            }
            if (auto r = write_char(']'); !r) return r;
            break;
        }
        case core::AttributeType::NS: {
            if (auto r = write_raw("\"NS\":["); !r) return r;
            const auto& ns = std::get<core::NumberSet>(val.value);
            for (size_t i = 0; i < ns.values.size(); ++i) {
                if (i > 0) {
                    if (auto r = write_char(','); !r) return r;
                }
                if (auto r = write_string(ns.values[i]); !r) return r;
            }
            if (auto r = write_char(']'); !r) return r;
            break;
        }
        case core::AttributeType::BS: {
            if (auto r = write_raw("\"BS\":["); !r) return r;
            const auto& bs = std::get<core::BinarySet>(val.value);
            for (size_t i = 0; i < bs.values.size(); ++i) {
                if (i > 0) {
                    if (auto r = write_char(','); !r) return r;
                }
                const std::string enc = utils::base64_encode_bytes(bs.values[i]);  // wire format is base64
                if (auto r = write_string(enc); !r) return r;
            }
            if (auto r = write_char(']'); !r) return r;
            break;
        }
        case core::AttributeType::M: {
            if (auto r = write_raw("\"M\":{"); !r) return r;
            const auto& m = std::get<core::MapValue>(val.value);
            bool first = true;
            for (const auto& [k, v] : m) {
                if (!first) {
                    if (auto r = write_char(','); !r) return r;
                }
                if (auto r = write_string(k); !r) return r;
                if (auto r = write_char(':'); !r) return r;
                if (v) {
                    if (auto r = write(*v); !r) return r;
                } else {
                    if (auto r = write_raw("{\"NULL\":true}"); !r) return r;
                }
                first = false;
            }
            if (auto r = write_char('}'); !r) return r;
            break;
        }
        case core::AttributeType::L: {
            if (auto r = write_raw("\"L\":["); !r) return r;
            const auto& l = std::get<core::ListValue>(val.value);
            bool first = true;
            for (const auto& v : l) {
                if (!first) {
                    if (auto r = write_char(','); !r) return r;
                }
                if (v) {
                    if (auto r = write(*v); !r) return r;
                } else {
                    if (auto r = write_raw("{\"NULL\":true}"); !r) return r;
                }
                first = false;
            }
            if (auto r = write_char(']'); !r) return r;
            break;
        }
    }
    
    if (auto r = write_raw("}"); !r) return r;
    dec_depth();
    return {};
}

} // namespace cynamodb::json
