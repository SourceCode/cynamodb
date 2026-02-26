#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace cynamodb::streams {

struct SequenceNumberRange {
    std::string starting_sequence_number;
    std::optional<std::string> ending_sequence_number;
};

struct Shard {
    std::string shard_id;
    std::optional<std::string> parent_shard_id;
    SequenceNumberRange sequence_number_range;
};

} // namespace cynamodb::streams
