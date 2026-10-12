#pragma once

#include "satsolv/parser.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

namespace satsolv {

enum class SolveResult { Sat, Unsat, Unknown };

struct SolverConfig {
    std::uint64_t seed = 1;
    double time_limit_seconds = 0.0;
    int restart_base = 100;
    int reduce_interval = 2000;
    double var_decay = 0.80;
};

struct SolverStats {
    double solve_seconds = 0.0;
    std::uint64_t conflicts = 0;
    std::uint64_t decisions = 0;
    std::uint64_t propagations = 0;
    std::uint64_t restarts = 0;
    std::uint64_t learnt_clauses = 0;
    std::uint64_t deleted_learnt_clauses = 0;
    std::size_t max_trail = 0;
    std::uint64_t proof_bytes = 0;
};

struct SolveOutput {
    SolveResult result = SolveResult::Unknown;
    std::vector<int> model;
    SolverStats stats;
};

class Solver {
public:
    explicit Solver(SolverConfig config = {});
    SolveOutput solve(const Formula& formula, const std::optional<std::filesystem::path>& proof_path = std::nullopt);

private:
    struct Clause {
        std::vector<int> lits;
        int watch0 = 0;
        int watch1 = 0;
        bool learnt = false;
        bool deleted = false;
        int lbd = 0;
        double activity = 0.0;
    };

    struct HeapEntry {
        double activity;
        int variable;
        std::uint64_t stamp;
    };
    struct HeapLess {
        bool operator()(const HeapEntry& a, const HeapEntry& b) const noexcept {
            if (a.activity != b.activity) return a.activity < b.activity;
            if (a.variable != b.variable) return a.variable > b.variable;
            return a.stamp < b.stamp;
        }
    };

    SolverConfig config_;
    SolverStats stats_{};
    int nvars_ = 0;
    std::vector<Clause> clauses_;
    std::vector<std::vector<int>> watches_;
    std::vector<std::int8_t> assigns_;
    std::vector<int> levels_;
    std::vector<int> reasons_;
    std::vector<int> trail_;
    std::vector<std::size_t> trail_lim_;
    std::size_t qhead_ = 0;
    std::vector<double> activity_;
    std::vector<std::uint64_t> heap_stamp_;
    std::vector<std::int8_t> saved_phase_;
    std::priority_queue<HeapEntry, std::vector<HeapEntry>, HeapLess> order_heap_;
    double var_inc_ = 1.0;
    std::ofstream proof_;
    bool proof_enabled_ = false;
    bool root_unsat_ = false;
    std::chrono::steady_clock::time_point start_time_{};
    std::uint64_t restart_index_ = 1;
    std::uint64_t conflicts_since_restart_ = 0;
    std::uint64_t next_restart_ = 100;

    static int var(int lit) noexcept { return lit < 0 ? -lit : lit; }
    int lit_index(int lit) const noexcept;
    int value_lit(int lit) const noexcept;
    int decision_level() const noexcept { return static_cast<int>(trail_lim_.size()); }
    bool timed_out() const;

    void reset(int variables);
    int add_clause(std::vector<int> lits, bool learnt, int lbd = 0);
    bool enqueue(int lit, int reason);
    int propagate();
    void cancel_until(int level);
    int pick_branch_lit();
    void bump_variable(int variable);
    void decay_activities();
    void rescale_activities();
    std::pair<std::vector<int>, int> analyze(int conflict_clause);
    int compute_lbd(const std::vector<int>& clause) const;
    void minimize_clause(std::vector<int>& clause);
    void reduce_database();
    bool clause_locked(int cid) const;
    void log_clause(const std::vector<int>& clause);
    void log_empty();
    static std::uint64_t luby(std::uint64_t index);
};

std::string result_name(SolveResult result);

} // namespace satsolv
