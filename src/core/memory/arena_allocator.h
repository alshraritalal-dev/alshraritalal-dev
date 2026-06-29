#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace talal::core {

class ArenaAllocator {
public:
    explicit ArenaAllocator(std::size_t capacityBytes);

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    void* allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t));
    void reset() noexcept;
    std::size_t mark() const noexcept;
    void rewind(std::size_t marker) noexcept;

    template <typename T, typename... Args>
    T* construct(Args&&... args)
    {
        void* storage = allocate(sizeof(T), alignof(T));
        return storage ? new (storage) T(std::forward<Args>(args)...) : nullptr;
    }

    std::size_t capacity() const noexcept { return storage_.size(); }
    std::size_t used() const noexcept { return offset_; }
    std::size_t remaining() const noexcept { return storage_.size() - offset_; }

private:
    std::vector<std::byte> storage_;
    std::size_t offset_ = 0;
};

} // namespace talal::core
