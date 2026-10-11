#include "satsolv/solver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_set>

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

bool Solver::timed_out() const {
    if (config_.time_limit_seconds <= 0.0) return false;
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count();
    return elapsed >= config_.time_limit_seconds;
}

void Solver::reset(int variables) {
    nvars_ = variables;
    stats_ = {};
    clauses_.clear();
    watches_.assign(static_cast<std::size_t>(2 * variables), {});
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
    root_unsat_ = false;
    restart_index_ = 1;
    conflicts_since_restart_ = 0;
    next_restart_ = static_cast<std::uint64_t>(config_.restart_base) * luby(restart_index_);
    for (int v = 1; v <= variables; ++v) {
        order_heap_.push(HeapEntry{0.0, v, 0});
    }
}

int Solver::add_clause(std::vector<int> lits, bool learnt, int lbd) {
    if (!learnt) {
        std::sort(lits.begin(), lits.end(), [](int a, int b) {
            const int va = a < 0 ? -a : a;
            const int vb = b < 0 ? -b : b;
            if (va != vb) return va < vb;
            return a < b;
        });
        std::vector<int> normalized;
        normalized.reserve(lits.size());
        int previous = 0;
        bool have_previous = false;
        for (int lit : lits) {
            if (have_previous && lit == previous) continue;
            if (have_previous && var(lit) == var(previous) && lit == -previous) {
                return -1;
            }
            normalized.push_back(lit);
            previous = lit;
            have_previous = true;
        }
        lits.swap(normalized);
    }

    const int cid = static_cast<int>(clauses_.size());
    Clause clause;
    clause.lits = std::move(lits);
    clause.learnt = learnt;
    clause.lbd = lbd;
    clause.watch0 = 0;
    clause.watch1 = clause.lits.size() > 1 ? 1 : 0;
    clauses_.push_back(std::move(clause));

    Clause& c = clauses_.back();
    if (c.lits.empty()) {
        root_unsat_ = true;
        return cid;
    }
    if (c.lits.size() == 1) {
        if (!enqueue(c.lits[0], cid)) root_unsat_ = true;
        return cid;
    }
    watches_[static_cast<std::size_t>(lit_index(c.lits[0]))].push_back(cid);
    watches_[static_cast<std::size_t>(lit_index(c.lits[1]))].push_back(cid);
    return cid;
}

