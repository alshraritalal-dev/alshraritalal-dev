#include "core/memory/tlsf_heap.h"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <malloc.h>
#include <new>

namespace talal::core {

namespace {

bool IsPowerOfTwo(std::size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

} // namespace

TlsfHeap::TlsfHeap(std::size_t capacityBytes)
{
    capacity_ = align_up(std::max(capacityBytes, header_size() + kMinSplitPayload), kAlignment);
    memory_ = static_cast<std::byte*>(_aligned_malloc(capacity_, kAlignment));
    if (!memory_) {
        throw std::bad_alloc();
    }

    auto* first = new (memory_) BlockHeader {};
    first->size = capacity_ - header_size();
    first->free = true;
    insert_free(first);
}

TlsfHeap::~TlsfHeap()
{
    _aligned_free(memory_);
}

void* TlsfHeap::allocate(std::size_t bytes, std::size_t alignment)
{
    if (bytes == 0 || !IsPowerOfTwo(alignment) || alignment > kAlignment) {
        return nullptr;
    }

    const std::size_t requested = align_up(bytes, kAlignment);
    BlockHeader* block = find_suitable(requested);
    if (!block) {
        return nullptr;
    }

    remove_free(block);
    split_block(block, requested);
    block->free = false;
    usedBytes_ += block->size;
    return reinterpret_cast<std::byte*>(block) + header_size();
}

void TlsfHeap::deallocate(void* pointer) noexcept
{
    if (!owns(pointer)) {
        return;
    }

    BlockHeader* block = header_from_payload(pointer);
    if (block->free) {
        return;
    }

    usedBytes_ -= block->size;
    block->free = true;

    if (BlockHeader* next = physical_next(block); next && next->free) {
        remove_free(next);
        block->size += header_size() + next->size;
        if (BlockHeader* after = physical_next(block)) {
            after->prevPhysical = block;
        }
    }

    if (BlockHeader* prev = block->prevPhysical; prev && prev->free) {
        remove_free(prev);
        prev->size += header_size() + block->size;
        if (BlockHeader* after = physical_next(prev)) {
            after->prevPhysical = prev;
        }
        block = prev;
    }

    insert_free(block);
}

bool TlsfHeap::owns(const void* pointer) const noexcept
{
    if (!pointer || !memory_) {
        return false;
    }
    const auto* address = static_cast<const std::byte*>(pointer);
    return address >= memory_ + header_size() && address < memory_ + capacity_;
}

std::size_t TlsfHeap::largest_free_block() const noexcept
{
    std::size_t largest = 0;
    for (const auto& firstLevel : freeLists_) {
        for (const BlockHeader* block : firstLevel) {
            for (const BlockHeader* cursor = block; cursor; cursor = cursor->nextFree) {
                largest = std::max(largest, cursor->size);
            }
        }
    }
    return largest;
}

std::size_t TlsfHeap::align_up(std::size_t value, std::size_t alignment) noexcept
{
    const std::size_t mask = alignment - 1;
    return (value + mask) & ~mask;
}

std::size_t TlsfHeap::header_size() noexcept
{
    return align_up(sizeof(BlockHeader), kAlignment);
}

std::pair<std::uint8_t, std::uint8_t> TlsfHeap::mapping(std::size_t size) noexcept
{
    size = std::max(size, kAlignment);
    const auto firstLevel = static_cast<std::uint8_t>(std::min<std::size_t>(std::bit_width(size) - 1, kFirstLevelCount - 1));
    const std::size_t base = std::size_t { 1 } << firstLevel;
    const std::size_t step = std::max<std::size_t>(1, base / kSecondLevelCount);
    const auto secondLevel = static_cast<std::uint8_t>(std::min<std::size_t>((size - base) / step, kSecondLevelCount - 1));
    return { firstLevel, secondLevel };
}

TlsfHeap::BlockHeader* TlsfHeap::physical_next(BlockHeader* block) const noexcept
{
    auto* next = reinterpret_cast<std::byte*>(block) + header_size() + block->size;
    if (next + header_size() > memory_ + capacity_) {
        return nullptr;
    }
    return reinterpret_cast<BlockHeader*>(next);
}

const TlsfHeap::BlockHeader* TlsfHeap::physical_next(const BlockHeader* block) const noexcept
{
    return physical_next(const_cast<BlockHeader*>(block));
}

void TlsfHeap::insert_free(BlockHeader* block) noexcept
{
    const auto [firstLevel, secondLevel] = mapping(block->size);
    block->firstLevel = firstLevel;
    block->secondLevel = secondLevel;
    block->free = true;
    block->prevFree = nullptr;
    block->nextFree = freeLists_[firstLevel][secondLevel];
    if (block->nextFree) {
        block->nextFree->prevFree = block;
    }
    freeLists_[firstLevel][secondLevel] = block;
    firstLevelBitmap_ |= (1u << firstLevel);
    secondLevelBitmap_[firstLevel] |= static_cast<std::uint8_t>(1u << secondLevel);
}

void TlsfHeap::remove_free(BlockHeader* block) noexcept
{
    const auto firstLevel = block->firstLevel;
    const auto secondLevel = block->secondLevel;
    if (block->prevFree) {
        block->prevFree->nextFree = block->nextFree;
    } else {
        freeLists_[firstLevel][secondLevel] = block->nextFree;
    }
    if (block->nextFree) {
        block->nextFree->prevFree = block->prevFree;
    }
    if (!freeLists_[firstLevel][secondLevel]) {
        secondLevelBitmap_[firstLevel] &= static_cast<std::uint8_t>(~(1u << secondLevel));
        if (secondLevelBitmap_[firstLevel] == 0) {
            firstLevelBitmap_ &= ~(1u << firstLevel);
        }
    }
    block->prevFree = nullptr;
    block->nextFree = nullptr;
}

TlsfHeap::BlockHeader* TlsfHeap::find_suitable(std::size_t size) const noexcept
{
    const auto [startFirst, startSecond] = mapping(size);
    for (std::size_t first = startFirst; first < kFirstLevelCount; ++first) {
        if ((firstLevelBitmap_ & (1u << first)) == 0) {
            continue;
        }
        const std::size_t secondStart = first == startFirst ? startSecond : 0;
        for (std::size_t second = secondStart; second < kSecondLevelCount; ++second) {
            if ((secondLevelBitmap_[first] & (1u << second)) == 0) {
                continue;
            }
            for (BlockHeader* block = freeLists_[first][second]; block; block = block->nextFree) {
                if (block->size >= size) {
                    return block;
                }
            }
        }
    }
    return nullptr;
}

void TlsfHeap::split_block(BlockHeader* block, std::size_t requestedSize) noexcept
{
    if (block->size < requestedSize + header_size() + kMinSplitPayload) {
        return;
    }

    const std::size_t remaining = block->size - requestedSize - header_size();
    auto* splitAddress = reinterpret_cast<std::byte*>(block) + header_size() + requestedSize;
    auto* split = new (splitAddress) BlockHeader {};
    split->size = remaining;
    split->prevPhysical = block;
    split->free = true;
    block->size = requestedSize;

    if (BlockHeader* after = physical_next(split)) {
        after->prevPhysical = split;
    }
    insert_free(split);
}

TlsfHeap::BlockHeader* TlsfHeap::header_from_payload(void* pointer) const noexcept
{
    return reinterpret_cast<BlockHeader*>(static_cast<std::byte*>(pointer) - header_size());
}

} // namespace talal::core
