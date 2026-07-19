#include <cynamodb/core/memory.hpp>
#include <cynamodb/engine/lsm/lsm_engine.hpp>
#include <cynamodb/engine/lsm/sstable.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace {

using cynamodb::core::AttributeType;
using cynamodb::core::AttributeValue;
using cynamodb::core::String;
using cynamodb::engine::StorageEngine;
using cynamodb::engine::lsm::LsmEngine;
using cynamodb::engine::lsm::SSTable;
using cynamodb::engine::lsm::SSTableWriter;
using cynamodb::engine::lsm::Skiplist;

constexpr size_t kIndexEntries = 500000;
constexpr size_t kStressEntries = 40000;
constexpr size_t kKiB = 1024;

struct TempDir {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("cynamodb_memory_regression_" + std::to_string(static_cast<unsigned long long>(::getpid())));

    TempDir() { std::filesystem::remove_all(path); std::filesystem::create_directories(path); }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

std::shared_ptr<AttributeValue> string_value(std::string_view value) {
    auto attribute = std::make_shared<AttributeValue>();
    attribute->type = AttributeType::S;
    attribute->value = String(value.data(), value.size());
    return attribute;
}

StorageEngine::AttributeMap item(std::string_view key, std::string_view payload) {
    StorageEngine::AttributeMap value;
    value["pk"] = string_value(key);
    value["payload"] = string_value(payload);
    return value;
}

std::string key_for(size_t value) {
    std::ostringstream out;
    out << 'k' << std::setw(12) << std::setfill('0') << value;
    return out.str();
}

size_t anonymous_rss_kib() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (!line.starts_with("RssAnon:")) continue;
        std::istringstream fields(line);
        std::string label;
        size_t value = 0;
        std::string unit;
        if (fields >> label >> value >> unit && value > 0) return value;
        break;
    }
    throw std::runtime_error("unable to read non-zero RssAnon from /proc/self/status");
}

void release_free_heap() {
#ifdef __GLIBC__
    malloc_trim(0);
#endif
}

bool verify_payload(const std::optional<StorageEngine::AttributeMap>& found,
                    std::string_view expected) {
    if (!found) return false;
    const auto it = found->find("payload");
    return it != found->end() && it->second &&
           std::get<String>(it->second->value) == expected;
}

bool mapped_index_regression(const std::filesystem::path& directory) {
    const std::string path = (directory / "large-index.sst").string();
    {
        SSTableWriter writer(path);
        Skiplist::SnapshotEntry entry;
        entry.attributes["payload"] = string_value(std::string(64, 'x'));
        for (size_t i = 0; i < kIndexEntries; ++i) {
            const std::string key = key_for(i) + std::string(96, 'k');
            if (!writer.append(key, entry)) {
                std::cerr << "memory regression: failed to write index entry " << i << '\n';
                return false;
            }
        }
        if (!writer.finish()) {
            std::cerr << "memory regression: failed to finalize large SSTable\n";
            return false;
        }
    }

    release_free_heap();
    const size_t before = anonymous_rss_kib();
    SSTable table(path);
    const size_t after = anonymous_rss_kib();
    const size_t delta = after > before ? after - before : 0;

    // The compact offset vector needs 8 bytes per entry (about 3.9 MiB here). Leave
    // headroom for allocator granularity, but reject the old heap-string index, which
    // consumed well over 64 MiB for this key set.
    constexpr size_t kMaxIndexAnonDeltaKiB = 16 * kKiB;
    if (table.entry_count() != kIndexEntries || delta > kMaxIndexAnonDeltaKiB) {
        std::cerr << "memory regression: mapped index anonymous RSS delta was " << delta
                  << " KiB for " << table.entry_count() << " entries (limit "
                  << kMaxIndexAnonDeltaKiB << " KiB)\n";
        return false;
    }

    for (size_t position : {size_t{0}, kIndexEntries / 2, kIndexEntries - 1}) {
        const std::string key = key_for(position) + std::string(96, 'k');
        auto found = table.get(key);
        if (!verify_payload(found, std::string(64, 'x'))) {
            std::cerr << "memory regression: mapped lookup failed at " << position << '\n';
            return false;
        }
    }
    std::cout << "mapped_index entries=" << table.entry_count()
              << " anon_delta_kib=" << delta << '\n';
    return true;
}