bool Solver::enqueue(int lit, int reason) {
    const int v = var(lit);
    const int current = value_lit(lit);
    if (current == 1) return true;
    if (current == 0) return false;
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
        auto& ws = watches_[static_cast<std::size_t>(lit_index(-p))];
        std::size_t i = 0;
        while (i < ws.size()) {
            const int cid = ws[i];
            Clause& c = clauses_[static_cast<std::size_t>(cid)];
            if (c.deleted) {
                ws[i] = ws.back();
                ws.pop_back();
                continue;
            }

            int false_slot = -1;
            int other_slot = -1;
            if (c.lits[static_cast<std::size_t>(c.watch0)] == -p) {
                false_slot = c.watch0;
                other_slot = c.watch1;
            } else if (c.lits[static_cast<std::size_t>(c.watch1)] == -p) {
                false_slot = c.watch1;
                other_slot = c.watch0;
            } else {
                ws[i] = ws.back();
                ws.pop_back();
                continue;
            }

            const int other_lit = c.lits[static_cast<std::size_t>(other_slot)];
            if (value_lit(other_lit) == 1) {
                ++i;
                continue;
            }

            bool moved = false;
            for (int k = 0; k < static_cast<int>(c.lits.size()); ++k) {
                if (k == c.watch0 || k == c.watch1) continue;
                const int candidate = c.lits[static_cast<std::size_t>(k)];
                if (value_lit(candidate) != 0) {
                    if (false_slot == c.watch0) c.watch0 = k;
                    else c.watch1 = k;
                    ws[i] = ws.back();
                    ws.pop_back();
                    watches_[static_cast<std::size_t>(lit_index(candidate))].push_back(cid);
                    moved = true;
                    break;
                }
            }
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
    for (int v = 1; v <= nvars_; ++v) {
        ++heap_stamp_[static_cast<std::size_t>(v)];
        order_heap_.push(HeapEntry{activity_[static_cast<std::size_t>(v)], v, heap_stamp_[static_cast<std::size_t>(v)]});
    }
}

void Solver::bump_variable(int variable) {
    activity_[static_cast<std::size_t>(variable)] += var_inc_;
    if (activity_[static_cast<std::size_t>(variable)] > 1e100) rescale_activities();
    ++heap_stamp_[static_cast<std::size_t>(variable)];
    order_heap_.push(HeapEntry{activity_[static_cast<std::size_t>(variable)], variable, heap_stamp_[static_cast<std::size_t>(variable)]});
}

void Solver::decay_activities() {
    var_inc_ /= config_.var_decay;
}

int Solver::compute_lbd(const std::vector<int>& clause) const {
    std::unordered_set<int> levels;
    levels.reserve(clause.size());
    for (int lit : clause) levels.insert(levels_[static_cast<std::size_t>(var(lit))]);
    return static_cast<int>(levels.size());
}

void Solver::minimize_clause(std::vector<int>& clause) {
    if (clause.size() <= 2) return;
    std::unordered_set<int> present(clause.begin(), clause.end());
    std::vector<int> kept;
    kept.reserve(clause.size());
    kept.push_back(clause[0]);

    for (std::size_t i = 1; i < clause.size(); ++i) {
        const int q = clause[i];
        const int v = var(q);
        const int rid = reasons_[static_cast<std::size_t>(v)];
        bool removable = rid >= 0;
        if (removable) {
            const Clause& reason = clauses_[static_cast<std::size_t>(rid)];
            const int assigned_lit = assigns_[static_cast<std::size_t>(v)] == 1 ? v : -v;
            bool found_pivot = false;
            for (int r : reason.lits) {
                if (r == assigned_lit) {
                    found_pivot = true;
                    continue;
                }
                if (present.find(r) == present.end()) {
                    removable = false;
                    break;
                }
            }
            removable = removable && found_pivot;
        }
        if (removable) {
            present.erase(q);
        } else {
            kept.push_back(q);
        }
    }
    clause.swap(kept);
}

std::pair<std::vector<int>, int> Solver::analyze(int conflict_clause) {
    std::vector<int> learnt(1, 0);
    std::vector<bool> seen(static_cast<std::size_t>(nvars_ + 1), false);
    int path_count = 0;
    int p = 0;
    int cid = conflict_clause;
    std::size_t trail_index = trail_.size();

    do {
        if (cid < 0) throw std::logic_error("conflict analysis reached decision without resolving UIP");
        Clause& c = clauses_[static_cast<std::size_t>(cid)];
        c.activity += 1.0;
        for (int q : c.lits) {
            const int v = var(q);
            if (p != 0 && v == var(p)) continue;
            if (seen[static_cast<std::size_t>(v)] || levels_[static_cast<std::size_t>(v)] == 0) continue;
            seen[static_cast<std::size_t>(v)] = true;
            bump_variable(v);
            if (levels_[static_cast<std::size_t>(v)] == decision_level()) ++path_count;
            else learnt.push_back(q);
        }

        while (true) {
            if (trail_index == 0) throw std::logic_error("conflict analysis trail underflow");
            p = trail_[--trail_index];
            if (seen[static_cast<std::size_t>(var(p))]) break;
        }
        seen[static_cast<std::size_t>(var(p))] = false;
        --path_count;
        cid = reasons_[static_cast<std::size_t>(var(p))];
    } while (path_count > 0);

    learnt[0] = -p;
    minimize_clause(learnt);

    int backtrack = 0;
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
    return {std::move(learnt), backtrack};
}

bool Solver::clause_locked(int cid) const {
    for (int v = 1; v <= nvars_; ++v) {
        if (assigns_[static_cast<std::size_t>(v)] != -1 && reasons_[static_cast<std::size_t>(v)] == cid) return true;
    }
    return false;
}

void Solver::reduce_database() {
    std::vector<int> candidates;
    for (int cid = 0; cid < static_cast<int>(clauses_.size()); ++cid) {
        const Clause& c = clauses_[static_cast<std::size_t>(cid)];
        if (!c.learnt || c.deleted || c.lits.size() <= 2 || c.lbd <= 2 || clause_locked(cid)) continue;
        candidates.push_back(cid);
    }
    std::sort(candidates.begin(), candidates.end(), [this](int a, int b) {
        const Clause& ca = clauses_[static_cast<std::size_t>(a)];
        const Clause& cb = clauses_[static_cast<std::size_t>(b)];
        if (ca.lbd != cb.lbd) return ca.lbd > cb.lbd;
        if (ca.activity != cb.activity) return ca.activity < cb.activity;
        return ca.lits.size() > cb.lits.size();
    });
    const std::size_t remove_count = candidates.size() / 2;
    for (std::size_t i = 0; i < remove_count; ++i) {
        clauses_[static_cast<std::size_t>(candidates[i])].deleted = true;
        ++stats_.deleted_learnt_clauses;
    }
}

void Solver::log_clause(const std::vector<int>& clause) {
    if (!proof_enabled_) return;
    const std::streampos before = proof_.tellp();
    for (int lit : clause) proof_ << lit << ' ';
    proof_ << "0\n";
    const std::streampos after = proof_.tellp();
    if (before >= 0 && after >= before) stats_.proof_bytes += static_cast<std::uint64_t>(after - before);
}

void Solver::log_empty() {
    if (!proof_enabled_) return;
    const std::streampos before = proof_.tellp();
    proof_ << "0\n";
    const std::streampos after = proof_.tellp();
    if (before >= 0 && after >= before) stats_.proof_bytes += static_cast<std::uint64_t>(after - before);
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
    if (proof_path) {
        proof_.open(*proof_path, std::ios::out | std::ios::trunc);
        if (!proof_) throw std::runtime_error("cannot open proof output: " + proof_path->string());
        proof_enabled_ = true;
    } else {
        proof_enabled_ = false;
    }
    start_time_ = std::chrono::steady_clock::now();

    for (const auto& clause : formula.clauses) {
        const int cid = add_clause(clause, false);
        if (cid >= 0) {
            for (int lit : clauses_[static_cast<std::size_t>(cid)].lits) {
                const int v = var(lit);
                activity_[static_cast<std::size_t>(v)] += 1.0;
                ++heap_stamp_[static_cast<std::size_t>(v)];
                order_heap_.push(HeapEntry{activity_[static_cast<std::size_t>(v)], v, heap_stamp_[static_cast<std::size_t>(v)]});
            }
        }
        if (root_unsat_) break;
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
                    auto [learnt, backtrack] = analyze(conflict);
                    const int lbd = compute_lbd(learnt);
                    log_clause(learnt);
                    cancel_until(backtrack);
                    const int learnt_id = add_clause(learnt, true, lbd);
                    if (learnt_id < 0 || learnt_id >= static_cast<int>(clauses_.size())) {
                        throw std::logic_error("invalid learnt clause insertion");
                    }
                    ++stats_.learnt_clauses;
                    if (!enqueue(clauses_[static_cast<std::size_t>(learnt_id)].lits[0], learnt_id)) {
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
