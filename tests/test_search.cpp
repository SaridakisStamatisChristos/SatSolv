#include "satsolv/solver.hpp"

#include <cassert>
#include <iostream>
#include <limits>

namespace satsolv {
struct SolverSearchTestAccess {
    static void scores_and_tiers() {
        SolverConfig config; config.rank_lbd = true;
        Solver solver(config); solver.reset(3);
        solver.levels_ = {0, 3, 2, 1};
        const std::vector<int> literals{-1, -2, -3};
        const int birth = solver.compute_lbd(literals);
        assert(birth == 3);
        const int cid = solver.add_clause(literals, true, birth);
        auto& clause = solver.clauses_[static_cast<std::size_t>(cid)];
        assert(clause.birth_lbd == 3 && clause.lbd == 2);
        assert(!solver.protected_clause(clause));
        solver.levels_[1] = 2; // UIP asserted at the backjump level.
        assert(solver.compute_lbd(literals) == 2);
        assert(solver.compute_antecedent_lbd(clause, 1) == 2);
        solver.levels_[2] = 1;
        assert(solver.compute_antecedent_lbd(clause, 1) == 1);
        clause.lbd = 1;
        assert(!solver.protected_clause(clause)); // ranking never promotes.
        solver.level_epoch_ = std::numeric_limits<std::uint64_t>::max();
        assert(solver.compute_antecedent_lbd(clause, 1) == 1);
        assert(clause.birth_lbd == 3);
        solver.config_.rank_lbd = false;
        assert(solver.protected_clause(clause)); // legacy admission, preserved.
        solver.config_.fixed_lbd_tier = true;
        assert(!solver.protected_clause(clause));
        clause.birth_lbd = 2;
        assert(solver.protected_clause(clause));
        assert(sizeof(Solver::Clause) <= 40); // no per-clause memory expansion on this ABI.
    }

    static void recent_use_and_locks() {
        SolverConfig config; config.recent_use = true;
        Solver solver(config); solver.reset(8);
        for (int i = 0; i < 6; ++i) solver.add_clause({1, 2, i + 3}, true, 4);
        solver.clauses_[0].used = true;
        solver.clauses_[1].used = true; solver.clauses_[1].reason_refs = 1;
        solver.clauses_[2].birth_lbd = solver.clauses_[2].lbd = 2;
        solver.clauses_[2].used = true;
        solver.reduce_database();
        assert(!solver.clauses_[0].deleted && !solver.clauses_[0].used);
        assert(!solver.clauses_[1].deleted && !solver.clauses_[1].used);
        assert(!solver.clauses_[2].deleted && !solver.clauses_[2].used);
        assert(solver.stats_.recent_use_kept == 1);
        solver.reduce_database();
        assert(solver.clauses_[0].deleted); // stale use does not mean forever protected.
        assert(!solver.clauses_[1].deleted && !solver.clauses_[2].deleted);
    }

    static void targets_and_reason_budget() {
        SolverConfig config; config.target_phases = true; config.reason_bump = true;
        Solver solver(config); solver.reset(5);
        solver.trail_lim_.push_back(0);
        assert(solver.enqueue(1, -1)); assert(solver.enqueue(2, -1));
        solver.capture_target(); assert(solver.stats_.target_phase_writes == 2);
        solver.cancel_until(0); assert(solver.target_shared_prefix_ == 0);
        solver.trail_lim_.push_back(0);
        assert(solver.enqueue(-1, -1)); assert(solver.enqueue(3, -1)); assert(solver.enqueue(4, -1));
        solver.capture_target();
        assert(solver.stats_.target_phase_writes == 5 && solver.target_phase_[1] == 0);
        solver.cancel_until(0);
        const int decision = solver.pick_branch_lit();
        assert(decision == -1 && solver.stats_.target_decisions == 1);
        solver.reset(5); // no target or reason marks leak between solve calls.
        assert(solver.target_depth_ == 0 && solver.target_shared_prefix_ == 0);
        assert(solver.target_phase_[1] == -1 && solver.stats_.target_decisions == 0);
        const int cid = solver.add_clause({1, 2, 4}, false);
        solver.reasons_[4] = cid; solver.levels_ = {0, 1, 2, 0, 2, 0};
        solver.config_.reason_bump_budget = 0;
        solver.bump_reason_variables({-4}); assert(solver.stats_.reason_bump_scans == 0);
        solver.config_.reason_bump_budget = 6;
        solver.bump_reason_variables({-4, -4});
        assert(solver.stats_.reason_bump_scans == 6 && solver.stats_.reason_bumps == 2);
        solver.reason_bump_epoch_ = std::numeric_limits<std::uint64_t>::max();
        solver.bump_reason_variables({-4}); assert(solver.stats_.reason_bumps == 4);
    }

