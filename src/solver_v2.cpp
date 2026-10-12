#include "satsolv/solver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace satsolv {

Solver::Solver(SolverConfig config) : config_(config) {
    if (!(config_.var_decay > 0.0 && config_.var_decay < 1.0)) {
        throw std::invalid_argument("var_decay must be in (0,1)");
    }
    if (config_.restart_base <= 0 || config_.reduce_interval <= 0 || config_.time_limit_seconds < 0.0) {
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
    quality_.clear();
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
    heap_stamp_.assign(static_cast<std::size_t>(variables + 1), 0);
    saved_phase_.assign(static_cast<std::size_t>(variables + 1), static_cast<std::int8_t>(1));
    order_heap_ = {};
    var_inc_ = 1.0;

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
    for (int v = 1; v <= variables; ++v) {
        order_heap_.push(HeapEntry{0.0, v, 0});
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
    ClauseQuality quality;
    quality.created = quality.touched = stats_.conflicts;
    if (learnt) set_clause_tier(quality,
        lbd <= 2 || clause.size <= 2 ? ClauseTier::Core : ClauseTier::Local);
    quality_.push_back(quality);
    if (clause.size >= 2) stats_.live_watchers += 2;

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
    while (qhead_ < trail_.size()) {
        const int p = trail_[qhead_++];
        ++stats_.propagations;
        const std::size_t false_index = static_cast<std::size_t>(lit_index(-p));

        const auto& binary = binary_watches_[false_index];
        for (const BinaryWatcher& watcher : binary) {
            const Clause& c = clauses_[static_cast<std::size_t>(watcher.clause)];
            if (c.deleted) continue;
            const int value = value_lit(watcher.implied);
            if (value == 0) return watcher.clause;
            if (value < 0 && !enqueue(watcher.implied, watcher.clause)) return watcher.clause;
        }

        auto& ws = watches_[false_index];
        std::size_t i = 0;
        while (i < ws.size()) {
            const int cid = ws[i].clause;
            Clause& c = clauses_[static_cast<std::size_t>(cid)];
            if (c.deleted) {
                ws[i] = ws.back();
                ws.pop_back();
                continue;
            }

            if (value_lit(ws[i].blocker) == 1) {
                ++i;
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
            std::size_t start = static_cast<std::size_t>(c.scan_cursor);
            if (start < 2 || start >= static_cast<std::size_t>(c.size)) start = 2;

            auto try_range = [&](std::size_t begin, std::size_t end) {
                for (std::size_t k = begin; k < end; ++k) {
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
            if (!moved && start > 2) try_range(2, start);
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
        ++heap_stamp_[static_cast<std::size_t>(v)];
        order_heap_.push(HeapEntry{activity_[static_cast<std::size_t>(v)], v, heap_stamp_[static_cast<std::size_t>(v)]});
    }
    trail_.resize(target);
    trail_lim_.resize(static_cast<std::size_t>(level));
    qhead_ = std::min(qhead_, trail_.size());
}

int Solver::pick_branch_lit() {
    while (!order_heap_.empty()) {
        const HeapEntry top = order_heap_.top();
        order_heap_.pop();
        const int v = top.variable;
        if (assigns_[static_cast<std::size_t>(v)] != -1) continue;
        if (top.stamp != heap_stamp_[static_cast<std::size_t>(v)]) continue;
        const double current = activity_[static_cast<std::size_t>(v)];
        const double tolerance = std::max(1.0, std::abs(current)) * 1e-12;
        if (std::abs(top.activity - current) > tolerance) continue;
        const bool positive = saved_phase_[static_cast<std::size_t>(v)] != 0;
        return positive ? v : -v;
    }
    for (int v = 1; v <= nvars_; ++v) {
        if (assigns_[static_cast<std::size_t>(v)] == -1) return v;
    }
    return 0;
}

void Solver::rescale_activities() {
    for (int v = 1; v <= nvars_; ++v) activity_[static_cast<std::size_t>(v)] *= 1e-100;
    var_inc_ *= 1e-100;
    order_heap_ = {};
    for (int v = 1; v <= nvars_; ++v) {
        ++heap_stamp_[static_cast<std::size_t>(v)];
        order_heap_.push(HeapEntry{activity_[static_cast<std::size_t>(v)], v, heap_stamp_[static_cast<std::size_t>(v)]});
    }
}

void Solver::bump_variable(int variable) {
    const std::size_t heap_limit = std::max<std::size_t>(4096U, static_cast<std::size_t>(nvars_ + 1) * 4U);
    if (order_heap_.size() > heap_limit) {
        order_heap_ = {};
        for (int v = 1; v <= nvars_; ++v) {
            order_heap_.push(HeapEntry{activity_[static_cast<std::size_t>(v)], v, heap_stamp_[static_cast<std::size_t>(v)]});
        }
    }
    activity_[static_cast<std::size_t>(variable)] += var_inc_;
    if (activity_[static_cast<std::size_t>(variable)] > 1e100) rescale_activities();
    ++heap_stamp_[static_cast<std::size_t>(variable)];
    order_heap_.push(HeapEntry{activity_[static_cast<std::size_t>(variable)], variable, heap_stamp_[static_cast<std::size_t>(variable)]});
}

void Solver::decay_activities() {
    var_inc_ /= config_.var_decay;
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
                if (literal_marks_[static_cast<std::size_t>(lit_index(r))] != literal_epoch_) {
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
        c.activity += 1.0;

        // Fuse dynamic LBD with the existing analysis traversal, avoiding
        // a second scan and avoiding writes in the BCP hot path.
        const bool track_lbd = c.learnt && c.lbd > 2;
        int current_lbd = 0;
        if (track_lbd && ++level_epoch_ == 0) {
            std::fill(level_marks_.begin(), level_marks_.end(), 0);
            level_epoch_ = 1;
        }

        for (std::size_t i = 0; i < static_cast<std::size_t>(c.size); ++i) {
            const int q = clause_lit(c, i);
            const int v = var(q);
            if (track_lbd) {
                const auto level = static_cast<std::size_t>(levels_[static_cast<std::size_t>(v)]);
                if (level_marks_[level] != level_epoch_) {
                    level_marks_[level] = level_epoch_;
                    ++current_lbd;
                }
            }
            if (p != 0 && v == var(p)) continue;
            if (analyze_seen_[static_cast<std::size_t>(v)] != 0 || levels_[static_cast<std::size_t>(v)] == 0) continue;
            analyze_seen_[static_cast<std::size_t>(v)] = 1;
            analyze_touched_.push_back(v);
            bump_variable(v);
            if (levels_[static_cast<std::size_t>(v)] == decision_level()) ++path_count;
            else learnt.push_back(q);
        }

        if (c.learnt) refresh_clause_quality(cid, track_lbd ? current_lbd : c.lbd);

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
    if (dead_literals_ == 0) return;

    std::vector<int> compact;
    const std::size_t live_literals = literal_arena_.size() >= dead_literals_ ? literal_arena_.size() - dead_literals_ : 0;
    compact.reserve(live_literals);

    for (Clause& c : clauses_) {
        if (c.deleted) {
            c.offset = 0;
            c.size = 0;
            c.scan_cursor = 2;
            continue;
        }
        const std::size_t old_offset = c.offset;
        c.offset = compact.size();
        for (std::size_t i = 0; i < static_cast<std::size_t>(c.size); ++i) {
            compact.push_back(literal_arena_[old_offset + i]);
        }
    }

    literal_arena_.swap(compact);
    dead_literals_ = 0;
}

void Solver::refresh_clause_quality(int cid, int current_lbd) {
    Clause& c = clauses_[static_cast<std::size_t>(cid)];
    ClauseQuality& q = quality_[static_cast<std::size_t>(cid)];
    if (!c.learnt || c.deleted) return;
    if (q.touched != stats_.conflicts) {
        if (q.uses < 2) ++q.uses;
        if (q.tier == ClauseTier::Tier2) q.tier2_reused = true;
    }
    q.touched = stats_.conflicts;
    const bool improved = current_lbd < c.lbd;
    if (improved) c.lbd = current_lbd;
    const ClauseTier target = c.lbd <= 2 || c.size <= 2 ? ClauseTier::Core :
        (c.lbd <= 4 && (improved || q.uses >= 2) ? ClauseTier::Tier2 : ClauseTier::Local);
    if (target != q.tier) {
        if (target == ClauseTier::Core || target == ClauseTier::Tier2) ++stats_.promotions;
        else ++stats_.demotions;
        set_clause_tier(q, target);
    }
    if (target == ClauseTier::Tier2) q.was_tier2 = true;
}

void Solver::set_clause_tier(ClauseQuality& q, ClauseTier tier) {
    auto count = [this](ClauseTier t) -> std::uint64_t* {
        switch (t) {
            case ClauseTier::Core: return &stats_.core_count;
            case ClauseTier::Tier2: return &stats_.tier2_count;
            case ClauseTier::Local: return &stats_.local_count;
            case ClauseTier::Original: return nullptr;
        }
        return nullptr;
    };
    if (auto* old_count = count(q.tier)) --*old_count;
    if (auto* new_count = count(tier)) ++*new_count;
    q.tier = tier;
}

void Solver::snapshot_database_stats() {
    stats_.peak_live_learnt = std::max(stats_.peak_live_learnt,
        stats_.core_count + stats_.tier2_count + stats_.local_count);
    stats_.peak_tier2 = std::max(stats_.peak_tier2, stats_.tier2_count);
    stats_.peak_arena_literals = std::max(stats_.peak_arena_literals,
        static_cast<std::uint64_t>(literal_arena_.size()));
    stats_.peak_live_watchers = std::max(stats_.peak_live_watchers, stats_.live_watchers);
    stats_.database_bytes = static_cast<std::uint64_t>(
        clauses_.capacity() * sizeof(Clause) + quality_.capacity() * sizeof(ClauseQuality) +
        literal_arena_.capacity() * sizeof(int));
}

void Solver::reduce_database() {
    std::vector<int> candidates, tier2;
    const auto interval = static_cast<std::uint64_t>(config_.reduce_interval);
    std::size_t eligible = 0;
    ++stats_.reductions;
    snapshot_database_stats();
    for (int cid = 0; cid < static_cast<int>(clauses_.size()); ++cid) {
        const Clause& c = clauses_[static_cast<std::size_t>(cid)];
        if (!c.learnt || c.deleted || c.size <= 2 || c.lbd <= 2) continue;
        ClauseQuality& q = quality_[static_cast<std::size_t>(cid)];
        // Staleness applies even while a clause is locked. Locking protects
        // deletion, not indefinite TIER2 membership.
        if (q.tier == ClauseTier::Tier2 && stats_.conflicts - q.touched >= interval) {
            set_clause_tier(q, ClauseTier::Local);
            q.uses = 0;
            ++stats_.demotions;
        }
        if (clause_locked(cid)) continue;
        ++eligible;
        if (q.tier == ClauseTier::Tier2) tier2.push_back(cid);
        else candidates.push_back(cid);
    }

    auto worse = [this](int a, int b) {
        const Clause& ca = clauses_[static_cast<std::size_t>(a)];
        const Clause& cb = clauses_[static_cast<std::size_t>(b)];
        const ClauseQuality& qa = quality_[static_cast<std::size_t>(a)];
        const ClauseQuality& qb = quality_[static_cast<std::size_t>(b)];
        if (ca.lbd != cb.lbd) return ca.lbd > cb.lbd;
        if (qa.touched != qb.touched) return qa.touched < qb.touched;
        if (ca.activity != cb.activity) return ca.activity < cb.activity;
        if (qa.created != qb.created) return qa.created < qb.created;
        if (ca.size != cb.size) return ca.size > cb.size;
        return a < b;
    };
    std::sort(tier2.begin(), tier2.end(), worse);
    // Protect at most R and one quarter of the eligible pool. Thus LOCAL
    // always contains enough clauses to restore Core-v2 half-pool deletion.
    const std::size_t cap = std::min(static_cast<std::size_t>(interval), eligible / 4);
    const std::size_t excess = tier2.size() > cap ? tier2.size() - cap : 0;
    for (std::size_t i = 0; i < excess; ++i) {
        ClauseQuality& q = quality_[static_cast<std::size_t>(tier2[i])];
        set_clause_tier(q, ClauseTier::Local);
        q.uses = 0;
        ++stats_.demotions;
        candidates.push_back(tier2[i]);
    }
    std::sort(candidates.begin(), candidates.end(), worse);
    const std::size_t remove_count = eligible / 2;
    if (candidates.size() < remove_count) throw std::logic_error("retention cap violated");
    for (std::size_t i = 0; i < remove_count; ++i) {
        const auto cid = static_cast<std::size_t>(candidates[i]);
        Clause& c = clauses_[cid];
        ClauseQuality& q = quality_[cid];
        if (c.reason_refs != 0 || c.deleted || c.size <= 2 || c.lbd <= 2)
            throw std::logic_error("protected clause selected for deletion");
        c.deleted = true;
        dead_literals_ += static_cast<std::size_t>(c.size);
        ++stats_.deleted_learnt_clauses;
        if (q.tier == ClauseTier::Core) ++stats_.deleted_core;
        else if (q.tier == ClauseTier::Tier2) ++stats_.deleted_tier2;
        else ++stats_.deleted_local;
        if (q.was_tier2 && q.tier2_reused) ++stats_.tier2_reused_before_deletion;
        stats_.deletion_age_sum += stats_.conflicts - q.created;
        stats_.live_watchers -= 2;
        set_clause_tier(q, ClauseTier::Original);
    }
    snapshot_database_stats();
    const std::size_t compact_floor = 4096;
    if (dead_literals_ >= compact_floor && dead_literals_ >= literal_arena_.size() / 4) {
        compact_clause_arena();
    }
}

void Solver::write_binary_literal(std::ofstream& out, int lit, std::uint64_t& byte_counter) {
    const std::uint64_t magnitude = static_cast<std::uint64_t>(lit < 0 ? -static_cast<std::int64_t>(lit) : lit);
    std::uint64_t code = 2ULL * magnitude + (lit < 0 ? 1ULL : 0ULL);
    do {
        unsigned char byte = static_cast<unsigned char>(code & 0x7fU);
        code >>= 7U;
        if (code != 0U) byte = static_cast<unsigned char>(byte | 0x80U);
        out.put(static_cast<char>(byte));
        ++byte_counter;
    } while (code != 0U);
}

void Solver::log_clause(const std::vector<int>& clause) {
    if (!proof_enabled_) return;
    proof_.put('a');
    ++stats_.proof_bytes;
    for (int lit : clause) write_binary_literal(proof_, lit, stats_.proof_bytes);
    proof_.put('\0');
    ++stats_.proof_bytes;
}

void Solver::log_empty() {
    if (!proof_enabled_) return;
    proof_.put('a');
    proof_.put('\0');
    stats_.proof_bytes += 2;
}

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
    reset(formula.variables);

    std::size_t initial_literals = 0;
    for (const auto& clause : formula.clauses) {
        if (clause.size() > std::numeric_limits<std::size_t>::max() - initial_literals) {
            throw std::length_error("formula literal count overflow");
        }
        initial_literals += clause.size();
    }
    literal_arena_.reserve(initial_literals + initial_literals / 4 + 1024);
    clauses_.reserve(formula.clauses.size() + 1024);
    quality_.reserve(formula.clauses.size() + 1024);

    if (proof_path) {
        proof_.open(*proof_path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!proof_) throw std::runtime_error("cannot open proof output: " + proof_path->string());
        proof_enabled_ = true;
    } else {
        proof_enabled_ = false;
    }
    start_time_ = std::chrono::steady_clock::now();

    for (const auto& clause : formula.clauses) {
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

    order_heap_ = {};
    for (int v = 1; v <= nvars_; ++v) {
        order_heap_.push(HeapEntry{activity_[static_cast<std::size_t>(v)], v, heap_stamp_[static_cast<std::size_t>(v)]});
    }

    SolveOutput output;
    if (root_unsat_) {
        log_empty();
        output.result = SolveResult::Unsat;
    } else {
        int conflict = propagate();
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

    snapshot_database_stats();
    stats_.solve_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count();
    if (proof_enabled_) {
        proof_.flush();
        proof_.close();
        proof_enabled_ = false;
    }
    output.stats = stats_;
    return output;
}

} // namespace satsolv

