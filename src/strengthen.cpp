#include "satsolv/strengthen.hpp"
#include <algorithm>
#include <cstddef>
#include <limits>

namespace satsolv {
namespace {
int variable(int lit) noexcept { return lit < 0 ? -lit : lit; }
std::size_t index(int lit) noexcept {
    return 2U * static_cast<std::size_t>(variable(lit) - 1) + (lit < 0 ? 1U : 0U);
}
struct Work {
    std::uint64_t left;
    std::uint64_t used = 0;
    const std::function<bool()>& interrupted;
    std::uint64_t reserve = 0;
    bool take() {
        if (left <= reserve) return false;
        --left; ++used;
        if ((used & 1023U) == 0U && interrupted()) { left = 0; return false; }
        return true;
    }
};
}

StrengthenResult strengthen_short(const Formula& input, bool ssr, bool implications,
    std::uint64_t budget, const std::function<void(const std::vector<int>&)>& emit,
    const std::function<bool()>& interrupted) {
    StrengthenResult result;
    std::uint64_t literals = 0;
    for (const auto& c : input.clauses) literals += c.size();
    // Workspace is bounded independently of the caller's address-space limit.
    if ((!ssr && !implications) || budget == 0 || input.variables > 200000 ||
        input.clauses.size() > 1000000 || literals > 2000000 || interrupted()) return result;
    result.formula.variables = input.variables;
    auto& clauses = result.formula.clauses;
    clauses.reserve(input.clauses.size());
    for (auto c : input.clauses) {
        std::sort(c.begin(), c.end());
        c.erase(std::unique(c.begin(), c.end()), c.end());
        bool tautology = false;
        for (int lit : c) if (std::binary_search(c.begin(), c.end(), -lit)) { tautology = true; break; }
        if (!tautology) clauses.push_back(std::move(c));
    }
    Work work{budget, 0, interrupted};
    const auto slots = 2U * static_cast<std::size_t>(input.variables);
    if (ssr) {
        // A combined arm must actually exercise both mechanisms. Otherwise
        // resolution can spend the entire shared budget before graph analysis.
        work.reserve = implications ? budget / 2 : 0;
        std::vector<std::vector<std::size_t>> occurrences(slots);
        bool complete = true;
        for (std::size_t id = 0; id < clauses.size() && complete; ++id) {
            // Short supports only; targets can be wider. Replacements only
            // remove literals, so stale occurrence entries stay conservative.
            if (clauses[id].size() < 2 || clauses[id].size() > 8) continue;
            for (int lit : clauses[id]) {
                if (!work.take()) { complete = false; break; }
                occurrences[index(lit)].push_back(id);
            }
        }
        if (complete) for (auto& ids : occurrences) std::stable_sort(ids.begin(), ids.end(), [&](std::size_t a, std::size_t b) {
            return clauses[a].size() < clauses[b].size();
        });
        std::vector<int> shortened;
        for (int pass = 0; complete && pass < 2 && work.left > work.reserve; ++pass) {
            for (std::size_t id = 0; id < clauses.size() && work.left > work.reserve; ++id) {
                auto& c = clauses[id];
                if (c.size() < 2 || c.size() > 32) continue;
                for (std::size_t k = 0; k < c.size() && c.size() >= 2 && work.left > work.reserve;) {
                    const int pivot = c[k];
                    bool remove = false;
                    unsigned attempts = 0;
                    for (std::size_t support : occurrences[index(-pivot)]) {
                        if (++attempts > 128 || !work.take()) break;
                        if (support == id) continue;
                        const auto& d = clauses[support];
                        if (d.size() > c.size()) continue;
                        bool found_pivot = false, subset = true;
                        for (int lit : d) {
                            if (!work.take()) { subset = false; break; }
                            if (lit == -pivot) found_pivot = true;
                            else if (!std::binary_search(c.begin(), c.end(), lit)) { subset = false; break; }
                        }
                        if (found_pivot && subset) { remove = true; break; }
                    }
                    if (!remove) { ++k; continue; }
                    shortened = c;
                    shortened.erase(shortened.begin() + static_cast<std::ptrdiff_t>(k));
                    emit(shortened); // RUP from C and its opposite-pivot support.
                    c.swap(shortened); ++result.ssr_removed;
                }
            }
        }
    }
    work.reserve = 0;
    if (implications && work.left > 0) {
        std::vector<std::vector<int>> graph(slots);
        bool complete = true;
        for (const auto& c : clauses) {
            if (c.size() != 2) continue;
            if (!work.take() || !work.take()) { complete = false; break; }
            graph[index(-c[0])].push_back(c[1]);
            graph[index(-c[1])].push_back(c[0]);
        }
        std::vector<std::uint64_t> reached(slots, 0);
        std::vector<std::uint8_t> emitted_units(slots, 0);
        std::uint64_t epoch = 0;
        std::vector<int> queue, shortened, units;
        queue.reserve(129);
        const auto before = clauses.size();
        for (std::size_t id = 0; complete && id < before && work.left > 0; ++id) {
            auto& c = clauses[id];
            if (c.size() < 2 || c.size() > 12) continue;
            for (std::size_t k = 0; k < c.size() && c.size() >= 2 && work.left > 0;) {
                const int source = c[k];
                if (graph[index(source)].empty()) { ++k; continue; }
                if (++epoch == 0) { std::fill(reached.begin(), reached.end(), 0); epoch = 1; }
                queue.clear(); queue.push_back(source); reached[index(source)] = epoch;
                bool failed = false;
                unsigned edges = 0;
                for (std::size_t head = 0; head < queue.size() && !failed && edges < 128 && work.left > 0; ++head) {
                    for (int implied : graph[index(queue[head])]) {
                        if (++edges > 128 || !work.take()) break;
                        if (reached[index(-implied)] == epoch) { failed = true; break; }
                        if (reached[index(implied)] != epoch) {
                            reached[index(implied)] = epoch; queue.push_back(implied);
                        }
                    }
                }
                if (failed) {
                    if (emitted_units[index(-source)] == 0) {
                        emit({-source}); units.push_back(-source);
                        emitted_units[index(-source)] = 1; ++result.failed_units;
                    }
                    // The new unit entails removal of this false literal.
                }
                bool remove = failed;
                if (!remove) for (std::size_t j = 0; j < c.size(); ++j)
                    if (j != k && reached[index(c[j])] == epoch) { remove = true; break; }
                if (!remove) { ++k; continue; }
                shortened = c;
                shortened.erase(shortened.begin() + static_cast<std::ptrdiff_t>(k));
                emit(shortened); c.swap(shortened); ++result.implication_removed;
                if (c.size() == 2) {
                    graph[index(-c[0])].push_back(c[1]);
                    graph[index(-c[1])].push_back(c[0]);
                }
            }
        }
        for (int unit : units) clauses.push_back({unit});
    }
    result.work = work.used;
    result.applied = result.ssr_removed != 0 || result.implication_removed != 0 || result.failed_units != 0;
    // Avoid changing normalization/watch order when no transformation fires.
    if (!result.applied) result.formula = {};
    return result;
}
}
