#include <catch2/catch_test_macros.hpp>
#include <cynamodb/json/serializer.hpp>
#include <simdjson.h>
#include <limits>
#include <stdexcept>

using namespace cynamodb::json;
using namespace cynamodb::core;

TEST_CASE("JSON Parser basic types", "[json]") {
    simdjson::dom::parser parser;
    
    SECTION("S type") {
        auto doc = parser.parse(std::string(R"({"S":"test"})"));
        auto val = JsonParser::parse_attribute_value(doc.value());
        REQUIRE(val.type == AttributeType::S);
        REQUIRE(std::get<String>(val.value) == "test");
    }

    SECTION("N type") {
        auto doc = parser.parse(std::string(R"({"N":"123.45"})"));
        auto val = JsonParser::parse_attribute_value(doc.value());
        REQUIRE(val.type == AttributeType::N);
        REQUIRE(std::get<String>(val.value) == "123.45");
    }

    SECTION("BOOL type") {
        auto doc = parser.parse(std::string(R"({"BOOL":true})"));
        auto val = JsonParser::parse_attribute_value(doc.value());
        REQUIRE(val.type == AttributeType::BOOL);
        REQUIRE(std::get<bool>(val.value) == true);
    }

    SECTION("NULL type") {
        auto doc = parser.parse(std::string(R"({"NULL":true})"));
        auto val = JsonParser::parse_attribute_value(doc.value());
        REQUIRE(val.type == AttributeType::NUL);
    }
}

TEST_CASE("JSON Parser error handling", "[json]") {
    simdjson::dom::parser parser;

    SECTION("Empty object") {
        auto doc = parser.parse(std::string("{}"));
        REQUIRE_THROWS_AS(JsonParser::parse_attribute_value(doc.value()), std::invalid_argument);
    }

    SECTION("Multiple type keys") {
        auto doc = parser.parse(std::string(R"({"S":"a","N":"1"})"));
        REQUIRE_THROWS_AS(JsonParser::parse_attribute_value(doc.value()), std::invalid_argument);
    }

    SECTION("Unsupported type") {
        auto doc = parser.parse(std::string(R"({"X":"abc"})"));
        REQUIRE_THROWS_AS(JsonParser::parse_attribute_value(doc.value()), std::invalid_argument);
    }
}

TEST_CASE("JSON Serializer basic types", "[json]") {
    SECTION("S type") {
        AttributeValue val;
        val.type = AttributeType::S;
        val.value = String("test");
        REQUIRE(JsonSerializer::serialize_attribute_value(val) == R"({"S":"test"})");
    }

    SECTION("N type") {
        AttributeValue val;
        val.type = AttributeType::N;
        val.value = String("123");
        REQUIRE(JsonSerializer::serialize_attribute_value(val) == R"({"N":"123"})");
    }

    SECTION("BOOL type") {
        AttributeValue val;
        val.type = AttributeType::BOOL;
        val.value = true;
        REQUIRE(JsonSerializer::serialize_attribute_value(val) == R"({"BOOL":true})");
    }

    SECTION("NULL type") {
        AttributeValue val;
        val.type = AttributeType::NUL;
        REQUIRE(JsonSerializer::serialize_attribute_value(val) == R"({"NULL":true})");
    }
}

TEST_CASE("JSON Writer Round-trip", "[json]") {
    std::string json_str = R"({"M":{"bool":{"BOOL":true},"n":{"N":"123"},"nul":{"NULL":true},"s":{"S":"test"}}})";
    simdjson::dom::parser parser;
    auto doc = parser.parse(std::string(json_str));
    auto val = JsonParser::parse_attribute_value(doc.value());

    char buf[1024];
    JsonWriter writer(std::span<char>{buf, sizeof(buf)});
    auto res = writer.write(val);
    REQUIRE(res.has_value());
    
    std::string serialized(buf, writer.get_offset());
    auto doc2 = parser.parse(std::string(serialized));
    auto val2 = JsonParser::parse_attribute_value(doc2.value());
    
    REQUIRE(val.type == val2.type);
    REQUIRE(std::get<MapValue>(val2.value).size() == 4);
}
