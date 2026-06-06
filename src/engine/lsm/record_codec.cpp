#include <cynamodb/engine/lsm/record_codec.hpp>

#include <cstdint>
#include <cstring>
#include <variant>

namespace cynamodb::engine::lsm {

namespace {

using core::AttributeType;
using core::AttributeValue;

void put_u32(std::string& out, uint32_t v) {
    char buf[4];
    std::memcpy(buf, &v, sizeof(v));
    out.append(buf, sizeof(buf));
}

void put_bytes(std::string& out, std::string_view bytes) {
    put_u32(out, static_cast<uint32_t>(bytes.size()));
    out.append(bytes.data(), bytes.size());
}

void encode_value(std::string& out, const AttributeValue& av) {
    out.push_back(static_cast<char>(av.type));
    switch (av.type) {
        case AttributeType::S:
        case AttributeType::N: {
            const auto& s = std::get<core::String>(av.value);
            put_bytes(out, std::string_view(s.data(), s.size()));
            break;
        }
        case AttributeType::B: {
            const auto& b = std::get<std::pmr::vector<uint8_t>>(av.value);
            put_bytes(out, std::string_view(reinterpret_cast<const char*>(b.data()), b.size()));
            break;
        }
        case AttributeType::BOOL:
            out.push_back(std::get<bool>(av.value) ? 1 : 0);
            break;
        case AttributeType::NUL:
            break;
        case AttributeType::M: {
            const auto& m = std::get<core::MapValue>(av.value);
            put_u32(out, static_cast<uint32_t>(m.size()));
            for (const auto& [k, v] : m) {
                put_bytes(out, std::string_view(k.data(), k.size()));
                encode_value(out, *v);
            }
            break;
        }
        case AttributeType::L: {
            const auto& l = std::get<core::ListValue>(av.value);
            put_u32(out, static_cast<uint32_t>(l.size()));
            for (const auto& v : l) {
                encode_value(out, *v);
            }
            break;
        }
        case AttributeType::SS: {
            const auto& s = std::get<core::StringSet>(av.value);
            put_u32(out, static_cast<uint32_t>(s.values.size()));
            for (const auto& x : s.values) {
                put_bytes(out, std::string_view(x.data(), x.size()));
            }
            break;
        }
        case AttributeType::NS: {
            const auto& s = std::get<core::NumberSet>(av.value);
            put_u32(out, static_cast<uint32_t>(s.values.size()));
            for (const auto& x : s.values) {
                put_bytes(out, std::string_view(x.data(), x.size()));
            }
            break;
        }
        case AttributeType::BS: {
            const auto& s = std::get<core::BinarySet>(av.value);
            put_u32(out, static_cast<uint32_t>(s.values.size()));
            for (const auto& x : s.values) {
                put_bytes(out, std::string_view(reinterpret_cast<const char*>(x.data()), x.size()));
            }
            break;
        }
    }
}

struct Reader {
    std::string_view data;
    size_t pos = 0;

    bool read_u32(uint32_t& out) {
        if (pos + sizeof(uint32_t) > data.size()) return false;
        std::memcpy(&out, data.data() + pos, sizeof(uint32_t));
        pos += sizeof(uint32_t);
        return true;
    }
    bool read_u8(uint8_t& out) {
        if (pos + 1 > data.size()) return false;
        out = static_cast<uint8_t>(data[pos++]);
        return true;
    }
    bool read_bytes(uint32_t n, std::string_view& out) {
        if (pos + n > data.size()) return false;
        out = data.substr(pos, n);
        pos += n;
        return true;
    }
};

std::shared_ptr<AttributeValue> decode_value(Reader& r) {
    uint8_t tag = 0;
    if (!r.read_u8(tag)) return nullptr;
    auto av = std::make_shared<AttributeValue>();
    av->type = static_cast<AttributeType>(tag);
    switch (av->type) {
        case AttributeType::S:
        case AttributeType::N: {
            uint32_t n = 0;
            std::string_view sv;
            if (!r.read_u32(n) || !r.read_bytes(n, sv)) return nullptr;
            av->value = core::String(sv.data(), sv.size());
            break;
        }
        case AttributeType::B: {
            uint32_t n = 0;
            std::string_view sv;
            if (!r.read_u32(n) || !r.read_bytes(n, sv)) return nullptr;
            av->value = std::pmr::vector<uint8_t>(sv.begin(), sv.end());
            break;
        }
        case AttributeType::BOOL: {
            uint8_t b = 0;
            if (!r.read_u8(b)) return nullptr;
            av->value = (b != 0);
            break;
        }
        case AttributeType::NUL:
            av->value = std::monostate{};
            break;
        case AttributeType::M: {
            uint32_t count = 0;
            if (!r.read_u32(count)) return nullptr;
            core::MapValue m;
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t kl = 0;
                std::string_view key;
                if (!r.read_u32(kl) || !r.read_bytes(kl, key)) return nullptr;
                auto v = decode_value(r);
                if (!v) return nullptr;
                m[core::String(key.data(), key.size())] = std::move(v);
            }
            av->value = std::move(m);
            break;
        }
        case AttributeType::L: {
            uint32_t count = 0;
            if (!r.read_u32(count)) return nullptr;
            core::ListValue l;
            for (uint32_t i = 0; i < count; ++i) {
                auto v = decode_value(r);
                if (!v) return nullptr;
                l.push_back(std::move(v));
            }
            av->value = std::move(l);
            break;
        }
        case AttributeType::SS: {
            uint32_t count = 0;
            if (!r.read_u32(count)) return nullptr;
            core::StringSet s;
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t n = 0;
                std::string_view sv;
                if (!r.read_u32(n) || !r.read_bytes(n, sv)) return nullptr;
                s.values.push_back(core::String(sv.data(), sv.size()));
            }
            av->value = std::move(s);
            break;
        }
        case AttributeType::NS: {
            uint32_t count = 0;
            if (!r.read_u32(count)) return nullptr;
            core::NumberSet s;
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t n = 0;
                std::string_view sv;
                if (!r.read_u32(n) || !r.read_bytes(n, sv)) return nullptr;
                s.values.push_back(core::String(sv.data(), sv.size()));
            }
            av->value = std::move(s);
            break;
        }
        case AttributeType::BS: {
            uint32_t count = 0;
            if (!r.read_u32(count)) return nullptr;
            core::BinarySet s;
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t n = 0;
                std::string_view sv;
                if (!r.read_u32(n) || !r.read_bytes(n, sv)) return nullptr;
                s.values.emplace_back(sv.begin(), sv.end());
            }
            av->value = std::move(s);
            break;
        }
        default:
            return nullptr;
    }
    return av;
}

}  // namespace

std::string encode_attributes(const RecordAttributes& attrs) {
    std::string out;
    put_u32(out, static_cast<uint32_t>(attrs.size()));
    for (const auto& [name, v] : attrs) {
        put_bytes(out, name);
        encode_value(out, *v);
    }
    return out;
}

std::optional<RecordAttributes> decode_attributes(std::string_view data) {
    Reader r{data, 0};
    uint32_t count = 0;
    if (!r.read_u32(count)) return std::nullopt;
    RecordAttributes attrs;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t nl = 0;
        std::string_view name;
        if (!r.read_u32(nl) || !r.read_bytes(nl, name)) return std::nullopt;
        auto v = decode_value(r);
        if (!v) return std::nullopt;
        attrs[std::string(name)] = std::move(v);
    }
    return attrs;
}

}  // namespace cynamodb::engine::lsm