    static void delayed_activation() {
        SolverConfig config; config.rank_lbd = config.target_phases = config.reason_bump = config.recent_use = true;
        config.search_warmup = 1000;
        Solver solver(config); solver.reset(4);
        assert(!solver.search_active_);
        const int cid = solver.add_clause({1, 2, 3}, true, 3);
        auto& c = solver.clauses_[static_cast<std::size_t>(cid)];
        assert(c.lbd == 3 && !solver.protected_clause(c));
        solver.stats_.conflicts = 1000;
        solver.activate_search();
        assert(c.lbd == 2 && c.birth_lbd == 3 && !solver.protected_clause(c));
        assert(solver.search_active_ && solver.stats_.search_activations == 1);
        assert(solver.next_target_reset_ == 1000 + config.target_window);
        solver.activate_search(); assert(solver.stats_.search_activations == 1);
        const int fresh = solver.add_clause({1,2,4}, true, 3);
        assert(solver.clauses_[static_cast<std::size_t>(fresh)].lbd == 2);
        solver.reset(4); assert(!solver.search_active_ && solver.stats_.search_activations == 0);
        solver.config_.search_warmup = 0;
        solver.config_.search_warmup_seconds = 1.0;
        solver.start_time_ = std::chrono::steady_clock::now();
        solver.maybe_activate_search(); assert(!solver.search_active_);
        solver.start_time_ -= std::chrono::seconds(2);
        solver.maybe_activate_search(); assert(solver.search_active_ && solver.stats_.search_activations == 1);
    }
};
}

int main() {
    satsolv::SolverSearchTestAccess::scores_and_tiers();
    satsolv::SolverSearchTestAccess::recent_use_and_locks();
    satsolv::SolverSearchTestAccess::targets_and_reason_budget();
    satsolv::SolverSearchTestAccess::delayed_activation();
    satsolv::SolverConfig config; config.rank_lbd = config.dynamic_lbd = true;
    bool rejected = false;
    try { satsolv::Solver solver(config); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    config = {}; config.target_phases = true; config.target_window = 0; rejected = false;
    try { satsolv::Solver solver(config); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    config = {}; config.rank_lbd = config.target_phases = config.reason_bump = config.recent_use = true;
    config.target_window = 1; config.reduce_interval = 1;
    satsolv::Solver reusable(config);
    const auto formula = satsolv::parse_dimacs_text("p cnf 4 4\n1 2 0\n-1 3 0\n-2 4 0\n-3 -4 0\n");
    const auto first = reusable.solve(formula);
    assert(first.result == satsolv::SolveResult::Sat);
    assert(reusable.solve(satsolv::parse_dimacs_text("p cnf 1 2\n1 0\n-1 0\n")).result == satsolv::SolveResult::Unsat);
    assert(reusable.solve(satsolv::parse_dimacs_text("p cnf 0 0\n")).result == satsolv::SolveResult::Sat);
    const auto second = reusable.solve(formula);
    assert(first.model == second.model && first.stats.conflicts == second.stats.conflicts);
    for (const auto& clause : formula.clauses) {
        bool satisfied = false;
        for (int lit : clause) if (second.model[static_cast<std::size_t>((lit < 0 ? -lit : lit) - 1)] == lit) satisfied = true;
        assert(satisfied);
    }
    std::cout << "PASS: birth/reason score conventions, fixed tiers, recent-use expiration, reason locks, target backtracking/reset, bounded bumps and epoch wrap\n";
}
