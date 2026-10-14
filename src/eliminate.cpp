#include "satsolv/simplifier.hpp"
#include "satsolv/indexed_heap.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>

namespace satsolv {
namespace {
int variable(int lit) { return lit < 0 ? -lit : lit; }
std::size_t index(int lit) { return 2U*static_cast<std::size_t>(variable(lit)-1)+(lit < 0 ? 1U : 0U); }
bool order(int a, int b) { return variable(a) != variable(b) ? variable(a) < variable(b) : a < b; }
bool normalize(std::vector<int>& clause) {
    std::sort(clause.begin(),clause.end(),order);
    clause.erase(std::unique(clause.begin(),clause.end()),clause.end());
    for (std::size_t i = 1; i < clause.size(); ++i) if (clause[i] == -clause[i-1]) return false;
    return true;
}
}

SimplifyResult eliminate_bounded(const Formula& input, std::uint64_t budget,
    const std::function<void(const std::vector<int>&)>& emit,
    const std::function<bool()>& interrupted) {
    SimplifyResult result;
    if (input.variables < 0 || input.variables > 200000 || input.clauses.size() > 1000000 || budget == 0 || interrupted()) return result;
    std::uint64_t literals = 0;
    for (const auto& clause : input.clauses) {
        if (clause.empty()) return result;
        literals += clause.size();
        for (int lit : clause) {
            const auto v = lit < 0 ? -static_cast<std::int64_t>(lit) : static_cast<std::int64_t>(lit);
            if (v == 0 || v > input.variables) throw std::invalid_argument("invalid elimination literal");
        }
        if (literals > 2000000) return result;
    }
    result.applied = true;
    result.formula.variables = input.variables;
    auto& clauses = result.formula.clauses;
    for (auto c : input.clauses) if (normalize(c)) clauses.push_back(std::move(c));
    std::vector<std::uint8_t> live(clauses.size(),1);
    std::vector<std::vector<int>> occurrences(static_cast<std::size_t>(2*input.variables));
    std::vector<std::size_t> counts(occurrences.size(),0);
    std::vector<std::uint8_t> eliminated(static_cast<std::size_t>(input.variables+1),0);
    std::vector<double> costs(eliminated.size(),0);
    std::vector<std::uint64_t> ranks(eliminated.size(),0);
    std::uint64_t live_literals = 0;
    const auto literal_limit = std::min<std::uint64_t>(2000000,4*literals+1024);
    IndexedHeap heap;
    heap.reset(costs,ranks);
    for (std::size_t i = 0; i < clauses.size(); ++i) for (int lit : clauses[i]) {
        occurrences[index(lit)].push_back(static_cast<int>(i)); ++counts[index(lit)];
        ++live_literals;
    }
    const auto refresh = [&](int v) {
        if (eliminated[static_cast<std::size_t>(v)] != 0) return;
        const auto pos = counts[index(v)], neg = counts[index(-v)];
        costs[static_cast<std::size_t>(v)] = -static_cast<double>(pos)*static_cast<double>(neg);
        heap.update(v);
    };
    for (int v = 1; v <= input.variables; ++v) { ranks[static_cast<std::size_t>(v)] = static_cast<std::uint64_t>(v); refresh(v); }
    std::uint64_t ticks = 0;
    const auto take = [&]() {
        if (budget == 0) return false;
        --budget; ++result.elimination_work;
        return (++ticks & 1023U) != 0U || !interrupted();
    };
    std::vector<int> pos,neg,changed;
    while (!heap.empty() && budget != 0 && !interrupted()) {
        const int v = heap.pop();
        const auto p = counts[index(v)], n = counts[index(-v)];
        if (p+n == 0 || p > 64 || n > 64 || p+n > 128) continue;
        ++result.elimination_attempts;
        pos.clear(); neg.clear();
        bool complete = true;
        std::size_t parent_literals = 0;
        for (int sign : {v,-v}) for (int id : occurrences[index(sign)]) {
            if (!take()) { complete = false; break; }
            if (live[static_cast<std::size_t>(id)] == 0) continue;
            (sign > 0 ? pos : neg).push_back(id);
            parent_literals += clauses[static_cast<std::size_t>(id)].size();
        }
        if (!complete) break;
        if (pos.size() != p || neg.size() != n) throw std::logic_error("elimination occurrence count drift");
        std::set<std::vector<int>> unique;
        std::size_t new_literals = 0;
        bool allowed = true;
        for (int a : pos) {
            for (int b : neg) {
                std::vector<int> resolvent;
                for (int id : {a,b}) {
                    for (int lit : clauses[static_cast<std::size_t>(id)]) {
                        if (!take()) { complete = false; break; }
                        if (variable(lit) != v) resolvent.push_back(lit);
                    }
                    if (!complete) break;
                }
                if (!complete) break;
                if (resolvent.size() > 64) { allowed = false; break; }
                if (!normalize(resolvent)) { ++result.tautological_resolvents; continue; }
                const auto [it, inserted] = unique.insert(std::move(resolvent));
                if (inserted) new_literals += it->size();
                if (unique.size() > p+n || live_literals-parent_literals+new_literals > literal_limit) { allowed = false; break; }
            }
            if (!complete || !allowed) break;
        }
        if (!complete) break;
        if (!allowed || interrupted()) continue;
        // Historical descriptors are bounded too; a non-growing live formula
        // alone does not bound stale occurrence storage across many eliminations.
        if (clauses.size()+unique.size() > 1000000) break;
        Elimination extension;
        extension.variable = v;
        for (int id : pos) extension.parents.push_back(clauses[static_cast<std::size_t>(id)]);
        for (int id : neg) extension.parents.push_back(clauses[static_cast<std::size_t>(id)]);
        for (const auto& c : unique) emit(c);
        if (unique.contains(std::vector<int>{})) {
            clauses = {{}};
            result.removed_literals = literals;
            return result;
        }
        changed.clear();
        eliminated[static_cast<std::size_t>(v)] = 1;
        for (int id : pos) live[static_cast<std::size_t>(id)] = 0;
        for (int id : neg) live[static_cast<std::size_t>(id)] = 0;
        for (const auto& c : extension.parents) for (int lit : c) {
            --counts[index(lit)]; changed.push_back(variable(lit));
        }
        result.eliminations.push_back(std::move(extension));
        live_literals = live_literals-parent_literals+new_literals;
        for (const auto& c : unique) {
            const int id = static_cast<int>(clauses.size());
            clauses.push_back(c); live.push_back(1);
            for (int lit : c) { occurrences[index(lit)].push_back(id); ++counts[index(lit)]; changed.push_back(variable(lit)); }
        }
        std::sort(changed.begin(),changed.end());
        changed.erase(std::unique(changed.begin(),changed.end()),changed.end());
        for (int neighbor : changed) refresh(neighbor);
    }
    std::uint64_t final_literals = 0;
    std::size_t out = 0;
    for (std::size_t i = 0; i < clauses.size(); ++i) if (live[i] != 0) {
        final_literals += clauses[i].size();
        if (out != i) clauses[out] = std::move(clauses[i]);
        ++out;
    }
    clauses.resize(out);
    result.removed_literals = literals > final_literals ? literals-final_literals : 0;
    return result;
}
} // namespace satsolv
