#include "satsolv/simplifier.hpp"
#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace satsolv {
namespace {
int variable(int lit) noexcept { return lit < 0 ? -lit : lit; }
std::size_t index(int lit) noexcept { return 2U*static_cast<std::size_t>(variable(lit)-1) + (lit < 0 ? 1U : 0U); }
bool less_lit(int a, int b) noexcept {
    return variable(a) != variable(b) ? variable(a) < variable(b) : a < b;
}
bool normalize(std::vector<int>& c) {
    std::sort(c.begin(), c.end(), less_lit);
    c.erase(std::unique(c.begin(), c.end()), c.end());
    for (std::size_t i = 1; i < c.size(); ++i) if (c[i-1] == -c[i]) return false;
    return true;
}
struct Budget {
    std::uint64_t left;
    const std::function<bool()>& interrupted;
    std::uint64_t ticks = 0;
    bool take() {
        if (left == 0) return false;
        --left;
        if ((++ticks & 1023U) == 0U && interrupted()) { left = 0; return false; }
        return true;
    }
};
// -1 exhausted; 0 consistent; 1 conflict. Only conflict is a proof signal.
int propagate(const std::vector<std::vector<int>>& clauses, const std::vector<std::uint8_t>& live,
              std::vector<std::int8_t>& values, Budget& work) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < clauses.size(); ++i) {
            if (live[i] == 0) continue;
            int unit = 0; int unassigned = 0; bool satisfied = false;
            for (int lit : clauses[i]) {
                if (!work.take()) return -1;
                const auto val = values[static_cast<std::size_t>(variable(lit))];
                if (val < 0) { ++unassigned; unit = lit; }
                else if ((val != 0) == (lit > 0)) { satisfied = true; break; }
            }
            if (satisfied) continue;
            if (unassigned == 0) return 1;
            if (unassigned == 1) {
                values[static_cast<std::size_t>(variable(unit))] = static_cast<std::int8_t>(unit > 0 ? 1 : 0);
                changed = true;
            }
        }
    }
    return 0;
}
}

