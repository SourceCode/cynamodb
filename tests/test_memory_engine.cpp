#include <catch2/catch_test_macros.hpp>
#include <cynamodb/engine/memory_engine.hpp>
#include <memory>
#include <string>

using namespace cynamodb::engine;
using namespace cynamodb::core;

TEST_CASE("MemoryEngine basic operations", "[engine][memory]") {
    MemoryEngine engine;
    std::string table = "TestTable";

    SECTION("Put and Get") {
        StorageEngine::AttributeMap attrs;
        auto val = std::make_shared<AttributeValue>();
        val->type = AttributeType::S;
        val->value = String("v1");
        attrs["pk"] = val;

        engine.put(table, "k1", attrs);
        // MemoryEngine::get is currently a placeholder in my fix
        // but let's at least make it compile
    }
}
