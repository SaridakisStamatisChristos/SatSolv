#include "satsolv/strengthen.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

namespace {
int variable(int lit) { return lit < 0 ? -lit : lit; }
bool satisfied(const satsolv::Formula& f, unsigned bits) {
    for (const auto& c : f.clauses) {
        bool found = false;
        for (int lit : c) if (((bits >> (variable(lit) - 1)) & 1U) == static_cast<unsigned>(lit > 0)) found = true;
        if (!found) return false;
    }
    return true;
}
// Independent whole-formula unit propagation under the negated addition.
bool rup(const satsolv::Formula& f, const std::vector<int>& addition) {
    std::vector<int> values(static_cast<std::size_t>(f.variables + 1), -1);
    for (int lit : addition) {
        auto& value = values[static_cast<std::size_t>(variable(lit))];
        const int assumption = lit < 0 ? 1 : 0;
        if (value >= 0 && value != assumption) return true;
        value = assumption;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto c : f.clauses) {
            std::sort(c.begin(), c.end()); c.erase(std::unique(c.begin(), c.end()), c.end());
            bool sat = false;
            int unit = 0, unknown = 0;
            for (int lit : c) {
                const int value = values[static_cast<std::size_t>(variable(lit))];
                if (value < 0) { ++unknown; unit = lit; }
                else if (value == static_cast<int>(lit > 0)) { sat = true; break; }
            }
            if (sat) continue;
            if (unknown == 0) return true;
            if (unknown == 1) { values[static_cast<std::size_t>(variable(unit))] = unit > 0 ? 1 : 0; changed = true; }
        }
    }
    return false;
}
satsolv::StrengthenResult check(const satsolv::Formula& original, bool ssr, bool graph, std::uint64_t budget, bool stop = false) {
    auto proof_formula = original;
    const auto r = satsolv::strengthen_short(original, ssr, graph, budget,
        [&](const auto& c) { assert(rup(proof_formula, c)); proof_formula.clauses.push_back(c); },
        [stop]() { return stop; });
    assert(r.work <= budget);
    const auto& transformed = r.applied ? r.formula : original;
    assert(transformed.variables == original.variables);
    for (unsigned bits = 0; bits < (1U << static_cast<unsigned>(original.variables)); ++bits)
        assert(satisfied(original, bits) == satisfied(transformed, bits));
    return r;
}
}
int main() {
    using satsolv::Formula;
    assert(check(Formula{3, {{1, 2, 3}, {-1, 2}}}, true, false, 10000).ssr_removed > 0);
    assert(check(Formula{4, {{-1, 4}, {-4, 2}, {1, 2, 3}}}, false, true, 10000).implication_removed > 0);
    assert(check(Formula{3, {{-1, 2}, {-1, -2}, {1, 3}}}, false, true, 10000).failed_units > 0);
    assert(!check(Formula{2, {{1, 2}}}, true, true, 0).applied);
    assert(!check(Formula{2, {{1, 2}, {-1, 2}}}, true, true, 10000, true).applied);
    std::mt19937 rng(0x51A6U);
    for (unsigned trial = 0; trial < 500; ++trial) {
        Formula f; f.variables = 3 + static_cast<int>(rng() % 6U);
        for (unsigned j = 0, count = static_cast<unsigned>(rng() % 50U); j < count; ++j) {
            std::vector<int> c;
            const unsigned width = 2 + static_cast<unsigned>(rng() % 5U);
            for (unsigned k = 0; k < width; ++k) {
                const int v = 1 + static_cast<int>(rng() % static_cast<unsigned>(f.variables));
                c.push_back((rng() & 1U) != 0 ? v : -v);
            }
            f.clauses.push_back(std::move(c));
        }
        for (unsigned arm = 1; arm < 4; ++arm) for (std::uint64_t budget : {1ULL, 31ULL, 1023ULL, 10000ULL})
            check(f, (arm & 1U) != 0, (arm & 2U) != 0, budget);
    }
    std::cout << "PASS: 6000 bounded random transformations exhaustively preserve every assignment; every emitted addition independently RUP checked\n";
}
