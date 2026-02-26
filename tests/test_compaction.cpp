#include <catch2/catch_test_macros.hpp>
#include <cynamodb/engine/lsm/manifest.hpp>
#include <cynamodb/engine/lsm/compaction.hpp>
#include <filesystem>

using namespace cynamodb::engine::lsm;

TEST_CASE("Manifest basic operations", "[lsm][manifest]") {
    std::string db_path = "./test_db_manifest";
    std::filesystem::remove_all(db_path);
    std::filesystem::create_directories(db_path);

    {
        Manifest manifest(db_path);
        manifest.set_next_sequence(100);
        
        SSTableMetadata meta;
        meta.path = "table_1.sst";
        meta.level = 0;
        meta.sequence_number = 1;
        meta.min_key = "a";
        meta.max_key = "z";
        
        manifest.add_file(0, meta);
        REQUIRE(manifest.save());
    }

    {
        Manifest manifest(db_path);
        REQUIRE(manifest.load());
        REQUIRE(manifest.get_next_sequence() == 100);
        auto files = manifest.get_level_files(0);
        REQUIRE(files.size() == 1);
        REQUIRE(files[0].path == "table_1.sst");
        REQUIRE(files[0].min_key == "a");
        REQUIRE(files[0].max_key == "z");
    }

    std::filesystem::remove_all(db_path);
}

TEST_CASE("CompactionManager overlapping files", "[lsm][compaction]") {
    std::string db_path = "./test_db_compaction";
    std::filesystem::remove_all(db_path);
    std::filesystem::create_directories(db_path);

    auto manifest = std::make_shared<Manifest>(db_path);
    CompactionManager manager(db_path, manifest);

    SSTableMetadata meta1;
    meta1.path = "1.sst"; meta1.level = 1; meta1.min_key = "100"; meta1.max_key = "200";
    manifest->add_file(1, meta1);

    SSTableMetadata meta2;
    meta2.path = "2.sst"; meta2.level = 1; meta2.min_key = "300"; meta2.max_key = "400";
    manifest->add_file(1, meta2);

    auto overlapping = manager.get_overlapping_files(1, "150", "350");
    REQUIRE(overlapping.size() == 2);

    overlapping = manager.get_overlapping_files(1, "000", "050");
    REQUIRE(overlapping.empty());

    std::filesystem::remove_all(db_path);
}
