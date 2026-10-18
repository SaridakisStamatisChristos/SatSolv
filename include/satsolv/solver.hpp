#pragma once

#include "satsolv/parser.hpp"
#include "satsolv/indexed_heap.hpp"
#include "satsolv/simplifier.hpp"
#include "satsolv/congruence.hpp"
#include "satsolv/strengthen.hpp"

#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace satsolv {

enum class SolveResult { Sat, Unsat, Unknown };

struct SolverConfig {
    std::uint64_t seed = 1;
    double time_limit_seconds = 0.0;
    int restart_base = 100;
    int reduce_interval = 2000;
    double var_decay = 0.95;
    double clause_decay = 1.0;
    bool binary_minimize = false;
    bool simplify = false;
    bool telemetry = false;
    bool congruence = true;
    bool watched_vivify = false;
    bool bounded_eliminate = false;
    bool dynamic_lbd = false;
    bool fixed_lbd_tier = false;
    bool rank_lbd = false;
    bool target_phases = false;
    bool reason_bump = false;
    bool recent_use = false;
    bool short_ssr = false;
    bool implication_strengthen = false;
    bool strengthen_keep_activity = false;
    bool root_probe = false;
    bool experimental_bva = false;
    std::uint64_t bva_budget = 2000000;
    std::uint64_t bva_max_variables = 16;
    std::uint64_t probe_start = 1000000;
    std::uint64_t probe_budget = 1000000;
    std::uint64_t strengthen_budget = 8000000;
    std::uint64_t target_window = 10000;
    std::uint64_t reason_bump_budget = 128;
    std::uint64_t search_warmup = 0;
    double search_warmup_seconds = 0.0;
    std::uint64_t eliminate_budget = 8000000;
    std::uint64_t vivify_budget = 16000000;
    std::uint64_t congruence_budget = 4000000;
    std::uint64_t binary_minimize_budget = 128;
    std::uint64_t simplify_budget = 1000000;

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
    std::uint64_t congruence_equivalences = 0;
    std::uint64_t congruence_and_gates = 0;
    std::uint64_t congruence_work = 0;
    std::uint64_t congruence_rounds = 0;
    std::uint64_t watch_visits = 0;
    std::uint64_t blocker_hits = 0;
    std::uint64_t descriptor_scans = 0;
    std::uint64_t descriptor_compactions = 0;
    std::uint64_t heap_updates = 0;
    std::uint64_t peak_heap_entries = 0;
    std::uint64_t binary_edges_scanned = 0;
    std::uint64_t binary_literals_removed = 0;
    std::uint64_t simplified_literals = 0;
    std::uint64_t eliminated_variables = 0;
    std::uint64_t elimination_work = 0;
    std::uint64_t elimination_attempts = 0;
    std::uint64_t tautological_resolvents = 0;
    std::uint64_t lbd_improvements = 0;
    std::uint64_t lbd_tier_crossings = 0;
    std::uint64_t reductions = 0;
    std::uint64_t peak_protected_clauses = 0;
    std::uint64_t peak_protected_literals = 0;
    std::uint64_t peak_birth_protected_clauses = 0;
    std::uint64_t recent_use_kept = 0;
    std::uint64_t target_captures = 0;
    std::uint64_t target_phase_writes = 0;
    std::uint64_t target_decisions = 0;
    std::uint64_t target_resets = 0;
    std::uint64_t reason_bump_scans = 0;
    std::uint64_t reason_bumps = 0;
    std::uint64_t search_activations = 0;
    std::uint64_t strengthen_work = 0;
    std::uint64_t ssr_removed = 0;
    std::uint64_t implication_removed = 0;
    std::uint64_t binary_failed_units = 0;
    std::uint64_t probe_work = 0;
    std::uint64_t probe_attempts = 0;
    std::uint64_t probe_rounds = 0;
    std::uint64_t bva_work = 0;
    std::uint64_t bva_variables_added = 0;
    std::uint64_t bva_clauses_removed = 0;
    std::uint64_t bva_literals_saved = 0;
    std::uint64_t probe_units = 0;
    std::uint64_t probe_binaries = 0;
    std::array<std::uint64_t, 16> birth_lbd_histogram{};
    std::array<std::uint64_t, 16> reuse_score_histogram{};
    std::uint64_t subsumed_clauses = 0;
    std::uint64_t probed_units = 0;
    std::uint64_t vivified_clauses = 0;
    std::uint64_t vivify_attempts = 0;
    std::uint64_t vivify_work = 0;
    std::uint64_t vivify_removed = 0;
    std::uint64_t live_clauses = 0;
    std::uint64_t clause_descriptors = 0;

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
    friend struct SolverSearchTestAccess;
    struct Clause {
        std::size_t offset = 0;
        double activity = 0.0;
        std::uint32_t size = 0;
        std::uint32_t scan_cursor = 2;
        std::uint32_t reason_refs = 0;
        int lbd = 0;
        int birth_lbd = 0;
        bool learnt = false;
        bool deleted = false;
        bool used = false;
    };

