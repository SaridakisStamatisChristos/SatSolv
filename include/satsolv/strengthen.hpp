#pragma once
#include "satsolv/parser.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace satsolv {
struct StrengthenResult {
    Formula formula;
    bool applied = false;
    std::uint64_t work = 0;
    std::uint64_t ssr_removed = 0;
    std::uint64_t implication_removed = 0;
    std::uint64_t failed_units = 0;
};
// Every replacement is an implication of the current formula, with its RUP
// addition emitted before replacement. No variable elimination or model repair.
StrengthenResult strengthen_short(const Formula& input, bool ssr, bool implications,
    std::uint64_t budget, const std::function<void(const std::vector<int>&)>& emit,
    const std::function<bool()>& interrupted);
}