bool streaming_compaction_regression(const std::filesystem::path& directory) {
    setenv("CYNAMODB_WAL_FSYNC", "0", 1);
    setenv("CYNAMODB_ENABLE_AUTO_COMPACTION", "1", 1);
    release_free_heap();
    const size_t baseline = anonymous_rss_kib();
    std::atomic<bool> monitor{true};
    std::atomic<size_t> peak{baseline};
    std::thread observer([&] {
        while (monitor.load(std::memory_order_relaxed)) {
            const size_t current = anonymous_rss_kib();
            size_t observed = peak.load(std::memory_order_relaxed);
            while (current > observed &&
                   !peak.compare_exchange_weak(observed, current, std::memory_order_relaxed)) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    bool correct = true;
    {
        auto arena = std::make_shared<cynamodb::core::Arena>();
        LsmEngine engine(directory.string(), arena);
        const std::string payload(1024, 'p');
        for (size_t i = 0; i < kStressEntries; ++i) {
            const std::string key = key_for(i);
            engine.put("Stress", key, item(key, payload));
        }

        // Overwrites and tombstones force newest-wins resolution across compaction
        // batches. Delete count exceeds one memtable to exercise delete rotation.
        const std::string updated(1024, 'u');
        for (size_t i = 0; i < kStressEntries; i += 4) {
            const std::string key = key_for(i);
            engine.put("Stress", key, item(key, updated));
        }
        for (size_t i = 0; i < kStressEntries; i += 5) {
            engine.remove("Stress", key_for(i));
        }

        const auto compaction_deadline = std::chrono::steady_clock::now() +
                                         std::chrono::seconds(30);
        bool compacted_output_seen = false;
        while (!compacted_output_seen && std::chrono::steady_clock::now() < compaction_deadline) {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
                if (entry.path().filename().string().ends_with("_c.sst")) {
                    compacted_output_seen = true;
                    break;
                }
            }
            if (!compacted_output_seen) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!compacted_output_seen) {
            std::cerr << "memory regression: no compacted SSTable was produced\n";
            correct = false;
        }

        for (size_t i : {size_t{1}, size_t{4}, kStressEntries / 2 + 1, kStressEntries - 1}) {
            const bool deleted = i % 5 == 0;
            const bool updated_item = i % 4 == 0;
            auto found = engine.get("Stress", key_for(i));
            if (deleted ? found.has_value()
                        : !verify_payload(found, updated_item ? updated : payload)) {
                std::cerr << "memory regression: point lookup mismatch at " << i << '\n';
                correct = false;
                break;
            }
        }

        size_t total = 0;
        std::optional<std::string> cursor;
        do {
            auto page = engine.scan("Stress", cursor, 257);
            if (page.items.size() > 257) {
                correct = false;
                break;
            }
            total += page.items.size();
            cursor = page.last_evaluated_key;
        } while (cursor);
        const size_t expected = kStressEntries - (kStressEntries + 4) / 5;
        if (total != expected) {
            std::cerr << "memory regression: paginated scan returned " << total
                      << " records, expected " << expected << '\n';
            correct = false;
        }
    }

    monitor.store(false, std::memory_order_relaxed);
    observer.join();
    const size_t peak_delta = peak.load(std::memory_order_relaxed) > baseline
        ? peak.load(std::memory_order_relaxed) - baseline : 0;
    // The old all-database map retained more than 39 MiB of payload bytes alone for
    // this workload. A 32 MiB ceiling cannot pass unless compaction remains streaming.
    constexpr size_t kMaxCompactionAnonDeltaKiB = 32 * kKiB;
    if (peak_delta > kMaxCompactionAnonDeltaKiB) {
        std::cerr << "memory regression: streaming workload anonymous RSS grew by "
                  << peak_delta << " KiB (limit " << kMaxCompactionAnonDeltaKiB << " KiB)\n";
        correct = false;
    }
    std::cout << "streaming_compaction entries=" << kStressEntries
              << " peak_anon_delta_kib=" << peak_delta << '\n';
    return correct;
}

} // namespace

int main() {
    TempDir temp;
    if (!mapped_index_regression(temp.path)) return 1;
    std::filesystem::remove_all(temp.path);
    std::filesystem::create_directories(temp.path);
    if (!streaming_compaction_regression(temp.path)) return 1;
    return 0;
}
