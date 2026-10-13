#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace satsolv {
// One entry per variable. Scores may only change through update/rebuild.
class IndexedHeap {
    const std::vector<double>* scores_ = nullptr;
    const std::vector<std::uint64_t>* ranks_ = nullptr;
    std::vector<int> heap_;
    std::vector<int> positions_;
    bool better(int a, int b) const noexcept {
        if ((*scores_)[static_cast<std::size_t>(a)] != (*scores_)[static_cast<std::size_t>(b)])
            return (*scores_)[static_cast<std::size_t>(a)] > (*scores_)[static_cast<std::size_t>(b)];
        if ((*ranks_)[static_cast<std::size_t>(a)] != (*ranks_)[static_cast<std::size_t>(b)])
            return (*ranks_)[static_cast<std::size_t>(a)] < (*ranks_)[static_cast<std::size_t>(b)];
        return a < b;
    }
    void exchange(std::size_t a, std::size_t b) noexcept {
        std::swap(heap_[a], heap_[b]);
        positions_[static_cast<std::size_t>(heap_[a])] = static_cast<int>(a);
        positions_[static_cast<std::size_t>(heap_[b])] = static_cast<int>(b);
    }
    void up(std::size_t p) noexcept {
        while (p != 0) {
            const auto parent = (p - 1) / 2;
            if (!better(heap_[p], heap_[parent])) break;
            exchange(p, parent); p = parent;
        }
    }
    void down(std::size_t p) noexcept {
        while (2*p+1 < heap_.size()) {
            auto child = 2*p+1;
            if (child+1 < heap_.size() && better(heap_[child+1], heap_[child])) ++child;
            if (!better(heap_[child], heap_[p])) break;
            exchange(p, child); p = child;
        }
    }
public:
    void reset(const std::vector<double>& scores, const std::vector<std::uint64_t>& ranks) {
        scores_ = &scores; ranks_ = &ranks;
        heap_.clear(); heap_.reserve(scores.size()); positions_.assign(scores.size(), -1);
    }
    bool empty() const noexcept { return heap_.empty(); }
    std::size_t size() const noexcept { return heap_.size(); }
    void update(int v) {
        const auto i = static_cast<std::size_t>(v);
        if (i >= positions_.size()) throw std::logic_error("heap variable out of range");
        if (positions_[i] < 0) {
            positions_[i] = static_cast<int>(heap_.size()); heap_.push_back(v);
        }
        auto p = static_cast<std::size_t>(positions_[i]);
        up(p); p = static_cast<std::size_t>(positions_[i]); down(p);
    }
    int pop() {
        if (heap_.empty()) throw std::logic_error("empty branch heap");
        const int v = heap_.front(); positions_[static_cast<std::size_t>(v)] = -1;
        if (heap_.size() == 1) { heap_.pop_back(); return v; }
        heap_.front() = heap_.back(); heap_.pop_back();
        positions_[static_cast<std::size_t>(heap_.front())] = 0; down(0); return v;
    }
    void rebuild() noexcept {
        for (std::size_t i = heap_.size()/2; i > 0; --i) down(i-1);
    }
};
} // namespace satsolv
