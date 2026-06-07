#include <cynamodb/json/serializer.hpp>
#include <cynamodb/utils/base64.hpp>
#include <cynamodb/core/sizing.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <expected>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_set>

namespace cynamodb::json {

namespace {

thread_local simdjson::ondemand::parser parser;

core::String escape_json(std::string_view input) {
    core::String out;
    out.reserve(input.size());
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (unsigned char c : input) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) {
            out += "\\u00";
            out.push_back(kHex[(c >> 4U) & 0x0FU]);
            out.push_back(kHex[c & 0x0FU]);
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

// Canonicalizes a DynamoDB number string (1.0 -> 1, +5 -> 5, -0 -> 0, 1.50 -> 1.5,
// 007 -> 7), so numeric values are representation-independent on the wire. Returns
// the input unchanged if it does not parse as a number, leaving it for the item
// validator to reject.
std::string normalize_number(std::string_view s) {
    size_t i = 0;
    const size_t n = s.size();
    if (n == 0) return std::string(s);
    bool neg = false;
    if (s[i] == '+') ++i;
    else if (s[i] == '-') { neg = true; ++i; }

    std::string int_digits;
    std::string frac_digits;
    while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) int_digits.push_back(s[i++]);
    if (i < n && s[i] == '.') {
        ++i;
        while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) frac_digits.push_back(s[i++]);
    }
    if (int_digits.empty() && frac_digits.empty()) return std::string(s);

    long long exp = 0;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        bool eneg = false;
        if (i < n && (s[i] == '+' || s[i] == '-')) { eneg = (s[i] == '-'); ++i; }
        std::string ed;
        while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) ed.push_back(s[i++]);
        if (ed.empty()) return std::string(s);
        for (char c : ed) { exp = exp * 10 + (c - '0'); if (exp > 100000) break; }
        if (eneg) exp = -exp;
    }
    if (i != n) return std::string(s);  // trailing junk -> not a number

    std::string digits = int_digits + frac_digits;       // full mantissa
    long long pexp = exp - static_cast<long long>(frac_digits.size());

    size_t lead = 0;
    while (lead + 1 < digits.size() && digits[lead] == '0') ++lead;
    digits = digits.substr(lead);
    if (digits.find_first_not_of('0') == std::string::npos) return "0";  // zero in any form
    while (digits.size() > 1 && digits.back() == '0') { digits.pop_back(); ++pexp; }

    const long long nd = static_cast<long long>(digits.size());
    const long long point_pos = nd + pexp;  // digits before the decimal point
    constexpr long long kMaxPlain = 40;
    std::string body;
    if (pexp >= 0) {
        if (pexp <= kMaxPlain) body = digits + std::string(static_cast<size_t>(pexp), '0');
    } else if (point_pos > 0) {
        body = digits.substr(0, static_cast<size_t>(point_pos)) + "." + digits.substr(static_cast<size_t>(point_pos));
    } else if (-point_pos <= kMaxPlain) {
        body = "0." + std::string(static_cast<size_t>(-point_pos), '0') + digits;
    }
    if (body.empty()) {  // extreme magnitude: canonical scientific form
        const long long adj = point_pos - 1;
        body = digits.substr(0, 1);
        if (digits.size() > 1) body += "." + digits.substr(1);
        body += "E" + std::string(adj >= 0 ? "+" : "") + std::to_string(adj);
    }
    return (neg ? "-" : "") + body;
}

} // namespace

