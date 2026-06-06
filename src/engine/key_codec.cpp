#include <cynamodb/engine/key_codec.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <variant>

namespace cynamodb::engine {

namespace {

using core::AttributeType;
using core::AttributeValue;

// Escape a byte string so components can be concatenated without ambiguity while
// preserving lexicographic order across boundaries: 0x00 -> 0x00 0xFF, and a
// 0x00 0x00 terminator that sorts before any escaped content byte (so a shorter
// component sorts before a longer one sharing its prefix).
void append_escaped(std::string& out, std::string_view bytes) {
    for (char c : bytes) {
        out.push_back(c);
        if (c == '\0') {
            out.push_back('\xFF');
        }
    }
    out.push_back('\0');
    out.push_back('\0');
}

// Order-preserving encoding of an IEEE-754 double into 8 big-endian bytes so
// unsigned lexicographic order matches numeric order (negative < 0 < positive).
// Exact for integers up to 2^53; beyond that ordering is preserved but extreme
// values may collide -- a documented limitation of the local engine.
void append_number(std::string& out, double d) {
    uint64_t bits = 0;
    std::memcpy(&bits, &d, sizeof(bits));
    if (bits & 0x8000000000000000ULL) {
        bits = ~bits;
    } else {
        bits |= 0x8000000000000000ULL;
    }
    for (int i = 7; i >= 0; --i) {
        out.push_back(static_cast<char>((bits >> (i * 8)) & 0xFF));
    }
}

}  // namespace

void encode_key_component(std::string& out, const AttributeValue& av) {
    switch (av.type) {
        case AttributeType::S: {
            out.push_back('S');
            const auto& s = std::get<core::String>(av.value);
            append_escaped(out, std::string_view(s.data(), s.size()));
            break;
        }
        case AttributeType::N: {
            out.push_back('N');
            const auto& s = std::get<core::String>(av.value);
            append_number(out, std::strtod(s.c_str(), nullptr));
            break;
        }
        case AttributeType::B: {
            out.push_back('B');
            const auto& b = std::get<std::pmr::vector<uint8_t>>(av.value);
            append_escaped(out, std::string_view(reinterpret_cast<const char*>(b.data()), b.size()));
            break;
        }
        default:
            out.push_back('?');
            out.push_back('\0');
            out.push_back('\0');
            break;
    }
}

std::string encode_primary_key(const core::TableDefinition& def,
                               const StorageEngine::AttributeMap& item) {
    std::string composite;
    for (const auto& element : def.key_schema) {
        auto it = item.find(element.attribute_name);
        if (it == item.end() || !it->second) {
            composite.push_back('\0');
            composite.push_back('\0');
            continue;
        }
        encode_key_component(composite, *it->second);
    }
    return composite;
}

}  // namespace cynamodb::engine