SimplifyResult simplify_formula(const Formula& input, std::uint64_t budget,
    const std::function<void(const std::vector<int>&)>& emit,
    const std::function<bool()>& interrupted) {
    SimplifyResult result;
    // Copying large CNFs can dominate the 4-GiB contract. Use the original engine
    // when a bounded workspace would not fit; this is a complete, safe fallback.
    std::uint64_t original_literals = 0;
    for (const auto& c : input.clauses) original_literals += c.size();
    if (budget == 0 || original_literals > 2000000 || input.variables > 200000 || input.clauses.size() > 1000000 || interrupted()) return result;
    result.applied = true; result.formula.variables = input.variables;
    auto& clauses = result.formula.clauses;
    clauses.reserve(input.clauses.size());
    for (auto c : input.clauses) if (normalize(c)) clauses.push_back(std::move(c));
    std::vector<std::uint8_t> live(clauses.size(), 1);
    std::vector<std::int8_t> roots(static_cast<std::size_t>(input.variables+1), -1);
    Budget work{budget/4, interrupted};
    const int root_result = propagate(clauses, live, roots, work);
    if (root_result == 1) { emit({}); clauses = {{}}; return result; }
    // Even a prefix of sound unit propagation is usable. Log all discovered
    // units against the original formula before rewriting any working clause.
    for (int v = 1; v <= input.variables; ++v) {
        if (roots[static_cast<std::size_t>(v)] >= 0) emit({roots[static_cast<std::size_t>(v)] != 0 ? v : -v});
    }
    const auto root_clause_count = clauses.size();
    for (std::size_t i = 0; i < root_clause_count; ++i) {
        auto& c = clauses[i]; std::vector<int> shortened; bool satisfied = false;
        for (int lit : c) {
            const auto val = roots[static_cast<std::size_t>(variable(lit))];
            if (val < 0) shortened.push_back(lit);
            else if ((val != 0) == (lit > 0)) { satisfied = true; break; }
        }
        if (satisfied) { live[i] = 0; continue; }
        if (shortened.size() != c.size()) { emit(shortened); c.swap(shortened); }
    }
    for (int v = 1; v <= input.variables; ++v) if (roots[static_cast<std::size_t>(v)] >= 0) {
        clauses.push_back({roots[static_cast<std::size_t>(v)] != 0 ? v : -v}); live.push_back(1);
    }
    // Bounded failed-literal probing and prefix vivification. Assumptions never
    // become persistent roots unless a complete propagation proves conflict.
    work.left = budget/8;
    for (int v = 1; v <= std::min(input.variables, 32) && work.left > 0; ++v) {
        if (roots[static_cast<std::size_t>(v)] >= 0) continue;
        for (int sign : {-1, 1}) {
            auto values = roots;
            values[static_cast<std::size_t>(v)] = static_cast<std::int8_t>(sign > 0 ? 1 : 0);
            const int rc = propagate(clauses, live, values, work);
            if (rc < 0) break;
            if (rc == 1) {
                const int unit = -sign*v; emit({unit});
                roots[static_cast<std::size_t>(v)] = static_cast<std::int8_t>(unit > 0 ? 1 : 0);
                clauses.push_back({unit}); live.push_back(1); ++result.probed_units; break;
            }
        }
    }
    work.left = budget/8;
    const auto before_vivify = clauses.size();
    for (std::size_t i = 0; i < std::min<std::size_t>(before_vivify, 128) && work.left > 0; ++i) {
        if (live[i] == 0 || clauses[i].size() < 2 || clauses[i].size() > 8) continue;
        auto values = roots; std::vector<int> prefix;
        for (int lit : clauses[i]) {
            prefix.push_back(lit);
            const auto old = values[static_cast<std::size_t>(variable(lit))];
            int rc = 0;
            if (old >= 0 && ((old != 0) == (lit > 0))) rc = 1;
            else {
                values[static_cast<std::size_t>(variable(lit))] = static_cast<std::int8_t>(lit < 0 ? 1 : 0);
                rc = propagate(clauses, live, values, work);
            }
            if (rc < 0) break;
            if (rc == 1) {
                if (prefix.size() < clauses[i].size()) { emit(prefix); clauses[i] = prefix; ++result.vivified; }
                break;
            }
        }
    }
    std::vector<std::vector<int>> occurrences(static_cast<std::size_t>(2*input.variables));
    for (std::size_t i = 0; i < clauses.size(); ++i) if (live[i] != 0)
        for (int lit : clauses[i]) occurrences[index(lit)].push_back(static_cast<int>(i));
    // Index candidates by one literal. Only live clauses can subsume; equal
    // duplicates are removed in ID order, retaining one support clause.
    work.left = budget/4;
    for (std::size_t i = 0; i < clauses.size() && work.left > 0; ++i) {
        if (live[i] == 0 || clauses[i].empty() || clauses[i].size() > 12) continue;
        int rare = clauses[i][0];
        for (int lit : clauses[i]) if (occurrences[index(lit)].size() < occurrences[index(rare)].size()) rare = lit;
        for (int id : occurrences[index(rare)]) {
            if (!work.take()) break;
            const auto j = static_cast<std::size_t>(id);
            if (i == j || live[j] == 0 || clauses[i].size() > clauses[j].size()) continue;
            bool exhausted = false;
            for (std::size_t k = 0; k < clauses[j].size(); ++k) if (!work.take()) { exhausted = true; break; }
            if (exhausted) break;
            if (std::includes(clauses[j].begin(), clauses[j].end(), clauses[i].begin(), clauses[i].end(), less_lit)) {
                live[j] = 0; ++result.subsumed;
            }
        }
    }
    // One pass of non-growing bounded variable elimination. All resolvents are
    // emitted before any parent leaves the working formula. Parent proof
    // records are deliberately retained, so reconstruction is independently
    // checked against the original CNF and RUP sees every supporting clause.
    work.left = budget/4;
    for (int v = 1; v <= input.variables && work.left > 0; ++v) {
        if (roots[static_cast<std::size_t>(v)] >= 0) continue;
        std::vector<int> positive, negative;
        bool complete = true;
        for (int id : occurrences[index(v)]) {
            if (!work.take()) { complete = false; break; }
            if (live[static_cast<std::size_t>(id)] != 0) positive.push_back(id);
        }
        for (int id : occurrences[index(-v)]) {
            if (!work.take()) { complete = false; break; }
            if (live[static_cast<std::size_t>(id)] != 0) negative.push_back(id);
        }
        if (!complete) break;
        if (positive.empty() && negative.empty()) continue;
        if (positive.size() > 8 || negative.size() > 8 || positive.size()*negative.size() > positive.size()+negative.size()) continue;
        std::vector<std::vector<int>> resolvents;
        bool allowed = true;
        for (int pos : positive) for (int neg : negative) {
            std::vector<int> resolvent;
            for (int id : {pos, neg}) for (int lit : clauses[static_cast<std::size_t>(id)]) {
                if (!work.take()) { allowed = false; break; }
                if (variable(lit) != v) resolvent.push_back(lit);
            }
            if (!allowed || resolvent.size() > 20) { allowed = false; break; }
            if (normalize(resolvent)) resolvents.push_back(std::move(resolvent));
        }
        if (!allowed || interrupted()) continue;
        Elimination elimination; elimination.variable = v;
        for (const auto& r : resolvents) emit(r);
        for (int id : positive) { elimination.parents.push_back(clauses[static_cast<std::size_t>(id)]); live[static_cast<std::size_t>(id)] = 0; }
        for (int id : negative) { elimination.parents.push_back(clauses[static_cast<std::size_t>(id)]); live[static_cast<std::size_t>(id)] = 0; }
        result.eliminations.push_back(std::move(elimination));
        for (auto& r : resolvents) {
            const int id = static_cast<int>(clauses.size());
            for (int lit : r) occurrences[index(lit)].push_back(id);
            clauses.push_back(std::move(r)); live.push_back(1);
        }
    }
    std::size_t out = 0; std::uint64_t final_literals = 0;
    for (std::size_t i = 0; i < clauses.size(); ++i) if (live[i] != 0) {
        final_literals += clauses[i].size();
        if (out != i) clauses[out] = std::move(clauses[i]);
        ++out;
    }
    clauses.resize(out);
    result.removed_literals = original_literals > final_literals ? original_literals-final_literals : 0;
    return result;
}

