#include "satsolv/solver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace satsolv {
namespace {
std::uint64_t seed_hash(std::uint64_t x) noexcept {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27U)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31U);
}
}


Solver::Solver(SolverConfig config) : config_(config) {
    if (!(config_.var_decay > 0.0 && config_.var_decay < 1.0)) {
        throw std::invalid_argument("var_decay must be in (0,1)");
    }
    if (!(config_.clause_decay > 0.0 && config_.clause_decay <= 1.0)) throw std::invalid_argument("clause_decay must be in (0,1]");
    if (config_.restart_base <= 0 || config_.reduce_interval <= 0 || !std::isfinite(config_.time_limit_seconds) || config_.time_limit_seconds < 0.0) {
        throw std::invalid_argument("invalid solver configuration");
    }
}

std::string result_name(SolveResult result) {
    switch (result) {
        case SolveResult::Sat: return "SAT";
        case SolveResult::Unsat: return "UNSAT";
        case SolveResult::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

int Solver::lit_index(int lit) const noexcept {
    const int v = var(lit) - 1;
    return 2 * v + (lit < 0 ? 1 : 0);
}

int Solver::value_lit(int lit) const noexcept {
    const int v = var(lit);
    const std::int8_t a = assigns_[static_cast<std::size_t>(v)];
    if (a < 0) return -1;
    const bool truth = (a == 1);
    const bool lit_truth = lit > 0 ? truth : !truth;
    return lit_truth ? 1 : 0;
}

int Solver::clause_lit(const Clause& clause, std::size_t index) const noexcept {
    return literal_arena_[clause.offset + index];
}

int& Solver::clause_lit(Clause& clause, std::size_t index) noexcept {
    return literal_arena_[clause.offset + index];
}

bool Solver::timed_out() const {
    if (config_.time_limit_seconds <= 0.0) return false;
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count();
    return elapsed >= config_.time_limit_seconds;
}

void Solver::reset(int variables) {
    nvars_ = variables;
    stats_ = {};
    clauses_.clear();
    learnt_ids_.clear();
    dead_descriptors_ = 0;
    proof_buffer_.clear();
    proof_buffer_.reserve(65536);
    literal_arena_.clear();
    dead_literals_ = 0;
    watches_.assign(static_cast<std::size_t>(2 * variables), {});
    binary_watches_.assign(static_cast<std::size_t>(2 * variables), {});
    assigns_.assign(static_cast<std::size_t>(variables + 1), static_cast<std::int8_t>(-1));
    levels_.assign(static_cast<std::size_t>(variables + 1), 0);
    reasons_.assign(static_cast<std::size_t>(variables + 1), -1);
    trail_.clear();
    trail_lim_.clear();
    qhead_ = 0;
    activity_.assign(static_cast<std::size_t>(variables + 1), 0.0);
    heap_rank_.resize(static_cast<std::size_t>(variables + 1));
    decision_enabled_.assign(static_cast<std::size_t>(variables + 1), 1);
    for (int v = 1; v <= variables; ++v)
        heap_rank_[static_cast<std::size_t>(v)] = config_.seed == 1 ? static_cast<std::uint64_t>(v) : seed_hash(config_.seed ^ static_cast<std::uint64_t>(v));
    saved_phase_.assign(static_cast<std::size_t>(variables + 1), static_cast<std::int8_t>(1));
    order_heap_.reset(activity_, heap_rank_);
    var_inc_ = 1.0;
    clause_inc_ = 1.0;

    analyze_seen_.assign(static_cast<std::size_t>(variables + 1), 0);
    analyze_touched_.clear();
    analyze_touched_.reserve(256);
    level_marks_.assign(static_cast<std::size_t>(variables + 1), 0);
    level_epoch_ = 0;
    literal_marks_.assign(static_cast<std::size_t>(2 * variables), 0);
    literal_epoch_ = 0;
    minimize_scratch_.clear();
    minimize_scratch_.reserve(64);

    root_unsat_ = false;
    restart_index_ = 1;
    conflicts_since_restart_ = 0;
    next_restart_ = static_cast<std::uint64_t>(config_.restart_base) * luby(restart_index_);
    vivify_ignored_ = -1;
    vivify_left_ = 0;
    next_vivify_ = 2000;
    for (int v = 1; v <= variables; ++v) {
        if (config_.seed != 1) saved_phase_[static_cast<std::size_t>(v)] = static_cast<std::int8_t>(seed_hash(heap_rank_[static_cast<std::size_t>(v)]) & 1ULL);
        order_heap_.update(v);
    }
}

int Solver::add_clause(const std::vector<int>& input, bool learnt, int lbd) {
    std::vector<int> normalized;
    const std::vector<int>* source = &input;

    if (!learnt) {
        normalized = input;
        std::sort(normalized.begin(), normalized.end(), [](int a, int b) {
            const int va = a < 0 ? -a : a;
            const int vb = b < 0 ? -b : b;
            if (va != vb) return va < vb;
            return a < b;
        });

        std::size_t out = 0;
        for (int lit : normalized) {
            if (out > 0 && normalized[out - 1] == lit) continue;
            if (out > 0 && var(normalized[out - 1]) == var(lit) && normalized[out - 1] == -lit) {
                return -1;
            }
            normalized[out++] = lit;
        }
        normalized.resize(out);
        source = &normalized;
    }

    if (source->size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::length_error("clause too large");
    }

    const int cid = static_cast<int>(clauses_.size());
    Clause clause;
    clause.offset = literal_arena_.size();
    clause.size = static_cast<std::uint32_t>(source->size());
    clause.scan_cursor = 2;
    clause.learnt = learnt;
    clause.lbd = lbd;
    literal_arena_.insert(literal_arena_.end(), source->begin(), source->end());
    clauses_.push_back(clause);
    if (learnt) learnt_ids_.push_back(cid);

    Clause& stored = clauses_.back();
    if (stored.size == 0) {
        root_unsat_ = true;
        return cid;
    }

    if (stored.size == 1) {
        if (!learnt && !enqueue(clause_lit(stored, 0), cid)) root_unsat_ = true;
        return cid;
    }

    const int first = clause_lit(stored, 0);
    const int second = clause_lit(stored, 1);
    if (stored.size == 2) {
        binary_watches_[static_cast<std::size_t>(lit_index(first))].push_back(BinaryWatcher{cid, second});
        binary_watches_[static_cast<std::size_t>(lit_index(second))].push_back(BinaryWatcher{cid, first});
        return cid;
    }

    watches_[static_cast<std::size_t>(lit_index(first))].push_back(Watcher{cid, second});
    watches_[static_cast<std::size_t>(lit_index(second))].push_back(Watcher{cid, first});
    return cid;
}

bool Solver::enqueue(int lit, int reason) {
    const int v = var(lit);
    const int current = value_lit(lit);
    if (current == 1) return true;
    if (current == 0) return false;

    if (reason >= 0) {
        Clause& c = clauses_[static_cast<std::size_t>(reason)];
        if (c.deleted) throw std::logic_error("deleted clause used as assignment reason");
        ++c.reason_refs;
    }

    assigns_[static_cast<std::size_t>(v)] = static_cast<std::int8_t>(lit > 0 ? 1 : 0);
    levels_[static_cast<std::size_t>(v)] = decision_level();
    reasons_[static_cast<std::size_t>(v)] = reason;
    saved_phase_[static_cast<std::size_t>(v)] = assigns_[static_cast<std::size_t>(v)];
    trail_.push_back(lit);
    stats_.max_trail = std::max(stats_.max_trail, trail_.size());
    return true;
}

int Solver::propagate() {
    return propagate_impl<false>();
}

template<bool Vivifying> int Solver::propagate_impl() {
    [[maybe_unused]] const auto charge = [this]() {
        if (vivify_left_ == 0) return false;
        --vivify_left_; ++stats_.vivify_work;
        return (stats_.vivify_work & 1023U) != 0U || !timed_out();
    };
    while (qhead_ < trail_.size()) {
        if constexpr (Vivifying) { if (!charge()) return -3; }
        const int p = trail_[qhead_++];
        ++stats_.propagations;
        const std::size_t false_index = static_cast<std::size_t>(lit_index(-p));

        const auto& binary = binary_watches_[false_index];
        for (const BinaryWatcher& watcher : binary) {
            if constexpr (Vivifying) { if (!charge()) return -3; }
            // Binary clauses are never reduced; the watcher is the implication.
            const int value = value_lit(watcher.implied);
            if (value == 0) return watcher.clause;
            if (value < 0 && !enqueue(watcher.implied, watcher.clause)) return watcher.clause;
        }

        auto& ws = watches_[false_index];
        std::size_t i = 0;
        while (i < ws.size()) {
            if constexpr (Vivifying) {
                if (!charge()) return -3;
                if (ws[i].clause == vivify_ignored_) { ++i; continue; }
            }
            if (config_.telemetry) ++stats_.watch_visits;
            if (value_lit(ws[i].blocker) == 1) {
                if (config_.telemetry) ++stats_.blocker_hits;
                ++i;
                continue;
            }
            const int cid = ws[i].clause;
            Clause& c = clauses_[static_cast<std::size_t>(cid)];
            if (c.deleted) {
                ws[i] = ws.back();
                ws.pop_back();
                continue;
            }

            int false_slot = -1;
            if (clause_lit(c, 0) == -p) false_slot = 0;
            else if (clause_lit(c, 1) == -p) false_slot = 1;
            else {
                ws[i] = ws.back();
                ws.pop_back();
                continue;
            }

            const int other_slot = 1 - false_slot;
            const int other_lit = clause_lit(c, static_cast<std::size_t>(other_slot));
            ws[i].blocker = other_lit;
            if (value_lit(other_lit) == 1) {
                ++i;
                continue;
            }

            bool moved = false;
            [[maybe_unused]] bool exhausted = false;
            std::size_t start = static_cast<std::size_t>(c.scan_cursor);
            if (start < 2 || start >= static_cast<std::size_t>(c.size)) start = 2;

            auto try_range = [&](std::size_t begin, std::size_t end) {
                for (std::size_t k = begin; k < end; ++k) {
                    if constexpr (Vivifying) { if (!charge()) { exhausted = true; return; } }
                    const int candidate = clause_lit(c, k);
                    if (value_lit(candidate) == 0) continue;

                    std::swap(clause_lit(c, static_cast<std::size_t>(false_slot)), clause_lit(c, k));
                    c.scan_cursor = static_cast<std::uint32_t>(k + 1 < static_cast<std::size_t>(c.size) ? k + 1 : 2);
                    const int new_watch = clause_lit(c, static_cast<std::size_t>(false_slot));
                    const Watcher moved_watcher{cid, other_lit};
                    ws[i] = ws.back();
                    ws.pop_back();
                    watches_[static_cast<std::size_t>(lit_index(new_watch))].push_back(moved_watcher);
                    moved = true;
                    return;
                }
            };

            try_range(start, static_cast<std::size_t>(c.size));
            if (!moved && !exhausted && start > 2) try_range(2, start);
            if constexpr (Vivifying) { if (exhausted) return -3; }
            if (moved) continue;

            const int other_value = value_lit(other_lit);
            if (other_value == 0) return cid;
            if (!enqueue(other_lit, cid)) return cid;
            ++i;
        }

        if ((stats_.propagations & 0x3FFFU) == 0U && timed_out()) return -2;
    }
    return -1;
}

int Solver::vivify_database(std::uint64_t budget) {
    if (decision_level() != 0) throw std::logic_error("vivification requires root level");
    if (budget == 0 || nvars_ > 200000 || clauses_.size() > 1000000 || literal_arena_.size() > 4000000 || timed_out())
        return timed_out() ? -2 : -1;
    const auto phases = saved_phase_;
    vivify_left_ = budget;
    std::vector<int> candidates;
    for (std::size_t i = 0; i < clauses_.size(); ++i) {
        const Clause& c = clauses_[i];
        if (!c.deleted && c.size >= 3 && c.size <= 64 && c.reason_refs == 0 && (!c.learnt || c.lbd <= 6))
            candidates.push_back(static_cast<int>(i));
    }
    std::stable_sort(candidates.begin(), candidates.end(), [this](int a, int b) {
        const Clause& x = clauses_[static_cast<std::size_t>(a)];
        const Clause& y = clauses_[static_cast<std::size_t>(b)];
        if (x.learnt != y.learnt) return x.learnt;
        return x.size < y.size;
    });
    std::vector<int> original, shortened;
    int root_conflict = -1;
    for (int cid : candidates) {
        if (vivify_left_ == 0 || timed_out()) break;
        const Clause& c = clauses_[static_cast<std::size_t>(cid)];
        if (c.deleted || c.reason_refs != 0) continue;
        original.clear();
        bool satisfied = false;
        for (std::size_t k = 0; k < c.size; ++k) {
            const int lit = clause_lit(c, k);
            original.push_back(lit);
            if (value_lit(lit) == 1) satisfied = true;
        }
        if (satisfied) continue;
        ++stats_.vivify_attempts;
        // Decisions are temporary assumptions. The candidate itself is ignored;
        // completed propagation is the only source of strengthening evidence.
        vivify_ignored_ = cid;
        trail_lim_.push_back(trail_.size());
        shortened.clear();
        bool complete = true;
        for (int lit : original) {
            if (vivify_left_ == 0 || timed_out()) { complete = false; break; }
            --vivify_left_; ++stats_.vivify_work;
            const int val = value_lit(lit);
            if (val == 0) continue;
            shortened.push_back(lit);
            if (val == 1) break;
            if (!enqueue(-lit, -1)) throw std::logic_error("vivification assumption conflict");
            const int conflict = propagate_impl<true>();
            if (conflict == -2 || conflict == -3) { complete = false; break; }
            if (conflict >= 0) break;
        }
        cancel_until(0);
        vivify_ignored_ = -1;
        if (!complete || shortened.size() >= original.size()) continue;
        // Addition must precede deletion. If all literals were visited without
        // conflict, the original clause plus the skipped implications supports
        // the shortened clause by RUP as well.
        log_clause(shortened);
        Clause& old = clauses_[static_cast<std::size_t>(cid)];
        const bool learnt = old.learnt;
        const int lbd = old.lbd;
        const double activity = old.activity;
        if (old.reason_refs != 0) throw std::logic_error("vivification removed a locked reason");
        log_record(original, true);
        old.deleted = true; ++dead_descriptors_; dead_literals_ += old.size;
        ++stats_.vivified_clauses;
        stats_.vivify_removed += original.size() - shortened.size();
        const int replacement = add_clause(shortened, learnt, lbd);
        clauses_[static_cast<std::size_t>(replacement)].activity = activity;
        if (root_unsat_) { root_conflict = replacement; break; }
        if (shortened.size() == 1 && !enqueue(shortened[0], replacement)) { root_conflict = replacement; break; }
        // New watches must see all persistent roots, including roots whose
        // propagation predates replacement. Never rely on old watch positions.
        qhead_ = 0;
        root_conflict = propagate();
        if (root_conflict != -1) break;
    }
    cancel_until(0);
    vivify_ignored_ = -1;
    saved_phase_ = phases;
    learnt_ids_.erase(std::remove_if(learnt_ids_.begin(), learnt_ids_.end(), [this](int cid) {
        return clauses_[static_cast<std::size_t>(cid)].deleted;
    }), learnt_ids_.end());
    if (root_conflict != -1) return root_conflict;
    return timed_out() ? -2 : -1;
}

void Solver::cancel_until(int level) {
    if (decision_level() <= level) return;
    const std::size_t target = trail_lim_[static_cast<std::size_t>(level)];
    for (std::size_t i = trail_.size(); i > target; --i) {
        const int lit = trail_[i - 1];
        const int v = var(lit);
        const int reason = reasons_[static_cast<std::size_t>(v)];
        if (reason >= 0) {
            Clause& c = clauses_[static_cast<std::size_t>(reason)];
            if (c.reason_refs == 0) throw std::logic_error("reason reference underflow");
            --c.reason_refs;
        }
        assigns_[static_cast<std::size_t>(v)] = -1;
        levels_[static_cast<std::size_t>(v)] = 0;
        reasons_[static_cast<std::size_t>(v)] = -1;
        if (decision_enabled_[static_cast<std::size_t>(v)] != 0) order_heap_.update(v);
    }
    trail_.resize(target);
    trail_lim_.resize(static_cast<std::size_t>(level));
    qhead_ = std::min(qhead_, trail_.size());
}

int Solver::pick_branch_lit() {
    while (!order_heap_.empty()) {
        const int v = order_heap_.pop();
        if (assigns_[static_cast<std::size_t>(v)] != -1 || decision_enabled_[static_cast<std::size_t>(v)] == 0) continue;
        return saved_phase_[static_cast<std::size_t>(v)] != 0 ? v : -v;
    }
    return 0;
}

void Solver::rescale_activities() {
    for (int v = 1; v <= nvars_; ++v) activity_[static_cast<std::size_t>(v)] *= 1e-100;
    var_inc_ *= 1e-100;
    order_heap_.rebuild();
}

void Solver::bump_variable(int variable) {
    activity_[static_cast<std::size_t>(variable)] += var_inc_;
    if (activity_[static_cast<std::size_t>(variable)] > 1e100) rescale_activities();
    if (decision_enabled_[static_cast<std::size_t>(variable)] != 0) order_heap_.update(variable);
    if (config_.telemetry) {
        ++stats_.heap_updates;
        stats_.peak_heap_entries = std::max(stats_.peak_heap_entries, static_cast<std::uint64_t>(order_heap_.size()));
    }
}

void Solver::decay_activities() {
    var_inc_ /= config_.var_decay;
    clause_inc_ /= config_.clause_decay;
    if (clause_inc_ > 1e100) {
        for (int cid : learnt_ids_) clauses_[static_cast<std::size_t>(cid)].activity *= 1e-100;
        clause_inc_ *= 1e-100;
    }
}

int Solver::compute_lbd(const std::vector<int>& clause) {
    ++level_epoch_;
    if (level_epoch_ == 0) {
        std::fill(level_marks_.begin(), level_marks_.end(), 0);
        level_epoch_ = 1;
    }

    int distinct = 0;
    for (int lit : clause) {
        const int level = levels_[static_cast<std::size_t>(var(lit))];
        const std::size_t index = static_cast<std::size_t>(level);
        if (level_marks_[index] == level_epoch_) continue;
        level_marks_[index] = level_epoch_;
        ++distinct;
    }
    return distinct;
}

void Solver::minimize_clause(std::vector<int>& clause) {
    if (clause.size() <= 2) return;

    ++literal_epoch_;
    if (literal_epoch_ == 0) {
        std::fill(literal_marks_.begin(), literal_marks_.end(), 0);
        literal_epoch_ = 1;
    }
    for (int lit : clause) {
        literal_marks_[static_cast<std::size_t>(lit_index(lit))] = literal_epoch_;
    }

    minimize_scratch_.clear();
    if (minimize_scratch_.capacity() < clause.size()) minimize_scratch_.reserve(clause.size());
    minimize_scratch_.push_back(clause[0]);

    for (std::size_t i = 1; i < clause.size(); ++i) {
        const int q = clause[i];
        const int v = var(q);
        const int rid = reasons_[static_cast<std::size_t>(v)];
        bool removable = rid >= 0;
        if (removable) {
            const Clause& reason = clauses_[static_cast<std::size_t>(rid)];
            const int assigned_lit = assigns_[static_cast<std::size_t>(v)] == 1 ? v : -v;
            bool found_pivot = false;
            for (std::size_t k = 0; k < static_cast<std::size_t>(reason.size); ++k) {
                const int r = clause_lit(reason, k);
                if (r == assigned_lit) {
                    found_pivot = true;
                    continue;
                }
                if (levels_[static_cast<std::size_t>(var(r))] != 0 &&
                    literal_marks_[static_cast<std::size_t>(lit_index(r))] != literal_epoch_) {
                    removable = false;
                    break;
                }
            }
            removable = removable && found_pivot;
        }

        if (removable) {
            literal_marks_[static_cast<std::size_t>(lit_index(q))] = 0;
        } else {
            minimize_scratch_.push_back(q);
        }
    }

    if (config_.binary_minimize) {
        std::uint64_t budget = config_.binary_minimize_budget;
        std::size_t out = 1;
        for (std::size_t i = 1; i < minimize_scratch_.size(); ++i) {
            const int q = minimize_scratch_[i];
            bool removable = false;
            for (const auto& edge : binary_watches_[static_cast<std::size_t>(lit_index(-q))]) {
                if (budget == 0) break;
                --budget; ++stats_.binary_edges_scanned;
                if (edge.implied != q && literal_marks_[static_cast<std::size_t>(lit_index(edge.implied))] == literal_epoch_) {
                    removable = true; break;
                }
            }
            if (removable) {
                literal_marks_[static_cast<std::size_t>(lit_index(q))] = 0;
                ++stats_.binary_literals_removed;
            } else minimize_scratch_[out++] = q;
        }
        minimize_scratch_.resize(out);
    }
    clause.assign(minimize_scratch_.begin(), minimize_scratch_.end());
}

void Solver::analyze(int conflict_clause, std::vector<int>& learnt, int& backtrack) {
    learnt.clear();
    learnt.push_back(0);
    analyze_touched_.clear();

    int path_count = 0;
    int p = 0;
    int cid = conflict_clause;
    std::size_t trail_index = trail_.size();

    do {
        if (cid < 0) throw std::logic_error("conflict analysis reached decision without resolving UIP");
        Clause& c = clauses_[static_cast<std::size_t>(cid)];
        if (c.deleted) throw std::logic_error("conflict analysis reached deleted clause");
        if (c.learnt) c.activity += clause_inc_;
        if (config_.dynamic_lbd && c.learnt && c.lbd > 2) {
            ++level_epoch_;
            if (level_epoch_ == 0) { std::fill(level_marks_.begin(), level_marks_.end(), 0); level_epoch_ = 1; }
            int distinct = 0;
            for (std::size_t k = 0; k < c.size; ++k) {
                const auto level = static_cast<std::size_t>(levels_[static_cast<std::size_t>(var(clause_lit(c,k)))]);
                if (level == 0 || level_marks_[level] == level_epoch_) continue;
                level_marks_[level] = level_epoch_; ++distinct;
            }
            if (distinct < c.lbd) { c.lbd = distinct; ++stats_.lbd_improvements; }
        }

        for (std::size_t i = 0; i < static_cast<std::size_t>(c.size); ++i) {
            const int q = clause_lit(c, i);
            const int v = var(q);
            if (p != 0 && v == var(p)) continue;
            if (analyze_seen_[static_cast<std::size_t>(v)] != 0 || levels_[static_cast<std::size_t>(v)] == 0) continue;
            analyze_seen_[static_cast<std::size_t>(v)] = 1;
            analyze_touched_.push_back(v);
            bump_variable(v);
            if (levels_[static_cast<std::size_t>(v)] == decision_level()) ++path_count;
            else learnt.push_back(q);
        }

        while (true) {
            if (trail_index == 0) throw std::logic_error("conflict analysis trail underflow");
            p = trail_[--trail_index];
            if (analyze_seen_[static_cast<std::size_t>(var(p))] != 0) break;
        }
        analyze_seen_[static_cast<std::size_t>(var(p))] = 0;
        --path_count;
        cid = reasons_[static_cast<std::size_t>(var(p))];
    } while (path_count > 0);

    learnt[0] = -p;
    minimize_clause(learnt);

    backtrack = 0;
    if (learnt.size() > 1) {
        std::size_t best = 1;
        for (std::size_t i = 1; i < learnt.size(); ++i) {
            const int level = levels_[static_cast<std::size_t>(var(learnt[i]))];
            if (level > backtrack) {
                backtrack = level;
                best = i;
            }
        }
        std::swap(learnt[1], learnt[best]);
    }

    for (int v : analyze_touched_) analyze_seen_[static_cast<std::size_t>(v)] = 0;
}

bool Solver::clause_locked(int cid) const noexcept {
    return clauses_[static_cast<std::size_t>(cid)].reason_refs != 0;
}

void Solver::compact_clause_arena() {
    if (dead_descriptors_ == 0) return;
    std::vector<int> remap(clauses_.size(), -1);
    std::vector<Clause> compact_clauses;
    std::vector<int> compact_literals;
    compact_clauses.reserve(clauses_.size() - dead_descriptors_);
    compact_literals.reserve(literal_arena_.size() - dead_literals_);
    stats_.descriptor_scans += clauses_.size();
    for (std::size_t cid = 0; cid < clauses_.size(); ++cid) {
        const Clause& c = clauses_[cid];
        if (c.deleted) continue;
        remap[cid] = static_cast<int>(compact_clauses.size());
        Clause copy = c; copy.offset = compact_literals.size();
        compact_literals.insert(compact_literals.end(), literal_arena_.begin() + static_cast<std::ptrdiff_t>(c.offset),
                                literal_arena_.begin() + static_cast<std::ptrdiff_t>(c.offset + c.size));
        compact_clauses.push_back(copy);
    }
    // Remove stale watches before IDs can be reused. Preserve live watch order.
    for (auto& list : watches_) {
        std::size_t out = 0;
        for (auto w : list) if (remap[static_cast<std::size_t>(w.clause)] >= 0) {
            w.clause = remap[static_cast<std::size_t>(w.clause)]; list[out++] = w;
        }
        list.resize(out);
    }
    for (auto& list : binary_watches_) for (auto& w : list) {
        w.clause = remap[static_cast<std::size_t>(w.clause)];
        if (w.clause < 0) throw std::logic_error("deleted binary clause during compaction");
    }
    for (int& reason : reasons_) if (reason >= 0) {
        reason = remap[static_cast<std::size_t>(reason)];
        if (reason < 0) throw std::logic_error("deleted locked reason during compaction");
    }
    for (int& cid : learnt_ids_) cid = remap[static_cast<std::size_t>(cid)];
    clauses_.swap(compact_clauses); literal_arena_.swap(compact_literals);
    dead_literals_ = 0; dead_descriptors_ = 0; ++stats_.descriptor_compactions;
}

void Solver::reduce_database() {
    std::vector<int> candidates;
    candidates.reserve(learnt_ids_.size());
    stats_.descriptor_scans += learnt_ids_.size();
    for (int cid : learnt_ids_) {
        const Clause& c = clauses_[static_cast<std::size_t>(cid)];
        if (c.size <= 2 || c.lbd <= 2 || clause_locked(cid)) continue;
        candidates.push_back(cid);
    }
    std::sort(candidates.begin(), candidates.end(), [this](int a, int b) {
        const Clause& ca = clauses_[static_cast<std::size_t>(a)];
        const Clause& cb = clauses_[static_cast<std::size_t>(b)];
        if (ca.lbd != cb.lbd) return ca.lbd > cb.lbd;
        if (ca.activity != cb.activity) return ca.activity < cb.activity;
        if (ca.size != cb.size) return ca.size > cb.size;
        return a < b;
    });
    std::vector<int> deleted;
    for (std::size_t i = 0; i < candidates.size()/2; ++i) {
        Clause& c = clauses_[static_cast<std::size_t>(candidates[i])];
        if (c.reason_refs != 0) throw std::logic_error("attempted deletion of locked reason");
        if (proof_enabled_) {
            deleted.clear();
            for (std::size_t k = 0; k < c.size; ++k) deleted.push_back(clause_lit(c, k));
            log_record(deleted, true);
        }
        c.deleted = true; ++dead_descriptors_; dead_literals_ += c.size;
        ++stats_.deleted_learnt_clauses;
    }
    learnt_ids_.erase(std::remove_if(learnt_ids_.begin(), learnt_ids_.end(), [this](int cid) {
        return clauses_[static_cast<std::size_t>(cid)].deleted;
    }), learnt_ids_.end());
    if (dead_descriptors_ >= 4096 && (dead_descriptors_ >= clauses_.size()/4 || dead_literals_ >= literal_arena_.size()/4)) compact_clause_arena();
}

void Solver::flush_proof() {
    if (proof_buffer_.empty()) return;
    proof_.write(proof_buffer_.data(), static_cast<std::streamsize>(proof_buffer_.size()));
    if (!proof_) throw std::runtime_error("proof output write failed");
    proof_buffer_.clear();
}

void Solver::append_proof_byte(char byte) {
    if (proof_buffer_.size() >= 65536) flush_proof();
    proof_buffer_.push_back(byte); ++stats_.proof_bytes;
}

void Solver::write_binary_literal(int lit) {
    const std::uint64_t magnitude = static_cast<std::uint64_t>(lit < 0 ? -static_cast<std::int64_t>(lit) : lit);
    std::uint64_t code = 2ULL*magnitude + (lit < 0 ? 1ULL : 0ULL);
    do {
        auto byte = static_cast<unsigned char>(code & 0x7fU); code >>= 7U;
        if (code != 0) byte = static_cast<unsigned char>(byte | 0x80U);
        append_proof_byte(static_cast<char>(byte));
    } while (code != 0);
}

void Solver::log_record(const std::vector<int>& clause, bool deletion) {
    if (!proof_enabled_) return;
    append_proof_byte(deletion ? 'd' : 'a');
    for (int lit : clause) write_binary_literal(lit);
    append_proof_byte('\0');
    if (proof_buffer_.size() >= 65536) flush_proof();
}
void Solver::log_clause(const std::vector<int>& clause) { log_record(clause, false); }
void Solver::log_empty() { log_record({}, false); }

std::uint64_t Solver::luby(std::uint64_t index) {
    std::uint64_t size = 1;
    std::uint64_t seq = 0;
    while (size < index + 1) {
        size = 2 * size + 1;
        ++seq;
    }
    while (size - 1 != index) {
        size = (size - 1) >> 1U;
        --seq;
        index %= size;
    }
    return 1ULL << seq;
}

SolveOutput Solver::solve(const Formula& formula, const std::optional<std::filesystem::path>& proof_path) {
    start_time_ = std::chrono::steady_clock::now();
    if (formula.variables < 0 || formula.variables > std::numeric_limits<int>::max()/2)
        throw std::invalid_argument("invalid formula variable count");
    for (const auto& clause : formula.clauses) for (int lit : clause) {
        const auto magnitude = lit < 0 ? -static_cast<std::int64_t>(lit) : static_cast<std::int64_t>(lit);
        if (magnitude == 0 || magnitude > formula.variables)
            throw std::invalid_argument("formula literal outside variable range");
    }
    if (proof_.is_open()) proof_.close();
    proof_.clear(); proof_enabled_ = false;
    reset(formula.variables);

    if (proof_path) {
        proof_.open(*proof_path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!proof_) throw std::runtime_error("cannot open proof output: " + proof_path->string());
        proof_enabled_ = true;
    } else {
        proof_enabled_ = false;
    }
    CongruenceResult congruent;
    SimplifyResult simplified;
    SimplifyResult eliminated;
    const Formula* input = &formula;
    if (config_.congruence) {
        congruent = simplify_congruence(formula, config_.congruence_budget,
            [this](const std::vector<int>& clause) { log_clause(clause); },
            [this]() { return timed_out(); });
        stats_.congruence_equivalences = congruent.equivalences;
        stats_.congruence_and_gates = congruent.and_gates;
        stats_.congruence_work = congruent.work;
        stats_.congruence_rounds = congruent.rounds;
        if (congruent.applied) {
            input = &congruent.formula;
            for (int v = 1; v <= nvars_; ++v)
                if (var(congruent.representatives[static_cast<std::size_t>(v)]) != v) decision_enabled_[static_cast<std::size_t>(v)] = 0;
        }
    }
    if (config_.bounded_eliminate) {
        eliminated = eliminate_bounded(*input, config_.eliminate_budget,
            [this](const std::vector<int>& clause) { log_clause(clause); },
            [this]() { return timed_out(); });
        if (eliminated.applied) {
            input = &eliminated.formula;
            stats_.eliminated_variables = eliminated.eliminations.size();
            stats_.elimination_work = eliminated.elimination_work;
            stats_.elimination_attempts = eliminated.elimination_attempts;
            stats_.tautological_resolvents = eliminated.tautological_resolvents;
            for (const auto& e : eliminated.eliminations) decision_enabled_[static_cast<std::size_t>(e.variable)] = 0;
        }
    }
    if (config_.simplify) {
        simplified = simplify_formula(*input, config_.simplify_budget,
            [this](const std::vector<int>& clause) { log_clause(clause); },
            [this]() { return timed_out(); });
        if (simplified.applied) {
            input = &simplified.formula;
            stats_.simplified_literals = simplified.removed_literals;
            stats_.eliminated_variables += simplified.eliminations.size();
            stats_.subsumed_clauses = simplified.subsumed;
            stats_.probed_units = simplified.probed_units;
            stats_.vivified_clauses = simplified.vivified;
            for (const auto& e : simplified.eliminations) decision_enabled_[static_cast<std::size_t>(e.variable)] = 0;
        }
    }
    std::size_t initial_literals = 0;
    for (const auto& clause : input->clauses) {
        if (clause.size() > std::numeric_limits<std::size_t>::max() - initial_literals) {
            throw std::length_error("formula literal count overflow");
        }
        initial_literals += clause.size();
    }
    literal_arena_.reserve(initial_literals + initial_literals / 4 + 1024);
    clauses_.reserve(input->clauses.size() + 1024);

    for (const auto& clause : input->clauses) {
        const int cid = add_clause(clause, false);
        if (cid >= 0) {
            const Clause& stored = clauses_[static_cast<std::size_t>(cid)];
            for (std::size_t i = 0; i < static_cast<std::size_t>(stored.size); ++i) {
                const int v = var(clause_lit(stored, i));
                activity_[static_cast<std::size_t>(v)] += 1.0;
            }
        }
        if (root_unsat_) break;
    }

    order_heap_.reset(activity_, heap_rank_);
    for (int v = 1; v <= nvars_; ++v) {
        if (decision_enabled_[static_cast<std::size_t>(v)] != 0) order_heap_.update(v);
    }

    SolveOutput output;
    if (root_unsat_) {
        log_empty();
        output.result = SolveResult::Unsat;
    } else {
        int conflict = propagate();
        if (conflict == -1 && config_.watched_vivify) conflict = vivify_database(config_.vivify_budget);
        if (conflict == -2) {
            output.result = SolveResult::Unknown;
        } else if (conflict >= 0) {
            log_empty();
            output.result = SolveResult::Unsat;
        } else {
            std::vector<int> learnt;
            learnt.reserve(64);

            while (true) {
                if (timed_out()) {
                    output.result = SolveResult::Unknown;
                    break;
                }

                conflict = propagate();
                if (conflict == -2) {
                    output.result = SolveResult::Unknown;
                    break;
                }

                if (conflict >= 0) {
                    ++stats_.conflicts;
                    ++conflicts_since_restart_;
                    if (decision_level() == 0) {
                        log_empty();
                        output.result = SolveResult::Unsat;
                        break;
                    }

                    int backtrack = 0;
                    analyze(conflict, learnt, backtrack);
                    const int lbd = compute_lbd(learnt);
                    log_clause(learnt);
                    cancel_until(backtrack);
                    const int learnt_id = add_clause(learnt, true, lbd);
                    if (learnt_id < 0 || learnt_id >= static_cast<int>(clauses_.size())) {
                        throw std::logic_error("invalid learnt clause insertion");
                    }
                    ++stats_.learnt_clauses;
                    Clause& stored = clauses_[static_cast<std::size_t>(learnt_id)];
                    if (!enqueue(clause_lit(stored, 0), learnt_id)) {
                        throw std::logic_error("asserting learnt clause did not assert");
                    }
                    decay_activities();

                    if (stats_.conflicts % static_cast<std::uint64_t>(config_.reduce_interval) == 0U) reduce_database();
                    if (conflicts_since_restart_ >= next_restart_) {
                        cancel_until(0);
                        ++stats_.restarts;
                        ++restart_index_;
                        conflicts_since_restart_ = 0;
                        next_restart_ = static_cast<std::uint64_t>(config_.restart_base) * luby(restart_index_);
                        if (config_.watched_vivify && stats_.conflicts >= next_vivify_) {
                            next_vivify_ *= 2;
                            conflict = propagate();
                            if (conflict == -1) conflict = vivify_database(config_.vivify_budget / 16);
                            if (conflict == -2) { output.result = SolveResult::Unknown; break; }
                            if (conflict >= 0) { log_empty(); output.result = SolveResult::Unsat; break; }
                        }
                    }
                    continue;
                }

                const int decision = pick_branch_lit();
                if (decision == 0) {
                    output.result = SolveResult::Sat;
                    output.model.reserve(static_cast<std::size_t>(nvars_));
                    for (int v = 1; v <= nvars_; ++v) {
                        const auto a = assigns_[static_cast<std::size_t>(v)];
                        output.model.push_back(a == 1 ? v : -v);
                    }
                    break;
                }

                ++stats_.decisions;
                trail_lim_.push_back(trail_.size());
                if (!enqueue(decision, -1)) throw std::logic_error("decision assignment conflict");
            }
        }
    }

    if (output.result == SolveResult::Sat && simplified.applied) extend_model(output.model, simplified.eliminations);
    if (output.result == SolveResult::Sat && eliminated.applied) extend_model(output.model, eliminated.eliminations);
    if (output.result == SolveResult::Sat && congruent.applied) restore_equivalent_model(output.model, congruent.representatives);
    if (proof_enabled_) {
        flush_proof(); proof_.flush();
        if (!proof_) throw std::runtime_error("proof output flush failed");
        proof_.close();
        if (proof_.fail()) throw std::runtime_error("proof output close failed");
        proof_enabled_ = false;
    }
    stats_.clause_descriptors = clauses_.size();
    stats_.live_clauses = clauses_.size() - dead_descriptors_;
    stats_.solve_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count();
    output.stats = stats_;
    return output;
}

} // namespace satsolv

