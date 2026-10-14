#pragma once
#include "satsolv/parser.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace satsolv {
struct CongruenceResult {
    Formula formula;
    // Original positive variable -> signed representative. Model restoration
    // occurs after any later variable-elimination reconstruction.
    std::vector<int> representatives;
    bool applied = false;
    std::uint64_t equivalences = 0;
    std::uint64_t and_gates = 0;
    std::uint64_t work = 0;
    std::uint64_t rounds = 0;
};
CongruenceResult simplify_congruence(const Formula&, std::uint64_t budget,
    const std::function<void(const std::vector<int>&)>& emit,
    const std::function<bool()>& interrupted);
void restore_equivalent_model(std::vector<int>&, const std::vector<int>&);
} // namespace satsolv
