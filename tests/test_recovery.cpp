#include <catch2/catch_test_macros.hpp>
#include <cynamodb/engine/lsm/wal.hpp>
#include <filesystem>

using namespace cynamodb::engine::lsm;

TEST_CASE("WAL Header and Replay", "[durability]") {
    std::string wal_path = "./test_durability.log";
    std::filesystem::remove(wal_path);

    {
        WriteAheadLog wal(wal_path);
        REQUIRE(wal.append(1, "k1", "v1"));
        REQUIRE(wal.append(2, "k2", "v2"));
        wal.sync();
    }

    {
        WriteAheadLog wal(wal_path);
        auto records = wal.replay();
        REQUIRE(records.size() == 2);
        REQUIRE(records[0].seq == 1);
        REQUIRE(records[0].key == "k1");
        REQUIRE(records[1].key == "k2");
    }

    std::filesystem::remove(wal_path);
}

TEST_CASE("WAL Checksum detection", "[durability]") {
    std::string wal_path = "./test_checksum.log";
    std::filesystem::remove(wal_path);

    {
        WriteAheadLog wal(wal_path);
        wal.append(1, "k1", "v1");
        wal.sync();
    }

    // Corrupt the file
    std::fstream file(wal_path, std::ios::in | std::ios::out | std::ios::binary);
    file.seekg(20, std::ios::beg); // Skip header and some record data
    file.put('X');
    file.close();

    {
        WriteAheadLog wal(wal_path);
        auto records = wal.replay();
        // Should stop at corruption
        REQUIRE(records.empty());
    }

    std::filesystem::remove(wal_path);
}
