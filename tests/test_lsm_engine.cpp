#include <catch2/catch_test_macros.hpp>
#include <cynamodb/engine/lsm/lsm_engine.hpp>
#include <cynamodb/core/memory.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

using namespace cynamodb::engine;
using namespace cynamodb::engine::lsm;
using namespace cynamodb::core;

namespace {

std::shared_ptr<AttributeValue> S(std::string_view s) {
    auto av = std::make_shared<AttributeValue>();
    av->type = AttributeType::S;
    av->value = String(s.data(), s.size());
    return av;
}

StorageEngine::AttributeMap make_item(std::string_view group, std::string_view payload) {
    StorageEngine::AttributeMap m;
    m["grp"] = S(group);
    m["data"] = S(payload);
    return m;
}

std::string get_s(const StorageEngine::AttributeMap& item, const std::string& attr) {
    auto it = item.find(attr);
    REQUIRE(it != item.end());
    REQUIRE(it->second);
    return std::string(std::get<String>(it->second->value));
}

// A unique temp directory per test that is removed on destruction.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        static std::atomic<uint64_t> counter{0};
        path = std::filesystem::temp_directory_path() /
               ("cynamodb_lsm_" + std::to_string(counter.fetch_add(1)) + "_" +
                std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::remove_all(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

size_t count_sstables(const std::filesystem::path& dir) {
    size_t n = 0;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.path().extension() == ".sst") ++n;
    }
    return n;
}

}  // namespace

TEST_CASE("LsmEngine memtable put/get/remove", "[lsm][engine]") {
    TempDir dir;
    auto arena = std::make_shared<Arena>();
    LsmEngine engine(dir.path.string(), arena);
    const std::string table = "T";

    engine.put(table, "k1", make_item("g", "one"));
    engine.put(table, "k2", make_item("g", "two"));

    auto v1 = engine.get(table, "k1");
    REQUIRE(v1.has_value());
    REQUIRE(get_s(*v1, "data") == "one");

    REQUIRE(!engine.get(table, "missing").has_value());

    SECTION("remove tombstones the key") {
        engine.remove(table, "k1");
        REQUIRE(!engine.get(table, "k1").has_value());
        REQUIRE(engine.get(table, "k2").has_value());
    }

    SECTION("overwrite wins") {
        engine.put(table, "k1", make_item("g", "updated"));
        REQUIRE(get_s(*engine.get(table, "k1"), "data") == "updated");
    }
}

TEST_CASE("LsmEngine scan returns sorted live items and excludes tombstones", "[lsm][engine][scan]") {
    TempDir dir;
    auto arena = std::make_shared<Arena>();
    LsmEngine engine(dir.path.string(), arena);
    const std::string table = "T";

    for (auto k : {"c", "a", "d", "b"}) {
        engine.put(table, k, make_item("g", k));
    }
    engine.remove(table, "b");

    auto result = engine.scan(table, std::nullopt, 0);
    REQUIRE(result.items.size() == 3);
    std::vector<std::string> order;
    for (auto& it : result.items) order.push_back(get_s(it, "data"));
    REQUIRE(order == std::vector<std::string>{"a", "c", "d"});

    SECTION("pagination covers all items exactly once") {
        std::vector<std::string> all;
        std::optional<std::string> cursor;
        do {
            auto page = engine.scan(table, cursor, 1);
            for (auto& it : page.items) all.push_back(get_s(it, "data"));
            cursor = page.last_evaluated_key;
        } while (cursor.has_value());
        REQUIRE(all == std::vector<std::string>{"a", "c", "d"});
    }
}

TEST_CASE("LsmEngine query filters by equality", "[lsm][engine][query]") {
    TempDir dir;
    auto arena = std::make_shared<Arena>();
    LsmEngine engine(dir.path.string(), arena);
    const std::string table = "T";

    engine.put(table, "1", make_item("x", "a"));
    engine.put(table, "2", make_item("y", "b"));
    engine.put(table, "3", make_item("x", "c"));

    StorageEngine::AttributeMap cond;
    cond["grp"] = S("x");
    auto result = engine.query(table, cond, std::nullopt, 0);
    REQUIRE(result.items.size() == 2);
    REQUIRE(get_s(result.items[0], "data") == "a");
    REQUIRE(get_s(result.items[1], "data") == "c");
}