core::AttributeValue JsonParser::parse_attribute_value(simdjson::dom::element el) {
    core::AttributeValue val;
    simdjson::dom::object obj = el.get_object();
    if (obj.size() != 1) throw std::invalid_argument("AttributeValue must contain exactly one type key");
    
    auto it = obj.begin();
    std::string_view key = it.key();
    if (key == "S") { val.type = core::AttributeType::S; val.value = core::String(it.value().get_string().value()); }
    else if (key == "N") {
        val.type = core::AttributeType::N;
        std::string norm = normalize_number(it.value().get_string().value());
        val.value = core::String(norm.data(), norm.size());
    }
    else if (key == "BOOL") { val.type = core::AttributeType::BOOL; val.value = it.value().get_bool().value(); }
    else if (key == "NULL") { val.type = core::AttributeType::NUL; val.value = std::monostate{}; }
    else if (key == "M") {
        val.type = core::AttributeType::M;
        core::MapValue map;
        for (auto field : it.value().get_object()) {
            map[core::String(field.key)] = std::make_shared<core::AttributeValue>(parse_attribute_value(field.value));
        }
        val.value = std::move(map);
    }
    else if (key == "L") {
        val.type = core::AttributeType::L;
        core::ListValue list;
        for (auto elem : it.value().get_array()) {
            list.push_back(std::make_shared<core::AttributeValue>(parse_attribute_value(elem)));
        }
        val.value = std::move(list);
    }
    else if (key == "B") {
        val.type = core::AttributeType::B;
        auto decoded = utils::base64_decode(it.value().get_string().value());
        if (!decoded) throw std::invalid_argument("B attribute value is not valid base64");
        val.value = std::pmr::vector<uint8_t>(decoded->begin(), decoded->end());
    }
    else if (key == "SS") {
        val.type = core::AttributeType::SS;
        core::StringSet set;
        for (auto elem : it.value().get_array()) {
            set.values.push_back(core::String(elem.get_string().value()));
        }
        val.value = std::move(set);
    }
    else if (key == "NS") {
        val.type = core::AttributeType::NS;
        core::NumberSet set;
        for (auto elem : it.value().get_array()) {
            std::string norm = normalize_number(elem.get_string().value());
            set.values.push_back(core::String(norm.data(), norm.size()));
        }
        val.value = std::move(set);
    }
    else if (key == "BS") {
        val.type = core::AttributeType::BS;
        core::BinarySet set;
        for (auto elem : it.value().get_array()) {
            auto decoded = utils::base64_decode(elem.get_string().value());
            if (!decoded) throw std::invalid_argument("BS attribute value is not valid base64");
            set.values.emplace_back(decoded->begin(), decoded->end());
        }
        val.value = std::move(set);
    }
    else throw std::invalid_argument("Unsupported type key");
    
    return val;
}

namespace {

core::KeyType parse_key_type(std::string_view s) {
    return s == "RANGE" ? core::KeyType::RANGE : core::KeyType::HASH;
}

std::optional<core::AttributeType> parse_attribute_type(std::string_view s) {
    if (s == "S") return core::AttributeType::S;
    if (s == "N") return core::AttributeType::N;
    if (s == "B") return core::AttributeType::B;
    return std::nullopt;
}

}  // namespace

namespace {

std::vector<core::KeySchemaElement> parse_key_schema(simdjson::dom::element el) {
    std::vector<core::KeySchemaElement> schema;
    simdjson::dom::array arr;
    if (el.get_array().get(arr) == simdjson::SUCCESS) {
        for (auto elem : arr) {
            std::string_view attr_name;
            std::string_view key_type;
            if (elem["AttributeName"].get_string().get(attr_name) == simdjson::SUCCESS &&
                elem["KeyType"].get_string().get(key_type) == simdjson::SUCCESS) {
                schema.push_back({std::string(attr_name), parse_key_type(key_type)});
            }
        }
    }
    return schema;
}

core::Projection parse_projection(simdjson::dom::element el) {
    core::Projection proj;
    proj.projection_type = core::ProjectionType::ALL;
    std::string_view ptype;
    if (el["ProjectionType"].get_string().get(ptype) == simdjson::SUCCESS) {
        if (ptype == "KEYS_ONLY") proj.projection_type = core::ProjectionType::KEYS_ONLY;
        else if (ptype == "INCLUDE") proj.projection_type = core::ProjectionType::INCLUDE;
        else proj.projection_type = core::ProjectionType::ALL;
    }
    simdjson::dom::array nka;
    if (el["NonKeyAttributes"].get_array().get(nka) == simdjson::SUCCESS) {
        for (auto a : nka) {
            std::string_view v;
            if (a.get_string().get(v) == simdjson::SUCCESS) proj.non_key_attributes.push_back(std::string(v));
        }
    }
    return proj;
}

}  // namespace

