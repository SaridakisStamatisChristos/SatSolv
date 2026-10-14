#include "satsolv/indexed_heap.hpp"
#include "satsolv/solver.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <set>

namespace {
void heap_properties() {
    std::vector<double> scores(129, 0.0);
    std::vector<std::uint64_t> ranks(129);
    for (std::size_t v = 0; v < ranks.size(); ++v) ranks[v] = v;
    satsolv::IndexedHeap heap; heap.reset(scores, ranks);
    std::set<int> present; std::mt19937 rng(7841);
    auto better = [&](int a, int b) { return scores[static_cast<std::size_t>(a)] != scores[static_cast<std::size_t>(b)] ? scores[static_cast<std::size_t>(a)] > scores[static_cast<std::size_t>(b)] : a < b; };
    for (int step = 0; step < 50000; ++step) {
        if (step % 211 == 0) {
            for (auto& s : scores) s *= 0.5;
            heap.rebuild();
        }
        if (!present.empty() && rng()%4U == 0) {
            const int expected = *std::min_element(present.begin(), present.end(), better);
            assert(heap.pop() == expected); present.erase(expected);
        } else {
            const int v = static_cast<int>(rng()%128U)+1;
            scores[static_cast<std::size_t>(v)] = static_cast<double>(rng()%101U);
            heap.update(v); present.insert(v);
        }
        assert(heap.size() == present.size()); assert(heap.size() <= 128);
    }
}
bool satisfies(const satsolv::Formula& f, const std::vector<int>& m) {
    for (const auto& c : f.clauses) {
        bool sat = false;
        for (int lit : c) if (m[static_cast<std::size_t>((lit < 0 ? -lit : lit)-1)] == lit) sat = true;
        if (!sat) return false;
    }
    return true;
}
void reconstruction_and_budgets() {
    const auto f = satsolv::parse_dimacs_text("p cnf 7 7\n1 2 0\n-1 3 0\n-2 4 0\n-3 5 0\n-4 6 0\n-5 7 0\n-6 -7 0\n");
    for (std::uint64_t budget : {0ULL, 1ULL, 7ULL, 100ULL, 1000000ULL}) for (std::uint64_t seed : {1ULL, 999ULL}) {
        satsolv::SolverConfig config; config.seed = seed; config.simplify = true; config.simplify_budget = budget;
        satsolv::Solver solver(config);
        auto out = solver.solve(f);
        assert(out.result == satsolv::SolveResult::Sat); assert(satisfies(f, out.model));
        out = solver.solve(satsolv::parse_dimacs_text("p cnf 1 2\n1 0\n-1 0\n"));
        assert(out.result == satsolv::SolveResult::Unsat);
        out = solver.solve(f); assert(satisfies(f, out.model));
    }
}
void seeds_are_reproducible() {
    const auto f = satsolv::parse_dimacs_text("p cnf 32 0\n");
    satsolv::SolverConfig a; a.simplify = false;
    satsolv::SolverConfig b = a; b.seed = 999;
    const auto x = satsolv::Solver(a).solve(f), y = satsolv::Solver(b).solve(f), z = satsolv::Solver(b).solve(f);
    assert(x.model != y.model); assert(y.model == z.model);
}
void deletion_and_compaction() {
    constexpr int n = 14;
    satsolv::Formula f; f.variables = n;
    for (int bits = 0; bits < (1<<n); ++bits) {
        std::vector<int> clause;
        for (int v = 1; v <= n; ++v) clause.push_back((bits & (1<<(v-1))) != 0 ? -v : v);
        f.clauses.push_back(std::move(clause));
    }
    satsolv::SolverConfig config; config.simplify = false; config.reduce_interval = 100; config.telemetry = true;
    const auto out = satsolv::Solver(config).solve(f);
    assert(out.result == satsolv::SolveResult::Unsat);
    assert(out.stats.deleted_learnt_clauses > 4096);
    assert(out.stats.descriptor_compactions > 0);
    assert(out.stats.peak_heap_entries <= n);
    assert(out.stats.clause_descriptors < f.clauses.size()+out.stats.learnt_clauses);
}
void failed_proof_output_is_an_error() {
    if (!std::filesystem::exists("/dev/full")) return;
    bool threw = false;
    try {
        satsolv::Solver().solve(satsolv::parse_dimacs_text("p cnf 1 2\n1 0\n-1 0\n"), std::filesystem::path("/dev/full"));
    } catch (const std::runtime_error&) { threw = true; }
    assert(threw);
}
}
int main() {
    heap_properties(); reconstruction_and_budgets(); seeds_are_reproducible();
    deletion_and_compaction(); failed_proof_output_is_an_error();
    std::cout << "PASS: heap reference model, reconstruction budgets, seed determinism, real deletion/compaction, proof I/O failure\n";
}
