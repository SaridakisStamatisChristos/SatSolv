#include "satsolv/bva.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace satsolv {
namespace {
using Centers = std::pair<int, int>;
using Tail = std::vector<int>;

// Both source clauses must be present for every selected tail. A pair of
// complementary centers cannot be used as a meaningful factor.
Centers centers(int a, int b) { return std::minmax(a, b); }
int magnitude(int x) { return x < 0 ? -x : x; }

using Index = std::map<Tail, std::map<int, std::vector<std::size_t>>>;
struct Candidate {
    Centers pair{0, 0};
    std::vector<Tail> tails;
    std::size_t potential_savings = 0;
};

bool normalize_clause(const std::vector<int>& original, Tail& sorted) {
    sorted = original;
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t j = 1; j < sorted.size(); ++j)
        if (sorted[j] == sorted[j - 1]) return false;
    for (int lit : sorted)
        if (std::binary_search(sorted.begin(), sorted.end(), -lit)) return false;
    return true;
}
} // namespace

BvaResult factor_bounded_bva(
    const Formula& input, std::uint64_t budget, std::uint64_t max_variables,
    const std::function<void(const std::vector<int>&)>& emit,
    const std::function<bool()>& interrupted) {
    BvaResult result;
    if (input.variables < 0 || input.variables >= std::numeric_limits<int>::max() / 2 ||
        input.clauses.size() > 300000 || !budget || !max_variables || interrupted())
        return result;
    for (const auto& clause : input.clauses)
        for (int literal : clause)
            if (literal == 0 ||
                (literal < 0 ? -static_cast<std::int64_t>(literal) : literal) > input.variables)
                throw std::invalid_argument("BVA input literal outside variable range");

    Formula working = input;
    const std::uint64_t maximum = std::min<std::uint64_t>(max_variables, 16);
    for (std::uint64_t round = 0; round < maximum; ++round) {
        if (interrupted() || working.variables >= std::numeric_limits<int>::max() / 2)
            break;

        // Index each bounded-length original clause as (center | tail).
        // This covers binary-clause bicliques and matching multi-literal tails.
        Index index;
        bool exhausted = false;
        for (std::size_t id = 0; id < working.clauses.size(); ++id) {
            if (result.work >= budget) { exhausted = true; break; }
            ++result.work;
            const auto& clause = working.clauses[id];
            if (clause.size() < 2 || clause.size() > 8) continue;
            Tail sorted;
            if (!normalize_clause(clause, sorted)) continue;
            for (std::size_t i = 0; i < sorted.size(); ++i) {
                if (result.work >= budget) { exhausted = true; break; }
                ++result.work;
                Tail tail;
                tail.reserve(sorted.size() - 1);
                for (std::size_t j = 0; j < sorted.size(); ++j)
                    if (j != i) tail.push_back(sorted[j]);
                index[std::move(tail)][sorted[i]].push_back(id);
            }
            if (exhausted) break;
        }
        // No inference from an incomplete occurrence scan.
        if (exhausted) break;

        // Complete 2 x k rectangles: two center literals share k distinct
        // tails, where every original (center | tail) clause exists.
        std::map<Centers, std::vector<Tail>> shared;
        for (const auto& [tail, occurrences] : index) {
            if (occurrences.size() < 2 || occurrences.size() > 64) continue;
            for (auto it = occurrences.begin(); it != occurrences.end(); ++it) {
                for (auto jt = std::next(it); jt != occurrences.end(); ++jt) {
                    if (result.work >= budget) { exhausted = true; break; }
                    ++result.work;
                    if (magnitude(it->first) == magnitude(jt->first)) continue;
                    shared[centers(it->first, jt->first)].push_back(tail);
                }
                if (exhausted) break;
            }
            if (exhausted) break;
        }
        if (exhausted) break;

        Candidate best;
        for (const auto& [pair, tails] : shared) {
            if (tails.size() < 3) continue;
            const std::size_t use = std::min<std::size_t>(tails.size(), 32);
            std::size_t saved = 0;
            for (std::size_t i = 0; i < use; ++i)
                saved += tails[i].size() + 1;
            if (saved <= 4 || saved <= best.potential_savings) continue;
            best.pair = pair;
            best.tails.assign(tails.begin(), tails.begin() +
                                               static_cast<std::ptrdiff_t>(use));
            best.potential_savings = saved;
        }
        if (best.tails.size() < 3) break;
        const int fresh = working.variables + 1;
        std::vector<std::uint8_t> removed(working.clauses.size(), 0);
        std::uint64_t removed_count = 0;
        std::uint64_t removed_literals = 0;
        for (const auto& tail : best.tails) {
            const auto bucket = index.find(tail);
            if (bucket == index.end()) throw std::logic_error("BVA tail vanished");
            for (int center : {best.pair.first, best.pair.second}) {
                const auto found = bucket->second.find(center);
                if (found == bucket->second.end()) throw std::logic_error("BVA center vanished");
                for (std::size_t id : found->second) {
                    if (!removed[id]) {
                        removed[id] = 1;
                        ++removed_count;
                        removed_literals += working.clauses[id].size();
                    }
                }
            }
        }
        const std::uint64_t added_count = best.tails.size() + 2;
        std::uint64_t added_literals = 4;
        for (const auto& tail : best.tails) added_literals += tail.size() + 1;
        if (removed_count <= added_count || removed_literals <= added_literals) break;

        Formula next;
        next.variables = fresh;
        next.clauses.reserve(working.clauses.size() - static_cast<std::size_t>(removed_count)
                             + static_cast<std::size_t>(added_count));
        for (std::size_t i = 0; i < working.clauses.size(); ++i)
            if (!removed[i]) next.clauses.push_back(std::move(working.clauses[i]));

        // Valid DRAT sequence, pivot FIRST:
        // (fresh | center_1) and (fresh | center_2) are RAT on a fresh
        // positive variable. Each (-fresh | tail) is RAT on -fresh, because
        // the two resolvents are pre-existing (center_i | tail) clauses.
        // Do not delete those clauses in the verifier proof database.
        std::vector<std::vector<int>> additions;
        additions.reserve(static_cast<std::size_t>(added_count));
        additions.push_back({fresh, best.pair.first});
        additions.push_back({fresh, best.pair.second});
        for (const auto& tail : best.tails) {
            std::vector<int> add;
            add.reserve(tail.size() + 1);
            add.push_back(-fresh);
            add.insert(add.end(), tail.begin(), tail.end());
            additions.push_back(std::move(add));
        }
        if (interrupted()) break;
        for (const auto& clause : additions) emit(clause);
        for (auto& clause : additions) next.clauses.push_back(std::move(clause));
        working = std::move(next);
        result.applied = true;
        ++result.variables_added;
        result.clauses_removed += removed_count;
        result.literals_saved += removed_literals - added_literals;
    }
    if (result.applied) result.formula = std::move(working);
    return result;
}
} // namespace satsolv
