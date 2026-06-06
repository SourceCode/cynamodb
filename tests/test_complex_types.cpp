// Regression tests for finding #1: complex attribute types (M/L/SS/NS/BS/B) must
// round-trip end-to-end — through the JSON wire codec, the WAL record codec, and
// crucially the SSTable codec (previously only S/N/BOOL survived a flush, so maps
// vanished and lists/sets became NULL).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cynamodb/engine/lsm/lsm_engine.hpp>
#include <cynamodb/engine/lsm/record_codec.hpp>
#include <cynamodb/engine/lsm/sstable.hpp>
#include <cynamodb/core/memory.hpp>
#include <cynamodb/json/serializer.hpp>
#include <cynamodb/utils/base64.hpp>

#include <simdjson.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

using namespace cynamodb;
using Catch::Matchers::ContainsSubstring;

namespace {

std::shared_ptr<core::AttributeValue> S(const std::string& s) {
    auto v = std::make_shared<core::AttributeValue>();
    v->type = core::AttributeType::S;
    v->value = core::String(s);
    return v;
}
std::shared_ptr<core::AttributeValue> N(const std::string& s) {
    auto v = std::make_shared<core::AttributeValue>();
    v->type = core::AttributeType::N;
    v->value = core::String(s);
    return v;
}

std::string tmp_path(const std::string& tag) {
    static std::atomic<uint64_t> counter{0};
    auto dir = std::filesystem::temp_directory_path() /
               ("cynamodb_ct_" + tag + "_" + std::to_string(counter.fetch_add(1)));
    std::filesystem::create_directories(dir);
    return (dir / "table.sst").string();
}

}  // namespace

TEST_CASE("base64 round-trips and rejects malformed input", "[base64]") {
    REQUIRE(utils::base64_encode(std::string_view("")) == "");
    REQUIRE(utils::base64_encode(std::string_view("f")) == "Zg==");
    REQUIRE(utils::base64_encode(std::string_view("fo")) == "Zm8=");
    REQUIRE(utils::base64_encode(std::string_view("foo")) == "Zm9v");
    REQUIRE(utils::base64_encode(std::string_view("foobar")) == "Zm9vYmFy");

    for (const std::string sample : {std::string(""), std::string("A"), std::string("hello world"),
                                     std::string("\x00\x01\x02\xff\xfe", 5)}) {
        auto enc = utils::base64_encode(std::string_view(sample));
        auto dec = utils::base64_decode(enc);
        REQUIRE(dec.has_value());
        REQUIRE(std::string(dec->begin(), dec->end()) == sample);
    }

    REQUIRE_FALSE(utils::base64_decode("Zg=").has_value());   // bad length
    REQUIRE_FALSE(utils::base64_decode("Zg=A").has_value());  // padding misuse
    REQUIRE_FALSE(utils::base64_decode("****").has_value());  // illegal chars
}

TEST_CASE("SSTable codec round-trips every attribute type after flush", "[lsm][sstable][complex]") {
    // Build an item exercising M (with nesting), L, SS, NS, BS, B alongside scalars.
    core::AttributeValue map_v;
    map_v.type = core::AttributeType::M;
    core::MapValue m;
    m[core::String("a")] = S("x");
    m[core::String("n")] = N("2");
    {
        auto deep = std::make_shared<core::AttributeValue>();
        deep->type = core::AttributeType::M;
        core::MapValue inner;
        inner[core::String("z")] = N("9");
        deep->value = std::move(inner);
        m[core::String("deep")] = deep;
    }
    map_v.value = std::move(m);

    core::AttributeValue list_v;
    list_v.type = core::AttributeType::L;
    core::ListValue l;
    l.push_back(S("t1"));
    l.push_back(N("3"));
    list_v.value = std::move(l);

    core::AttributeValue ss_v;
    ss_v.type = core::AttributeType::SS;
    ss_v.value = core::StringSet{{core::String("a"), core::String("b")}};

    core::AttributeValue ns_v;
    ns_v.type = core::AttributeType::NS;
    ns_v.value = core::NumberSet{{core::String("1"), core::String("2")}};

    core::AttributeValue bs_v;
    bs_v.type = core::AttributeType::BS;
    core::BinarySet bset;
    bset.values.push_back(std::pmr::vector<uint8_t>{1, 2, 3});
    bs_v.value = std::move(bset);

    core::AttributeValue b_v;
    b_v.type = core::AttributeType::B;
    b_v.value = std::pmr::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF};

    engine::lsm::Skiplist::SnapshotEntry entry;
    entry.is_deleted = false;
    entry.attributes["pk"] = S("CPX");
    entry.attributes["meta"] = std::make_shared<core::AttributeValue>(std::move(map_v));
    entry.attributes["tags"] = std::make_shared<core::AttributeValue>(std::move(list_v));
    entry.attributes["ss"] = std::make_shared<core::AttributeValue>(std::move(ss_v));
    entry.attributes["ns"] = std::make_shared<core::AttributeValue>(std::move(ns_v));
    entry.attributes["bs"] = std::make_shared<core::AttributeValue>(std::move(bs_v));
    entry.attributes["bin"] = std::make_shared<core::AttributeValue>(std::move(b_v));

    std::map<std::string, engine::lsm::Skiplist::SnapshotEntry, core::StringViewLess> entries;
    entries["CPX"] = std::move(entry);

    std::string path = tmp_path("sst");
    REQUIRE(engine::lsm::SSTable::create(path, entries) == path);

    engine::lsm::SSTable sst(path);
    auto got = sst.get("CPX");
    REQUIRE(got.has_value());

    // The whole row must survive (not vanish), and each complex type intact.
    REQUIRE(got->at("meta")->type == core::AttributeType::M);
    const auto& mr = std::get<core::MapValue>(got->at("meta")->value);
    REQUIRE(mr.at(core::String("a"))->type == core::AttributeType::S);
    REQUIRE(mr.at(core::String("deep"))->type == core::AttributeType::M);

    REQUIRE(got->at("tags")->type == core::AttributeType::L);
    REQUIRE(std::get<core::ListValue>(got->at("tags")->value).size() == 2);

    REQUIRE(got->at("ss")->type == core::AttributeType::SS);
    REQUIRE(std::get<core::StringSet>(got->at("ss")->value).values.size() == 2);

    REQUIRE(got->at("ns")->type == core::AttributeType::NS);
    REQUIRE(got->at("bs")->type == core::AttributeType::BS);

    REQUIRE(got->at("bin")->type == core::AttributeType::B);
    const auto& bin = std::get<std::pmr::vector<uint8_t>>(got->at("bin")->value);
    REQUIRE(bin == std::pmr::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF});
}

