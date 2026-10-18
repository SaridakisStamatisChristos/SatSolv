#pragma once
#include "satsolv/parser.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace satsolv {

// A bounded, experimental binary-clause BVA pass. A complete 2 x k biclique
// (a | b_i) & (c | b_i) is replaced by
// (a | x) & (c | x) & (not x | b_i), k >= 3, with fresh x.
// The existential projection onto original variables is equivalent.
// emit() receives DRAT RAT additions in pivot-first order, BEFORE use.
// The caller must project SAT models back to the original variable count.
struct BvaResult {
    Formula formula;
    bool applied = false;
    std::uint64_t work = 0;
    std::uint64_t variables_added = 0;
    std::uint64_t clauses_removed = 0;
    std::uint64_t literals_saved = 0;
};

BvaResult factor_binary_bva(
    const Formula& input,
    std::uint64_t budget,
    std::uint64_t max_variables,
    const std::function<void(const std::vector<int>&)>& emit,
    const std::function<bool()>& interrupted);

} // namespace satsolv