TEST_CASE("LsmEngine isolates tables sharing the same key", "[lsm][engine][multitable]") {
    TempDir dir;
    auto arena = std::make_shared<Arena>();
    LsmEngine engine(dir.path.string(), arena);

    // Same logical key "k" in two different tables must not collide.
    engine.put("TableA", "k", make_item("g", "from-a"));
    engine.put("TableB", "k", make_item("g", "from-b"));

    REQUIRE(get_s(*engine.get("TableA", "k"), "data") == "from-a");
    REQUIRE(get_s(*engine.get("TableB", "k"), "data") == "from-b");

    // A scan of one table never returns the other table's items.
    auto a = engine.scan("TableA", std::nullopt, 0);
    REQUIRE(a.items.size() == 1);
    REQUIRE(get_s(a.items[0], "data") == "from-a");

    // Deleting from one table leaves the other intact.
    engine.remove("TableA", "k");
    REQUIRE(!engine.get("TableA", "k").has_value());
    REQUIRE(engine.get("TableB", "k").has_value());
    REQUIRE(engine.scan("TableB", std::nullopt, 0).items.size() == 1);
}

TEST_CASE("LsmEngine persists unflushed writes across a restart (WAL replay)", "[lsm][engine][persistence]") {
    TempDir dir;

    // First session: write a few items (well below the flush threshold, so they
    // live only in the memtable + WAL), update one, and delete one. Then close.
    {
        auto arena = std::make_shared<Arena>();
        LsmEngine engine(dir.path.string(), arena);
        engine.put("T", "k1", make_item("g", "one"));
        engine.put("T", "k2", make_item("g", "two"));
        engine.put("T", "k3", make_item("g", "three"));
        engine.put("T", "k2", make_item("g", "two-updated"));  // overwrite
        engine.remove("T", "k3");                              // tombstone
    }

    // Second session: a brand-new engine over the same directory must recover the
    // exact logical state from the WAL — including the overwrite and the delete.
    {
        auto arena = std::make_shared<Arena>();
        LsmEngine engine(dir.path.string(), arena);

        auto k1 = engine.get("T", "k1");
        REQUIRE(k1.has_value());
        REQUIRE(get_s(*k1, "data") == "one");

        auto k2 = engine.get("T", "k2");
        REQUIRE(k2.has_value());
        REQUIRE(get_s(*k2, "data") == "two-updated");

        REQUIRE(!engine.get("T", "k3").has_value());  // deletion survived

        auto scan = engine.scan("T", std::nullopt, 0);
        REQUIRE(scan.items.size() == 2);
    }
}

TEST_CASE("LsmEngine persists flushed SSTables and the unflushed tail across a restart", "[lsm][engine][persistence][flush]") {
    TempDir dir;
    const int kCount = 1200;  // exceeds the 1000-entry flush threshold
    auto key = [](int i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "key%05d", i);
        return std::string(buf);
    };

    {
        auto arena = std::make_shared<Arena>();
        LsmEngine engine(dir.path.string(), arena);
        for (int i = 0; i < kCount; ++i) {
            engine.put("T", key(i), make_item("g", std::to_string(i)));
        }
        // Destructor flushes the frozen memtable to an SSTable; the tail stays in
        // the active memtable's WAL.
    }

    {
        auto arena = std::make_shared<Arena>();
        LsmEngine engine(dir.path.string(), arena);

        // An early key recovered from the on-disk SSTable.
        auto first = engine.get("T", key(0));
        REQUIRE(first.has_value());
        REQUIRE(get_s(*first, "data") == "0");

        // A late key recovered from the WAL-replayed memtable.
        auto last = engine.get("T", key(kCount - 1));
        REQUIRE(last.has_value());
        REQUIRE(get_s(*last, "data") == std::to_string(kCount - 1));

        // Everything is present exactly once.
        auto scan = engine.scan("T", std::nullopt, 0);
        REQUIRE(scan.items.size() == static_cast<size_t>(kCount));
    }
}

TEST_CASE("LsmEngine stays correct under heavy load across many flushes", "[lsm][engine][stress]") {
    TempDir dir;
    auto arena = std::make_shared<Arena>();
    LsmEngine engine(dir.path.string(), arena);
    const std::string T = "T";
    const int N = 5000;  // > 4 flush thresholds, so L0 fills and compaction triggers

    auto key = [](int i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "k%06d", i);
        return std::string(buf);
    };

    // Insert N, then update evens, then delete every 5th. Updates and deletes hit
    // keys that already migrated to on-disk SSTables, exercising cross-level reads.
    for (int i = 0; i < N; ++i) engine.put(T, key(i), make_item("g", std::to_string(i)));
    for (int i = 0; i < N; i += 2) engine.put(T, key(i), make_item("g", "u" + std::to_string(i)));
    for (int i = 0; i < N; i += 5) engine.remove(T, key(i));

    // Every key must reflect the last operation applied to it.
    for (int i = 0; i < N; ++i) {
        auto v = engine.get(T, key(i));
        if (i % 5 == 0) {
            REQUIRE(!v.has_value());  // deleted (delete is the last op for these)
        } else if (i % 2 == 0) {
            REQUIRE(v.has_value());
            REQUIRE(get_s(*v, "data") == "u" + std::to_string(i));  // updated
        } else {
            REQUIRE(v.has_value());
            REQUIRE(get_s(*v, "data") == std::to_string(i));  // original
        }
    }

    // A full scan must return exactly the live set, sorted, with no duplicates.
    const int expected_live = N - (N / 5);
    auto result = engine.scan(T, std::nullopt, 0);
    REQUIRE(result.items.size() == static_cast<size_t>(expected_live));

    // SSTables exist on disk (flushing happened); background compaction may have
    // merged some away, so we only require at least one.
    size_t sst = 0;
    for (auto& e : std::filesystem::directory_iterator(dir.path)) {
        if (e.path().extension() == ".sst") ++sst;
    }
    REQUIRE(sst >= 1);
}

