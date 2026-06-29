#include "core/memory/pool_allocator.h"

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

PoolAllocator::PoolAllocator(std::size_t blockSize, std::size_t blockCount, std::size_t alignment)
    : blockCount_(blockCount)
    , alignment_(IsPowerOfTwo(alignment) ? alignment : alignof(std::max_align_t))
{
    blockSize_ = AlignUp(std::max(blockSize, sizeof(FreeNode)), alignment_);
    storage_.resize(blockSize_ * blockCount_);

    for (std::size_t i = 0; i < blockCount_; ++i) {
        auto* node = reinterpret_cast<FreeNode*>(storage_.data() + i * blockSize_);
        node->next = freeList_;
        freeList_ = node;
    }
}

void* PoolAllocator::allocate() noexcept
{
    if (!freeList_) {
        return nullptr;
    }

    FreeNode* node = freeList_;
    freeList_ = freeList_->next;
    ++usedBlocks_;
    return node;
}

void PoolAllocator::deallocate(void* pointer) noexcept
{
    if (!owns(pointer)) {
        return;
    }

    auto* node = static_cast<FreeNode*>(pointer);
    node->next = freeList_;
    freeList_ = node;
    if (usedBlocks_ > 0) {
        --usedBlocks_;
    }
}

bool PoolAllocator::owns(const void* pointer) const noexcept
{
    if (!pointer || storage_.empty()) {
        return false;
    }

    const auto* begin = storage_.data();
    const auto* end = begin + storage_.size();
    const auto* address = static_cast<const std::byte*>(pointer);
    if (address < begin || address >= end) {
        return false;
    }

    const auto offset = static_cast<std::size_t>(address - begin);
    return offset % blockSize_ == 0;
}

} // namespace talal::core
