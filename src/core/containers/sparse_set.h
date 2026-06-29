#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace talal::core {

template <typename Id = std::uint32_t>
class SparseSet {
public:
    bool insert(Id id)
    {
        const auto index = to_index(id);
        if (index >= sparse_.size()) {
            sparse_.resize(index + 1, kInvalid);
        }
        if (contains(id)) {
            return false;
        }

        sparse_[index] = dense_.size();
        dense_.push_back(id);
        return true;
    }

    bool erase(Id id)
    {
        if (!contains(id)) {
            return false;
        }

        const auto sparseIndex = to_index(id);
        const auto denseIndex = sparse_[sparseIndex];
        const Id moved = dense_.back();
        dense_[denseIndex] = moved;
        sparse_[to_index(moved)] = denseIndex;
        dense_.pop_back();
        sparse_[sparseIndex] = kInvalid;
        return true;
    }

    bool contains(Id id) const
    {
        const auto index = to_index(id);
        if (index >= sparse_.size()) {
            return false;
        }
        const auto denseIndex = sparse_[index];
        return denseIndex != kInvalid && denseIndex < dense_.size() && dense_[denseIndex] == id;
    }

    void clear()
    {
        dense_.clear();
        std::fill(sparse_.begin(), sparse_.end(), kInvalid);
    }

    std::span<const Id> values() const noexcept
    {
        return dense_;
    }

    std::size_t size() const noexcept { return dense_.size(); }
    bool empty() const noexcept { return dense_.empty(); }

private:
    static constexpr std::size_t kInvalid = std::numeric_limits<std::size_t>::max();

    static std::size_t to_index(Id id)
    {
        return static_cast<std::size_t>(id);
    }

    std::vector<Id> dense_;
    std::vector<std::size_t> sparse_;
};

} // namespace talal::core
