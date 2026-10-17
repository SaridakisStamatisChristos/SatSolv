#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace satsolv {

// Intrusive variable-move-to-front queue used by the experimental focused
// search mode.  The tail is the most recently bumped (highest-priority)
// variable.  Assigned variables stay linked; a search cursor walks backwards
// and cancel_until() restores newly unassigned variables by stamp.
class VmtfQueue {
    std::vector<int> prev_;
    std::vector<int> next_;
    std::vector<std::uint64_t> stamp_;
    int head_ = 0;
    int tail_ = 0;
    int search_ = 0;
    std::uint64_t clock_ = 0;

    void unlink(int v) noexcept {
        const int p = prev_[static_cast<std::size_t>(v)];
        const int n = next_[static_cast<std::size_t>(v)];
        if (p != 0) next_[static_cast<std::size_t>(p)] = n;
        else head_ = n;
        if (n != 0) prev_[static_cast<std::size_t>(n)] = p;
        else tail_ = p;
        prev_[static_cast<std::size_t>(v)] = 0;
        next_[static_cast<std::size_t>(v)] = 0;
    }

    void append(int v) noexcept {
        prev_[static_cast<std::size_t>(v)] = tail_;
        next_[static_cast<std::size_t>(v)] = 0;
        if (tail_ != 0) next_[static_cast<std::size_t>(tail_)] = v;
        else head_ = v;
        tail_ = v;
    }

    void renormalize_stamps() noexcept {
        std::uint64_t stamp = 0;
        for (int v = head_; v != 0; v = next_[static_cast<std::size_t>(v)]) {
            stamp_[static_cast<std::size_t>(v)] = ++stamp;
        }
        clock_ = stamp;
    }

public:
    void clear() noexcept {
        prev_.clear();
        next_.clear();
        stamp_.clear();
        head_ = tail_ = search_ = 0;
        clock_ = 0;
    }

    void reset(const std::vector<double>& scores,
               const std::vector<std::uint64_t>& ranks) {
        if (scores.size() != ranks.size())
            throw std::invalid_argument("VMTF score/rank size mismatch");
        prev_.assign(scores.size(), 0);
        next_.assign(scores.size(), 0);
        stamp_.assign(scores.size(), 0);
        head_ = tail_ = search_ = 0;
        clock_ = 0;
        if (scores.size() <= 1) return;

        std::vector<int> variables;
        variables.reserve(scores.size() - 1);
        for (std::size_t i = 1; i < scores.size(); ++i)
            variables.push_back(static_cast<int>(i));

        // Append from worst to best so the initial focused choice matches the
        // EVSIDS heap's score/rank ordering before conflict-driven VMTF bumps.
        std::stable_sort(variables.begin(), variables.end(), [&](int a, int b) {
            const auto ia = static_cast<std::size_t>(a);
            const auto ib = static_cast<std::size_t>(b);
            if (scores[ia] != scores[ib]) return scores[ia] < scores[ib];
            if (ranks[ia] != ranks[ib]) return ranks[ia] > ranks[ib];
            return a > b;
        });
        for (int v : variables) {
            append(v);
            stamp_[static_cast<std::size_t>(v)] = ++clock_;
        }
        search_ = tail_;
    }

    void bump(int v) {
        const auto index = static_cast<std::size_t>(v);
        if (v <= 0 || index >= prev_.size())
            throw std::logic_error("VMTF variable out of range");
        if (clock_ == std::numeric_limits<std::uint64_t>::max())
            renormalize_stamps();
        if (tail_ != v) {
            unlink(v);
            append(v);
        }
        stamp_[index] = ++clock_;
        search_ = v;
    }

    void note_unassigned(int v) noexcept {
        const auto index = static_cast<std::size_t>(v);
        if (v <= 0 || index >= stamp_.size()) return;
        if (search_ == 0 || stamp_[index] > stamp_[static_cast<std::size_t>(search_)])
            search_ = v;
    }

    int pick(const std::vector<std::int8_t>& assigns,
             const std::vector<std::uint8_t>& enabled,
             std::uint64_t& inspected) noexcept {
        int v = search_ != 0 ? search_ : tail_;
        while (v != 0) {
            ++inspected;
            const auto index = static_cast<std::size_t>(v);
            if (index < assigns.size() && index < enabled.size() &&
                assigns[index] < 0 && enabled[index] != 0) {
                search_ = v;
                return v;
            }
            v = prev_[index];
        }
        search_ = 0;
        return 0;
    }

    int tail() const noexcept { return tail_; }
    int search() const noexcept { return search_; }
    std::uint64_t stamp(int v) const noexcept {
        const auto index = static_cast<std::size_t>(v);
        return v > 0 && index < stamp_.size() ? stamp_[index] : 0;
    }
};

} // namespace satsolv
