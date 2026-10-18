#include "satsolv/bva.hpp"
#include "satsolv/solver.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
bool satisfies(const satsolv::Formula& formula, const std::vector<int>& model) {
    for (const auto& clause : formula.clauses) {
        bool found = false;
        for (int lit : clause)
            if (model[static_cast<std::size_t>((lit < 0 ? -lit : lit) - 1)] == lit) {
                found = true;
                break;
            }
        if (!found) return false;
    }
    return true;
}
std::vector<int> valuation(int n, int mask) {
    std::vector<int> result;
    for (int v = 1; v <= n; ++v)
        result.push_back((mask & (1 << (v - 1))) ? v : -v);
    return result;
}
satsolv::Formula biclique() {
    return satsolv::parse_dimacs_text(
        "p cnf 5 6\n"
        "1 3 0\n2 3 0\n1 4 0\n2 4 0\n1 5 0\n2 5 0\n");
}
}

int main() {
    const auto original = biclique();
    std::vector<std::vector<int>> proof_additions;
    const auto transformed = satsolv::factor_bounded_bva(
        original, 10000, 1, [&](const std::vector<int>& c) { proof_additions.push_back(c); },
        [] { return false; });
    assert(transformed.applied);
    assert(transformed.variables_added == 1);
    assert(transformed.formula.variables == 6);
    assert(transformed.clauses_removed == 6);
    assert(transformed.literals_saved == 2);
    assert(proof_additions.size() == 5);
    const int extension = 6;
    assert(proof_additions[0][0] == extension && proof_additions[1][0] == extension);
    for (std::size_t i = 2; i < proof_additions.size(); ++i)
        assert(proof_additions[i][0] == -extension);

    // Exhaustively establish existential projection on the entire five
    // original-variable truth-table (32 assignments), not just one model.
    for (int mask = 0; mask < (1 << 5); ++mask) {
        const auto values = valuation(5, mask);
        bool witness = false;
        for (int xv : {-6, 6}) {
            auto extended = values;
            extended.push_back(xv);
            witness = witness || satisfies(transformed.formula, extended);
        }
        assert(satisfies(original, values) == witness);
    }

    // A partially exhausted budget or interruption must not emit an addition.
    for (std::uint64_t budget : {0ULL, 1ULL, 5ULL}) {
        std::size_t emitted = 0;
        const auto skipped = satsolv::factor_bounded_bva(
            original, budget, 1,
            [&](const std::vector<int>&) { ++emitted; }, [] { return false; });
        assert(!skipped.applied && emitted == 0);
        assert(skipped.work <= budget);
    }
    {
        std::size_t emitted = 0;
        const auto skipped = satsolv::factor_bounded_bva(
            original, 10000, 1,
            [&](const std::vector<int>&) { ++emitted; }, [] { return true; });
        assert(!skipped.applied && emitted == 0);
    }
    for (const auto& bad : std::vector<satsolv::Formula>{
        {5, {{0}}}, {5, {{6}}}, {5, {{-2147483647-1}}}
    }) {
        bool rejected = false;
        try {
            (void) satsolv::factor_bounded_bva(bad, 10000, 1,
                [](const std::vector<int>&) {}, [] { return false; });
        } catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }

    const satsolv::Formula sat = satsolv::parse_dimacs_text(
        "p cnf 5 8\n"
        "1 3 0\n2 3 0\n1 4 0\n2 4 0\n1 5 0\n2 5 0\n"
        "-1 0\n-2 0\n");
    const satsolv::Formula unsat = satsolv::parse_dimacs_text(
        "p cnf 5 9\n"
        "1 3 0\n2 3 0\n1 4 0\n2 4 0\n1 5 0\n2 5 0\n"
        "-1 0\n-2 0\n-3 0\n");

    satsolv::SolverConfig candidate;
    candidate.experimental_bva = true;
    candidate.bva_max_variables = 1;
    for (const auto& f : {sat, unsat}) {
        const auto control = satsolv::Solver().solve(f);
        const auto changed = satsolv::Solver(candidate).solve(f);
        assert(control.result == changed.result);
        assert(changed.stats.bva_variables_added == 1);
        if (changed.result == satsolv::SolveResult::Sat) {
            assert(changed.model.size() == static_cast<std::size_t>(f.variables));
            assert(satisfies(f, changed.model));
        }
    }
    // No transformation in zero-budget candidate: same behavior and no RAT.
    candidate.bva_budget = 0;
    const auto disabled = satsolv::Solver(candidate).solve(sat);
    assert(disabled.stats.bva_variables_added == 0);
    assert(disabled.result == satsolv::SolveResult::Sat);


    // Multi-literal rectangles: identical suffixes across two centers.
    // This is a stronger class than the first binary-only prototype.
    const satsolv::Formula long_tails = satsolv::parse_dimacs_text(
        "p cnf 6 6\n"
        "1 3 4 0\n2 3 4 0\n"
        "1 3 5 0\n2 3 5 0\n"
        "1 4 6 0\n2 4 6 0\n");
    std::vector<std::vector<int>> long_proof;
    const auto rectangle = satsolv::factor_bounded_bva(
        long_tails, 100000, 1,
        [&](const std::vector<int>& clause) { long_proof.push_back(clause); },
        [] { return false; });
    assert(rectangle.applied && rectangle.variables_added == 1);
    assert(rectangle.formula.variables == 7);
    assert(rectangle.clauses_removed == 6 && rectangle.literals_saved == 5);
    assert(long_proof.size() == 5);
    assert(long_proof[0][0] == 7 && long_proof[1][0] == 7);
    for (std::size_t i = 2; i < long_proof.size(); ++i) {
        assert(long_proof[i][0] == -7 && long_proof[i].size() == 3);
    }
    for (int mask = 0; mask < (1 << 6); ++mask) {
        const auto values = valuation(6, mask);
        bool witness = false;
        for (int xv : {-7, 7}) {
            auto extended = values;
            extended.push_back(xv);
            witness = witness || satisfies(rectangle.formula, extended);
        }
        assert(satisfies(long_tails, values) == witness);
    }
    const auto long_solve = satsolv::Solver(candidate).solve(long_tails);
    assert(long_solve.result == satsolv::SolveResult::Sat);
    assert(long_solve.model.size() == 6 && satisfies(long_tails, long_solve.model));

    std::cout << "PASS: generalized BVA existential equivalence, RAT order, "
                 "budgets, interruption, model projection and UNSAT\n";
}
