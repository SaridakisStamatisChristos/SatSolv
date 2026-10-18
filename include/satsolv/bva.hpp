#pragma once
#include "satsolv/parser.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace satsolv {

// A bounded, experimental BVA pass. A complete 2 x k clause rectangle
// (a | T_i) & (c | T_i) is replaced by
// (a | x) & (c | x) & (not x | T_i), k >= 3, with fresh x.
// T_i may be one literal (binary case) or a multi-literal disjunction.
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

BvaResult factor_bounded_bva(
    const Formula& input,
    std::uint64_t budget,
    std::uint64_t max_variables,
    const std::function<void(const std::vector<int>&)>& emit,
    const std::function<bool()>& interrupted);

} // namespace satsolv