TEST_CASE("LsmEngine compaction bounds file count and preserves correctness", "[lsm][engine][compaction]") {
    TempDir dir;
    const std::string T = "T";
    const int N = 8000;  // 8 flushes; without compaction that is 8+ SSTables

    auto key = [](int i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "k%06d", i);
        return std::string(buf);
    };
    auto sst_count = [&] {
        size_t n = 0;
        for (auto& e : std::filesystem::directory_iterator(dir.path)) {
            if (e.path().extension() == ".sst") ++n;
        }
        return n;
    };
    auto wait_settled = [&] {
        // Poll until the SSTable count stops changing (compaction quiescent).
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        size_t last = sst_count();
        int stable = 0;
        while (std::chrono::steady_clock::now() < deadline && stable < 5) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            size_t now = sst_count();
            stable = (now == last) ? stable + 1 : 0;
            last = now;
        }
        return last;
    };

    {
        auto arena = std::make_shared<Arena>();
        LsmEngine engine(dir.path.string(), arena);

        for (int i = 0; i < N; ++i) engine.put(T, key(i), make_item("g", std::to_string(i)));
        // Delete a contiguous range; their tombstones must be honored even after
        // the covered keys are compacted away.
        for (int i = 1000; i < 3000; ++i) engine.remove(T, key(i));

        size_t settled = wait_settled();
        // Compaction must keep the file count well below the number of flushes (8+).
        REQUIRE(settled <= 4);

        // Every live key reads correctly; every deleted key is absent.
        for (int i = 0; i < N; i += 3) {
            auto v = engine.get(T, key(i));
            if (i >= 1000 && i < 3000) {
                REQUIRE(!v.has_value());
            } else {
                REQUIRE(v.has_value());
                REQUIRE(get_s(*v, "data") == std::to_string(i));
            }
        }
        REQUIRE(engine.scan(T, std::nullopt, 0).items.size() == static_cast<size_t>(N - 2000));
    }

    // The merged data must also survive a restart (durable through compaction).
    {
        auto arena = std::make_shared<Arena>();
        LsmEngine engine(dir.path.string(), arena);
        REQUIRE(engine.scan(T, std::nullopt, 0).items.size() == static_cast<size_t>(N - 2000));
        REQUIRE(!engine.get(T, key(1500)).has_value());
        REQUIRE(get_s(*engine.get(T, key(0)), "data") == "0");
    }
}

TEST_CASE("LsmEngine reads correctly across a flushed SSTable", "[lsm][engine][flush]") {
    TempDir dir;
    auto arena = std::make_shared<Arena>();
    LsmEngine engine(dir.path.string(), arena);
    const std::string table = "T";

    // The memtable flush threshold is 1000 entries; insert enough to force a
    // flush of older keys to an on-disk SSTable while newer keys stay in memory.
    const int kCount = 1200;
    auto key = [](int i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "key%05d", i);
        return std::string(buf);
    };
    for (int i = 0; i < kCount; ++i) {
        engine.put(table, key(i), make_item("g", std::to_string(i)));
    }

    // Wait (bounded) for the background flush thread to produce an SSTable.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (count_sstables(dir.path) == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(count_sstables(dir.path) >= 1);

    // An early key now lives in the SSTable; it must still be readable.
    auto early = engine.get(table, key(0));
    REQUIRE(early.has_value());
    REQUIRE(get_s(*early, "data") == "0");

    // A late key is still in the live memtable.
    REQUIRE(engine.get(table, key(kCount - 1)).has_value());

    // Deleting an SSTable-resident key must shadow the on-disk value: the
    // memtable tombstone is authoritative and get() must not return stale data.
    engine.remove(table, key(0));
    REQUIRE(!engine.get(table, key(0)).has_value());

    // A full scan must see every live key and exclude the deleted one.
    auto result = engine.scan(table, std::nullopt, 0);
    REQUIRE(result.items.size() == static_cast<size_t>(kCount - 1));
}
