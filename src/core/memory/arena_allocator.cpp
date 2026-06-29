#include "core/memory/arena_allocator.h"

#include <algorithm>

namespace talal::core {

namespace {

std::size_t AlignUp(std::size_t value, std::size_t alignment)
{
    const std::size_t mask = alignment - 1;
    return (value + mask) & ~mask;
}

bool IsPowerOfTwo(std::size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

} // namespace

ArenaAllocator::ArenaAllocator(std::size_t capacityBytes)
    : storage_(capacityBytes)
{
}

void* ArenaAllocator::allocate(std::size_t bytes, std::size_t alignment)
{
    if (bytes == 0) {
        return nullptr;
    }

    alignment = std::max(alignment, alignof(void*));
    if (!IsPowerOfTwo(alignment)) {
        return nullptr;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(storage_.data());
    const auto current = base + offset_;
    const auto aligned = AlignUp(current, alignment);
    const auto nextOffset = static_cast<std::size_t>(aligned - base) + bytes;
    if (nextOffset > storage_.size()) {
        return nullptr;
    }

    offset_ = nextOffset;
    return reinterpret_cast<void*>(aligned);
}

void ArenaAllocator::reset() noexcept
{
    offset_ = 0;
}

std::size_t ArenaAllocator::mark() const noexcept
{
    return offset_;
}

void ArenaAllocator::rewind(std::size_t marker) noexcept
{
    offset_ = std::min(marker, offset_);
}

} // namespace talal::core
