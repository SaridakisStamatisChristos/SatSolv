#include "satsolv/parser.hpp"
#include "satsolv/solver.hpp"

#include <cassert>
#include <iostream>

namespace {

bool model_satisfies(const satsolv::Formula& f, const std::vector<int>& model) {
    std::vector<int> value(static_cast<std::size_t>(f.variables + 1), -1);
    for (int lit : model) value[static_cast<std::size_t>(lit < 0 ? -lit : lit)] = lit > 0 ? 1 : 0;
    for (const auto& clause : f.clauses) {
        bool sat = false;
        for (int lit : clause) {
            const int v = lit < 0 ? -lit : lit;
            if (value[static_cast<std::size_t>(v)] < 0) continue;
            if ((lit > 0 && value[static_cast<std::size_t>(v)] == 1) ||
                (lit < 0 && value[static_cast<std::size_t>(v)] == 0)) sat = true;
        }
        if (!sat) return false;
    }
    return true;
}

satsolv::SolveResult solve_text(const std::string& text) {
    auto f = satsolv::parse_dimacs_text(text);
    satsolv::Solver solver;
    return solver.solve(f).result;
}

} // namespace

int main() {
    assert(solve_text("p cnf 0 0\n") == satsolv::SolveResult::Sat);
    assert(solve_text("p cnf 1 1\n0\n") == satsolv::SolveResult::Unsat);
    assert(solve_text("p cnf 1 1\n1 0\n") == satsolv::SolveResult::Sat);
    assert(solve_text("p cnf 1 2\n1 0\n-1 0\n") == satsolv::SolveResult::Unsat);
    assert(solve_text("p cnf 3 3\n-1 2 0\n-2 3 0\n1 0\n") == satsolv::SolveResult::Sat);
    assert(solve_text("p cnf 2 4\n1 2 0\n1 -2 0\n-1 2 0\n-1 -2 0\n") == satsolv::SolveResult::Unsat);

    auto f = satsolv::parse_dimacs_text("c split clause\np cnf 3 2\n1 -2\n3 0\n-1 0\n");
    satsolv::Solver solver;
    auto out = solver.solve(f);
    assert(out.result == satsolv::SolveResult::Sat);
    assert(model_satisfies(f, out.model));

    bool threw = false;
    try { (void)satsolv::parse_dimacs_text("p cnf 1 1\n2 0\n"); }
    catch (const satsolv::ParseError&) { threw = true; }
    assert(threw);

    threw = false;
    try { (void)satsolv::parse_dimacs_text("p cnf 1 1\n1\n"); }
    catch (const satsolv::ParseError&) { threw = true; }
    assert(threw);

    std::cout << "unit tests passed\n";
    return 0;
}
