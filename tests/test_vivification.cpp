#include "satsolv/solver.hpp"
#include <cassert>
#include <iostream>

static bool model_ok(const satsolv::Formula& f, const std::vector<int>& model) {
    for (const auto& clause : f.clauses) {
        bool ok = false;
        for (int lit : clause) {
            const int v = lit < 0 ? -lit : lit;
            ok = ok || (model[static_cast<std::size_t>(v-1)] == lit);
        }
        if (!ok) return false;
    }
    return true;
}

int main() {
    const satsolv::Formula sat{3, {{1,2,3}, {1,-2}, {1,-3}}};
    const satsolv::Formula unsat{4, {{1}, {-1,2,3,4}, {-2,3}, {-3,4}, {-4}}};
    for (std::uint64_t budget : {0ULL, 1ULL, 16ULL, 64ULL, 16000000ULL}) {
        satsolv::SolverConfig config;
        config.congruence = false;
        config.watched_vivify = true;
        config.vivify_budget = budget;
        satsolv::Solver solver(config);
        for (int repetition = 0; repetition < 3; ++repetition) {
            auto a = solver.solve(sat);
            assert(a.result == satsolv::SolveResult::Sat && model_ok(sat, a.model));
            if (budget == 0) assert(a.stats.vivify_work == 0);
            auto b = solver.solve(unsat);
            assert(b.result == satsolv::SolveResult::Unsat);
        }
    }
    satsolv::SolverConfig config;
    config.congruence = false; config.watched_vivify = true;
    auto a = satsolv::Solver(config).solve(sat);
    assert(a.stats.vivify_removed > 0);
    config.time_limit_seconds = 1e-12;
    assert(satsolv::Solver(config).solve(sat).result == satsolv::SolveResult::Unknown);
    for (std::uint64_t budget : {0ULL,1ULL,64ULL,8000000ULL}) {
        config = {};
        config.congruence = false; config.bounded_eliminate = true;
        config.eliminate_budget = budget;
        satsolv::Solver solver(config);
        for (int repeat = 0; repeat < 3; ++repeat) {
            auto b = solver.solve(sat);
            assert(b.result == satsolv::SolveResult::Sat && model_ok(sat,b.model));
            assert(solver.solve(unsat).result == satsolv::SolveResult::Unsat);
        }
    }
    config = {}; config.bounded_eliminate = true; config.congruence = false;
    auto b = satsolv::Solver(config).solve(sat);
    assert(b.stats.eliminated_variables > 0);
    std::cout << "PASS: watched vivification budgets, root units, repeat solve and interruption\n";
    std::cout << "PASS: elimination budgets, root units and reverse reconstruction after repeated solve\n";
}