void extend_model(std::vector<int>& model, const std::vector<Elimination>& eliminations) {
    std::vector<std::int8_t> values(model.size()+1, -1);
    for (int lit : model) values[static_cast<std::size_t>(variable(lit))] = static_cast<std::int8_t>(lit > 0 ? 1 : 0);
    for (auto it = eliminations.rbegin(); it != eliminations.rend(); ++it) {
        int required = -1;
        for (const auto& c : it->parents) {
            bool satisfied = false; int polarity = -1;
            for (int lit : c) {
                if (variable(lit) == it->variable) { polarity = lit > 0 ? 1 : 0; continue; }
                const auto val = values[static_cast<std::size_t>(variable(lit))];
                if (val < 0) throw std::logic_error("unassigned model extension dependency");
                if ((val != 0) == (lit > 0)) { satisfied = true; break; }
            }
            if (!satisfied) {
                if (polarity < 0 || (required >= 0 && required != polarity)) throw std::logic_error("inconsistent eliminated-variable model extension");
                required = polarity;
            }
        }
        if (required >= 0) values[static_cast<std::size_t>(it->variable)] = static_cast<std::int8_t>(required);
    }
    for (std::size_t v = 1; v < values.size(); ++v) model[v-1] = values[v] != 0 ? static_cast<int>(v) : -static_cast<int>(v);
}
} // namespace satsolv
