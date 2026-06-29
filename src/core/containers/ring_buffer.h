#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

namespace talal::core {

template <typename T, std::size_t Capacity>
class RingBuffer {
public:
    static_assert(Capacity > 0, "RingBuffer capacity must be greater than zero.");

    bool push(const T& value)
    {
        if (full()) {
            return false;
        }
        values_[tail_].emplace(value);
        advance_tail();
        return true;
    }

    bool push(T&& value)
    {
        if (full()) {
            return false;
        }
        values_[tail_].emplace(std::move(value));
        advance_tail();
        return true;
    }

    std::optional<T> pop()
    {
        if (empty()) {
            return std::nullopt;
        }
        std::optional<T> value = std::move(values_[head_]);
        values_[head_].reset();
        head_ = (head_ + 1) % Capacity;
        --size_;
        return value;
    }

    T* front()
    {
        return empty() ? nullptr : std::addressof(*values_[head_]);
    }

    const T* front() const
    {
        return empty() ? nullptr : std::addressof(*values_[head_]);
    }

    void clear()
    {
        while (pop().has_value()) {
        }
    }

    bool empty() const noexcept { return size_ == 0; }
    bool full() const noexcept { return size_ == Capacity; }
    std::size_t size() const noexcept { return size_; }
    constexpr std::size_t capacity() const noexcept { return Capacity; }

private:
    void advance_tail() noexcept
    {
        tail_ = (tail_ + 1) % Capacity;
        ++size_;
    }

    std::array<std::optional<T>, Capacity> values_;
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t size_ = 0;
};

} // namespace talal::core