core::TableDefinition JsonParser::parse_table_definition(simdjson::dom::element el) {
    core::TableDefinition def;

    std::string_view name;
    if (el["TableName"].get_string().get(name) == simdjson::SUCCESS) {
        def.table_name = std::string(name);
    }

    simdjson::dom::element ks_el;
    if (el["KeySchema"].get(ks_el) == simdjson::SUCCESS) {
        def.key_schema = parse_key_schema(ks_el);
    }

    simdjson::dom::array gsis;
    if (el["GlobalSecondaryIndexes"].get_array().get(gsis) == simdjson::SUCCESS) {
        for (auto g : gsis) {
            core::GlobalSecondaryIndex gsi;
            std::string_view iname;
            if (g["IndexName"].get_string().get(iname) != simdjson::SUCCESS) continue;
            gsi.index_name = std::string(iname);
            simdjson::dom::element gks;
            if (g["KeySchema"].get(gks) == simdjson::SUCCESS) gsi.key_schema = parse_key_schema(gks);
            simdjson::dom::element gproj;
            if (g["Projection"].get(gproj) == simdjson::SUCCESS) gsi.projection = parse_projection(gproj);
            def.global_secondary_indexes.push_back(std::move(gsi));
        }
    }

    simdjson::dom::array lsis;
    if (el["LocalSecondaryIndexes"].get_array().get(lsis) == simdjson::SUCCESS) {
        for (auto li : lsis) {
            core::LocalSecondaryIndex lsi;
            std::string_view iname;
            if (li["IndexName"].get_string().get(iname) != simdjson::SUCCESS) continue;
            lsi.index_name = std::string(iname);
            simdjson::dom::element lks;
            if (li["KeySchema"].get(lks) == simdjson::SUCCESS) lsi.key_schema = parse_key_schema(lks);
            simdjson::dom::element lproj;
            if (li["Projection"].get(lproj) == simdjson::SUCCESS) lsi.projection = parse_projection(lproj);
            def.local_secondary_indexes.push_back(std::move(lsi));
        }
    }

    simdjson::dom::array attr_defs;
    if (el["AttributeDefinitions"].get_array().get(attr_defs) == simdjson::SUCCESS) {
        for (auto elem : attr_defs) {
            std::string_view attr_name;
            std::string_view attr_type;
            if (elem["AttributeName"].get_string().get(attr_name) == simdjson::SUCCESS &&
                elem["AttributeType"].get_string().get(attr_type) == simdjson::SUCCESS) {
                if (auto t = parse_attribute_type(attr_type)) {
                    def.attribute_definitions[std::string(attr_name)] = *t;
                }
            }
        }
    }

    std::string_view billing;
    if (el["BillingMode"].get_string().get(billing) == simdjson::SUCCESS && billing == "PROVISIONED") {
        def.billing_mode = core::BillingMode::PROVISIONED;
    }

    simdjson::dom::element ss;
    if (el["StreamSpecification"].get(ss) == simdjson::SUCCESS) {
        core::StreamSpecification spec;
        bool enabled = false;
        if (ss["StreamEnabled"].get_bool().get(enabled) == simdjson::SUCCESS) spec.stream_enabled = enabled;
        std::string_view svt;
        if (ss["StreamViewType"].get_string().get(svt) == simdjson::SUCCESS) {
            if (svt == "KEYS_ONLY") spec.stream_view_type = core::StreamViewType::KEYS_ONLY;
            else if (svt == "NEW_IMAGE") spec.stream_view_type = core::StreamViewType::NEW_IMAGE;
            else if (svt == "OLD_IMAGE") spec.stream_view_type = core::StreamViewType::OLD_IMAGE;
            else spec.stream_view_type = core::StreamViewType::NEW_AND_OLD_IMAGES;
        }
        def.stream_specification = spec;
    }

    simdjson::dom::element pt;
    if (el["ProvisionedThroughput"].get(pt) == simdjson::SUCCESS) {
        int64_t rcu = 0;
        int64_t wcu = 0;
        if (pt["ReadCapacityUnits"].get_int64().get(rcu) == simdjson::SUCCESS && rcu > 0) {
            def.provisioned_throughput.read_capacity_units = static_cast<uint64_t>(rcu);
        }
        if (pt["WriteCapacityUnits"].get_int64().get(wcu) == simdjson::SUCCESS && wcu > 0) {
            def.provisioned_throughput.write_capacity_units = static_cast<uint64_t>(wcu);
        }
        // Specifying throughput implies provisioned billing.
        if (rcu > 0 || wcu > 0) def.billing_mode = core::BillingMode::PROVISIONED;
    }

    return def;
}

