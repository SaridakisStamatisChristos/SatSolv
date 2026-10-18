#include "satsolv/bva.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace satsolv {
namespace {
using Edge = std::pair<int, int>;
Edge edge(int a, int b) { return std::minmax(a, b); }
int magnitude(int x) { return x < 0 ? -x : x; }

struct Candidate {
    Edge centers{0, 0};
    std::vector<int> common;
};
} // namespace

BvaResult factor_binary_bva(
    const Formula& input, std::uint64_t budget, std::uint64_t max_variables,
    const std::function<void(const std::vector<int>&)>& emit,
    const std::function<bool()>& interrupted) {
    BvaResult result;
    if (input.variables < 0 || input.variables >= std::numeric_limits<int>::max() / 2 ||
        input.clauses.size() > 1000000 || !budget || !max_variables || interrupted())
        return result;
    // This is an intentionally narrow BVA prototype, not a general SAT
    // simplifier. Reject malformed direct API inputs before copying/indexing.
    for (const auto& clause : input.clauses)
        for (int literal : clause)
            if (literal == 0 ||
                static_cast<std::int64_t>(literal < 0 ? -static_cast<std::int64_t>(literal) : literal) > input.variables)
                throw std::invalid_argument("BVA input literal outside variable range");

    Formula working = input;
    const std::uint64_t maximum = std::min<std::uint64_t>(max_variables, 16);
    for (std::uint64_t round = 0; round < maximum; ++round) {
        if (interrupted() || working.variables >= std::numeric_limits<int>::max() / 2)
            break;
        std::map<Edge, std::vector<std::size_t>> edges;
        bool exhausted = false;
        for (std::size_t index = 0; index < working.clauses.size(); ++index) {
            if (result.work >= budget) { exhausted = true; break; }
            ++result.work;
            const auto& clause = working.clauses[index];
            if (clause.size() != 2) continue;
            const int a = clause[0], b = clause[1];
            if (magnitude(a) == magnitude(b)) continue; // tautology or unit
            edges[edge(a, b)].push_back(index);
        }
        // A truncated index could miss clauses to be removed. Rather than
        // speculatively transform an incompletely scanned database, stop.
        if (exhausted || edges.size() > 300000) break;

        std::map<int, std::set<int>> neighbors;
        for (const auto& [pair, indices] : edges) {
            (void) indices;
            neighbors[pair.first].insert(pair.second);
            neighbors[pair.second].insert(pair.first);
        }

        // For each right-hand literal b, collect pairs of left-hand literals
        // that both occur in (left | b). This enumerates complete 2 x k
        // bicliques without assuming a particular polarity or ordering.
        std::map<Edge, std::vector<int>> commons;
        for (const auto& [b, adjacent] : neighbors) {
            if (adjacent.size() < 2 || adjacent.size() > 64) continue;
            const std::vector<int> as(adjacent.begin(), adjacent.end());
            for (std::size_t i = 0; i < as.size(); ++i) {
                for (std::size_t j = i + 1; j < as.size(); ++j) {
                    if (result.work >= budget) { exhausted = true; break; }
                    ++result.work;
                    const int a = as[i], c = as[j];
                    if (magnitude(a) == magnitude(c)) continue;
                    if (magnitude(b) == magnitude(a) || magnitude(b) == magnitude(c)) continue;
                    commons[edge(a, c)].push_back(b);
                }
                if (exhausted) break;
            }
            if (exhausted) break;
        }
        if (exhausted) break;

        Candidate best;
        for (const auto& [pair, bs] : commons) {
            if (bs.size() < 3) continue; // only transformations saving clauses
            if (bs.size() > best.common.size()) {
                best.centers = pair;
                best.common.assign(bs.begin(), bs.begin() +
                                   static_cast<std::ptrdiff_t>(std::min<std::size_t>(bs.size(), 32)));
            }
        }
        if (best.common.size() < 3) break;
        const int x = working.variables + 1;
        std::vector<std::uint8_t> remove(working.clauses.size(), 0);
        std::uint64_t deleted = 0;
        for (int b : best.common) {
            for (int a : {best.centers.first, best.centers.second}) {
                const auto found = edges.find(edge(a, b));
                if (found == edges.end()) throw std::logic_error("BVA selected missing biclique edge");
                for (std::size_t id : found->second) {
                    if (!remove[id]) { remove[id] = 1; ++deleted; }
                }
            }
        }
        if (deleted <= best.common.size() + 2) break;

        Formula next;
        next.variables = x;
        next.clauses.reserve(working.clauses.size() - static_cast<std::size_t>(deleted)
                             + best.common.size() + 2);
        for (std::size_t i = 0; i < working.clauses.size(); ++i)
            if (!remove[i]) next.clauses.push_back(std::move(working.clauses[i]));

        // DRAT addition order is essential: x is fresh, so (x | a) and
        // (x | c) are RAT on x. Every (-x | b) is RAT on -x because its
        // resolvents (a | b), (c | b) are still original/previously added
        // clauses in the checker, hence RUP. Only additions are necessary;
        // original biclique clauses may remain in the proof checker.
        const std::vector<int> first{x, best.centers.first};
        const std::vector<int> second{x, best.centers.second};
        std::vector<std::vector<int>> additions;
        additions.reserve(best.common.size() + 2);
        additions.push_back(first);
        additions.push_back(second);
        for (int b : best.common) additions.push_back({-x, b});
        if (interrupted()) break;
        for (const auto& clause : additions) emit(clause);
        for (auto& clause : additions) next.clauses.push_back(std::move(clause));
        working = std::move(next);

        result.applied = true;
        ++result.variables_added;
        result.clauses_removed += deleted;
        result.literals_saved += 2 * (deleted - (best.common.size() + 2));
    }
    if (result.applied) result.formula = std::move(working);
    return result;
}

} // namespace satsolv
