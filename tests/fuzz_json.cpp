#include <cynamodb/json/serializer.hpp>
#include <simdjson.h>
#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    simdjson::dom::parser parser;
    try {
        // We need to convert the data to a string for parser.parse if it doesn't take raw bytes
        // Actually simdjson::dom::parser::parse takes (const uint8_t*, size_t) or (const char*, size_t)
        auto doc = parser.parse(Data, Size);
        if (doc.error() == simdjson::SUCCESS) {
            try {
                cynamodb::json::JsonParser::parse_attribute_value(doc.value());
            } catch (...) {
                // Ignore expected parsing errors for malformed AttributeValues
            }
        }
    } catch (...) {
        // Ignore
    }
    return 0;
}