    struct Watcher {
        int clause = -1;
        int blocker = 0;
    };

    struct BinaryWatcher {
        int clause = -1;
        int implied = 0;
    };

    SolverConfig config_;
    SolverStats stats_{};
    int nvars_ = 0;
    std::vector<Clause> clauses_;
    std::vector<int> learnt_ids_;
    std::size_t dead_descriptors_ = 0;
    std::vector<int> literal_arena_;
    std::size_t dead_literals_ = 0;
    std::vector<std::vector<Watcher>> watches_;
    std::vector<std::vector<BinaryWatcher>> binary_watches_;
    std::vector<std::int8_t> assigns_;
    std::vector<int> levels_;
    std::vector<int> reasons_;
    std::vector<int> trail_;
    std::vector<std::size_t> trail_lim_;
    std::size_t qhead_ = 0;
    std::vector<double> activity_;
    std::vector<std::uint64_t> heap_rank_;
    std::vector<std::uint8_t> decision_enabled_;
    std::vector<std::int8_t> saved_phase_;
    std::vector<std::int8_t> target_phase_;
    std::size_t target_depth_ = 0;
    std::size_t target_shared_prefix_ = 0;
    std::uint64_t next_target_reset_ = 0;
    std::uint64_t target_window_ = 0;
    std::vector<std::uint64_t> reason_bump_marks_;
    std::uint64_t reason_bump_epoch_ = 0;
    bool search_active_ = true;
    IndexedHeap order_heap_;
    double var_inc_ = 1.0;
    double clause_inc_ = 1.0;

    std::vector<std::uint8_t> analyze_seen_;
    std::vector<int> analyze_touched_;
    std::vector<std::uint64_t> level_marks_;
    std::uint64_t level_epoch_ = 0;
    std::vector<std::uint64_t> literal_marks_;
    std::uint64_t literal_epoch_ = 0;
    std::vector<int> minimize_scratch_;

    std::ofstream proof_;
    std::vector<char> proof_buffer_;
    bool proof_enabled_ = false;
    bool root_unsat_ = false;
    std::chrono::steady_clock::time_point start_time_{};
    std::uint64_t restart_index_ = 1;
    std::uint64_t conflicts_since_restart_ = 0;
    std::uint64_t next_restart_ = 100;
    int vivify_ignored_ = -1;
    std::uint64_t vivify_left_ = 0;
    std::uint64_t next_vivify_ = 2000;
    bool probing_ = false;
    std::uint64_t probe_left_ = 0;
    std::uint64_t next_probe_ = 1000000;

    static int var(int lit) noexcept { return lit < 0 ? -lit : lit; }
    int lit_index(int lit) const noexcept;
    int value_lit(int lit) const noexcept;
    int decision_level() const noexcept { return static_cast<int>(trail_lim_.size()); }
    bool timed_out() const;
    int clause_lit(const Clause& clause, std::size_t index) const noexcept;
    int& clause_lit(Clause& clause, std::size_t index) noexcept;

    void reset(int variables);
    int add_clause(const std::vector<int>& lits, bool learnt, int lbd = 0);
    bool enqueue(int lit, int reason);
    int propagate();
    template<bool Vivifying> int propagate_impl();
    int vivify_database(std::uint64_t budget);
    int probe_database(std::uint64_t budget);
    void cancel_until(int level);
    int pick_branch_lit();
    void bump_variable(int variable);
    void decay_activities();
    void rescale_activities();
    void analyze(int conflict_clause, std::vector<int>& learnt, int& backtrack);
    int compute_lbd(const std::vector<int>& clause);
    int compute_antecedent_lbd(const Clause& clause, int implied_variable);
    bool protected_clause(const Clause& clause) const noexcept;
    void capture_target();
    void bump_reason_variables(const std::vector<int>& learnt);
    void activate_search();
    void maybe_activate_search();
    void minimize_clause(std::vector<int>& clause);
    void reduce_database();
    bool clause_locked(int cid) const noexcept;
    void compact_clause_arena();
    void log_clause(const std::vector<int>& clause);
    void log_empty();
    void log_record(const std::vector<int>& clause, bool deletion);
    void flush_proof();
    void append_proof_byte(char byte);
    void write_binary_literal(int lit);
    static std::uint64_t luby(std::uint64_t index);
};

std::string result_name(SolveResult result);

} // namespace satsolv