TEST_CASE("LsmEngine: a map survives a memtable flush (no silent data loss)", "[lsm][complex][flush]") {
    static std::atomic<uint64_t> counter{0};
    auto dir = std::filesystem::temp_directory_path() /
               ("cynamodb_lsm_flush_" + std::to_string(counter.fetch_add(1)));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    auto arena = std::make_shared<core::Arena>();
    engine::lsm::LsmEngine eng(dir.string(), arena);

    // Store a map item, then write enough rows to force the memtable to freeze and
    // flush to an SSTable (the flush threshold is 1000 entries).
    engine::StorageEngine::AttributeMap item;
    item["pk"] = S("MAPFLUSH");
    {
        auto mv = std::make_shared<core::AttributeValue>();
        mv->type = core::AttributeType::M;
        core::MapValue m;
        m[core::String("a")] = S("keepme");
        mv->value = std::move(m);
        item["meta"] = mv;
    }
    eng.put("T", std::string("\x01") + "MAPFLUSH", item);
    for (int i = 0; i < 1500; ++i) {
        engine::StorageEngine::AttributeMap filler;
        filler["pk"] = S("f" + std::to_string(i));
        eng.put("T", "k" + std::to_string(i), filler);
    }

    auto got = eng.get("T", std::string("\x01") + "MAPFLUSH");
    REQUIRE(got.has_value());                       // the whole row must not vanish
    REQUIRE(got->count("meta") == 1);
    REQUIRE(got->at("meta")->type == core::AttributeType::M);
    REQUIRE(std::get<core::MapValue>(got->at("meta")->value).at(core::String("a"))->type ==
            core::AttributeType::S);
}

TEST_CASE("record_codec single-value round-trips complex types", "[lsm][codec]") {
    core::AttributeValue list_v;
    list_v.type = core::AttributeType::L;
    core::ListValue l;
    l.push_back(S("hello"));
    list_v.value = std::move(l);

    std::string encoded = engine::lsm::encode_attribute_value(list_v);
    auto decoded = engine::lsm::decode_attribute_value(encoded);
    REQUIRE(decoded);
    REQUIRE(decoded->type == core::AttributeType::L);
    REQUIRE(std::get<core::ListValue>(decoded->value).size() == 1);
}

TEST_CASE("JSON wire codec round-trips L/SS/NS/BS/B", "[json][complex]") {
    simdjson::dom::parser parser;
    std::string body =
        R"({"pk":{"S":"k"},"tags":{"L":[{"S":"t1"},{"N":"2"}]},)"
        R"("ss":{"SS":["a","b"]},"ns":{"NS":["1","2"]},)"
        R"("bin":{"B":"3q2+7w=="},"bs":{"BS":["AQID"]}})";
    simdjson::dom::element doc = parser.parse(body);

    std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess> item;
    for (auto field : doc.get_object()) {
        item[std::string(field.key)] =
            std::make_shared<core::AttributeValue>(json::JsonParser::parse_attribute_value(field.value));
    }

    REQUIRE(item["tags"]->type == core::AttributeType::L);
    REQUIRE(item["bin"]->type == core::AttributeType::B);
    REQUIRE(std::get<std::pmr::vector<uint8_t>>(item["bin"]->value) ==
            (std::pmr::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));

    std::string out = json::JsonSerializer::serialize_item(item);
    // Round-trips must NOT degrade to {"NULL":true}.
    REQUIRE_THAT(out, ContainsSubstring("\"tags\":{\"L\":["));
    REQUIRE_THAT(out, ContainsSubstring("\"ss\":{\"SS\":["));
    REQUIRE_THAT(out, ContainsSubstring("\"ns\":{\"NS\":["));
    REQUIRE_THAT(out, ContainsSubstring("\"bin\":{\"B\":\"3q2+7w==\"}"));
    REQUIRE_THAT(out, ContainsSubstring("\"bs\":{\"BS\":[\"AQID\"]}"));
    REQUIRE(out.find("NULL") == std::string::npos);
}

TEST_CASE("N values are escaped on output so the response stays valid JSON", "[json]") {
    // N is stored as the raw wire string with no numeric validation, so a quote in
    // it must be escaped or it would corrupt the whole response body.
    std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess> item;
    item["n"] = N("1\"x");
    std::string out = json::JsonSerializer::serialize_item(item);
    REQUIRE(out == R"({"n":{"N":"1\"x"}})");
}

TEST_CASE("invalid base64 in a B attribute is rejected at parse time", "[json][complex]") {
    simdjson::dom::parser parser;
    std::string body = R"({"B":"not valid base64!"})";
    simdjson::dom::element doc = parser.parse(body);
    REQUIRE_THROWS(json::JsonParser::parse_attribute_value(doc));
}