std::string JsonSerializer::serialize_attribute_value(const core::AttributeValue& val) {
    core::String out;
    if (val.type == core::AttributeType::S) out = "{\"S\":\"" + escape_json(std::get<core::String>(val.value)) + "\"}";
    else if (val.type == core::AttributeType::N) out = "{\"N\":\"" + escape_json(std::get<core::String>(val.value)) + "\"}";
    else if (val.type == core::AttributeType::BOOL) out = core::String("{\"BOOL\":") + (std::get<bool>(val.value) ? "true" : "false") + "}";
    else if (val.type == core::AttributeType::NUL) out = "{\"NULL\":true}";
    else if (val.type == core::AttributeType::M) {
        out = "{\"M\":{";
        bool first = true;
        for (const auto& [k, v] : std::get<core::MapValue>(val.value)) {
            if (!first) out += ",";
            out += "\"" + escape_json(k) + "\":" + core::String(serialize_attribute_value(*v));
            first = false;
        }
        out += "}}";
    }
    else if (val.type == core::AttributeType::L) {
        out = "{\"L\":[";
        bool first = true;
        for (const auto& v : std::get<core::ListValue>(val.value)) {
            if (!first) out += ",";
            out += core::String(serialize_attribute_value(*v));
            first = false;
        }
        out += "]}";
    }
    else if (val.type == core::AttributeType::B) {
        out = "{\"B\":\"" +
              core::String(utils::base64_encode_bytes(std::get<std::pmr::vector<uint8_t>>(val.value))) +
              "\"}";
    }
    else if (val.type == core::AttributeType::SS) {
        out = "{\"SS\":[";
        bool first = true;
        for (const auto& s : std::get<core::StringSet>(val.value).values) {
            if (!first) out += ",";
            out += "\"" + escape_json(s) + "\"";
            first = false;
        }
        out += "]}";
    }
    else if (val.type == core::AttributeType::NS) {
        out = "{\"NS\":[";
        bool first = true;
        for (const auto& s : std::get<core::NumberSet>(val.value).values) {
            if (!first) out += ",";
            out += "\"" + escape_json(s) + "\"";  // escaped: N values are not validated as numeric on ingest
            first = false;
        }
        out += "]}";
    }
    else if (val.type == core::AttributeType::BS) {
        out = "{\"BS\":[";
        bool first = true;
        for (const auto& b : std::get<core::BinarySet>(val.value).values) {
            if (!first) out += ",";
            out += "\"" + core::String(utils::base64_encode_bytes(b)) + "\"";
            first = false;
        }
        out += "]}";
    }
    else out = "{\"NULL\":true}";
    return std::string(out);
}

std::string JsonSerializer::serialize_item(const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item) {
    core::String out = "{";
    bool first = true;
    for (const auto& [k, v] : item) {
        if (!first) out += ",";
        out += "\"" + escape_json(k) + "\":";
        out += core::String(serialize_attribute_value(*v));
        first = false;
    }
    out += "}";
    return std::string(out);
}

std::string JsonSerializer::serialize_error(const std::string& type, const std::string& message) {
    core::String out = "{\"__type\":\"" + escape_json(type) + "\",\"message\":\"" + escape_json(message) + "\"}";
    return std::string(out);
}

size_t JsonSerializer::calculate_item_size(const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item) {
    size_t size = 0;
    for (const auto& [k, v] : item) {
        size = core::size_saturating_add(size, k.size());
        if (v) size = core::size_saturating_add(size, core::attribute_size(*v));
    }
    return size;
}

size_t JsonSerializer::calculate_attr_size(const core::AttributeValue& val) {
    // Delegates to the shared sizing in core/sizing.hpp (previously this returned 0
    // for every non-scalar type, diverging from the item validator).
    return core::attribute_size(val);
}

} // namespace cynamodb::json
