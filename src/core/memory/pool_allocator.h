#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace talal::core {

class PoolAllocator {
public:
    PoolAllocator(std::size_t blockSize, std::size_t blockCount, std::size_t alignment = alignof(std::max_align_t));

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    void* allocate() noexcept;
    void deallocate(void* pointer) noexcept;

    bool owns(const void* pointer) const noexcept;
    std::size_t block_size() const noexcept { return blockSize_; }
    std::size_t capacity() const noexcept { return blockCount_; }
    std::size_t used() const noexcept { return usedBlocks_; }
    std::size_t free_count() const noexcept { return blockCount_ - usedBlocks_; }

private:
    struct FreeNode {
        FreeNode* next = nullptr;
    };

    std::vector<std::byte> storage_;
    std::size_t blockSize_ = 0;
    std::size_t blockCount_ = 0;
    std::size_t alignment_ = 0;
    std::size_t usedBlocks_ = 0;
    FreeNode* freeList_ = nullptr;
};

} // namespace talal::core
