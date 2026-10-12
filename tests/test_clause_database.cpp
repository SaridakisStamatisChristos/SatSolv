#include "satsolv/solver.hpp"

#include <stdexcept>
#include <iostream>

namespace satsolv {
struct SolverTestAccess {
    static void require(bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    }
    static void run() {
        SolverConfig config;
        config.reduce_interval = 4;
        Solver s(config);
        s.reset(8);
        const int original = s.add_clause({1, 2, 3}, false);
        const int binary = s.add_clause({1, 2}, true, 8);
        const int core = s.add_clause({1, 2, 3}, true, 2);
        const int locked = s.add_clause({1, 2, 4}, true, 8);
        require(s.enqueue(1, locked), "lock enqueue failed");
        for (int i = 0; i < 12; ++i) s.add_clause({2, 3, 4}, true, 4);
        s.stats_.conflicts = 1;
        for (int i = 4; i < 16; ++i) s.refresh_clause_quality(i, 4);
        require(s.stats_.tier2_count == 0, "one use must not promote initial LBD4");
        s.refresh_clause_quality(4, 4);
        require(s.quality_[4].uses == 1, "same-conflict touch counted twice");
        s.stats_.conflicts = 2;
        for (int i = 4; i < 16; ++i) s.refresh_clause_quality(i, 4);
        require(s.stats_.tier2_count == 12, "reuse did not promote TIER2");
        s.stats_.conflicts = 3;
        for (int i = 4; i < 16; ++i) s.refresh_clause_quality(i, 4);
        s.reduce_database();
        require(s.stats_.deleted_learnt_clauses == 6, "must delete half of eligible pool");
        require(s.stats_.tier2_count == 3, "TIER2 cap must bound retained eligible clauses");
        require(!s.clauses_[static_cast<std::size_t>(original)].deleted, "original deleted");
        require(!s.clauses_[static_cast<std::size_t>(binary)].deleted, "binary deleted");
        require(!s.clauses_[static_cast<std::size_t>(core)].deleted, "core deleted");
        require(!s.clauses_[static_cast<std::size_t>(locked)].deleted, "reason deleted");
        require(s.stats_.tier2_reused_before_deletion > 0, "TIER2 reuse evidence lost on demotion");
        s.stats_.conflicts = 7;
        s.reduce_database();
        require(s.stats_.tier2_count == 0, "stale TIER2 survived a full interval");
        const int improved = s.add_clause({2, 3, 4}, true, 9);
        s.stats_.conflicts = 8;
        s.refresh_clause_quality(improved, 4);
        require(s.quality_[static_cast<std::size_t>(improved)].tier == Solver::ClauseTier::Tier2,
                "LBD improvement did not promote");
        s.stats_.conflicts = 9;
        s.refresh_clause_quality(improved, 2);
        s.refresh_clause_quality(improved, 6);
        require(s.clauses_[static_cast<std::size_t>(improved)].lbd == 2, "LBD worsened");
        s.reduce_database();
        require(!s.clauses_[static_cast<std::size_t>(improved)].deleted, "promoted CORE deleted");
        std::uint64_t live = 0, watchers = 0;
        for (const auto& c : s.clauses_) {
            if (c.deleted) continue;
            if (c.learnt) ++live;
            if (c.size >= 2) watchers += 2;
        }
        require(live == s.stats_.core_count + s.stats_.tier2_count + s.stats_.local_count,
                "tier conservation failed");
        require(watchers == s.stats_.live_watchers, "watcher conservation failed");
        s.compact_clause_arena();
        require(s.clauses_[static_cast<std::size_t>(locked)].reason_refs == 1,
                "compaction damaged reason lock");
        s.reset(8);
        require(s.quality_.empty() && s.stats_.demotions == 0, "metadata survived reset");
    }
};
} // namespace satsolv

int main() {
    satsolv::SolverTestAccess::run();
    // Exercise real search/reduction and reuse of one Solver on UNSAT and SAT.
    satsolv::Formula f;
    f.variables = 10;
    for (int mask = 0; mask < (1 << f.variables); ++mask) {
        std::vector<int> clause;
        for (int v = 1; v <= f.variables; ++v)
            clause.push_back((mask & (1 << (v - 1))) ? -v : v);
        f.clauses.push_back(clause);
    }
    satsolv::SolverConfig config;
    config.reduce_interval = 4;
    satsolv::Solver solver(config);
    auto out = solver.solve(f);
    if (out.result != satsolv::SolveResult::Unsat || out.stats.reductions == 0)
        throw std::runtime_error("real reduction search failed");
    f.clauses.pop_back();
    out = solver.solve(f);
    if (out.result != satsolv::SolveResult::Sat)
        throw std::runtime_error("solver reuse SAT failed");
    std::cout << "clause database policy and search tests passed\n";
}
