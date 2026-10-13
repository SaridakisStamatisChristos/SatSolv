#pragma once
#include "satsolv/parser.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace satsolv {
struct Elimination {
    int variable = 0;
    std::vector<std::vector<int>> parents;
};
struct SimplifyResult {
    Formula formula;
    std::vector<Elimination> eliminations;
    bool applied = false;
    std::uint64_t removed_literals = 0;
    std::uint64_t subsumed = 0;
    std::uint64_t probed_units = 0;
    std::uint64_t vivified = 0;
};
// The input is immutable. Every strengthening/resolvent is emitted before use.
// Exhausted budgets stop an operation, never turn an incomplete scan into a proof.
SimplifyResult simplify_formula(const Formula&, std::uint64_t budget,
    const std::function<void(const std::vector<int>&)>& emit,
    const std::function<bool()>& interrupted);
void extend_model(std::vector<int>& model, const std::vector<Elimination>&);
} // namespace satsolv
