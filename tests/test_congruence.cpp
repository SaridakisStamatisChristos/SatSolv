#include "satsolv/solver.hpp"
#include <cassert>
#include <iostream>

bool model_satisfies(const satsolv::Formula& f,const std::vector<int>& model) {
    for (const auto& c:f.clauses) {
        bool sat=false;
        for (int lit:c) if (model[static_cast<std::size_t>((lit<0 ? -lit:lit)-1)]==lit) sat=true;
        if (!sat) return false;
    }
    return true;
}

int main() {
    const auto sat=satsolv::parse_dimacs_text("p cnf 6 7\n-3 1 0\n-3 -2 0\n3 -1 2 0\n4 1 0\n4 -2 0\n-4 -1 2 0\n3 5 6 0\n");
    for (std::uint64_t budget:{0ULL,1ULL,16ULL,64ULL,1000ULL,4000000ULL}) {
        satsolv::SolverConfig config;config.congruence_budget=budget;
        const auto out=satsolv::Solver(config).solve(sat);
        assert(out.result==satsolv::SolveResult::Sat && model_satisfies(sat,out.model));
        assert(out.stats.congruence_work<=budget);
    }
    const auto contradiction=satsolv::parse_dimacs_text("p cnf 3 6\n-3 1 0\n-3 2 0\n3 -1 -2 0\n3 1 0\n3 2 0\n-3 -1 -2 0\n");
    for (std::uint64_t budget:{0ULL,1ULL,64ULL,4000000ULL}) {
        satsolv::SolverConfig config;config.congruence_budget=budget;
        assert(satsolv::Solver(config).solve(contradiction).result==satsolv::SolveResult::Unsat);
    }
    // Cancellation must fall back without a partial equivalence/model mapping.
    const auto interrupted=satsolv::simplify_congruence(sat,4000000,[](const std::vector<int>&){assert(false);},[](){return true;});
    assert(!interrupted.applied && interrupted.representatives.empty());
    const auto empty=satsolv::Solver().solve(satsolv::parse_dimacs_text("p cnf 0 0\n"));
    assert(empty.result==satsolv::SolveResult::Sat);
    std::cout<<"PASS: congruence budgets, complementary models, interruption and empty graph\n";
}
