#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace talal::core {

class TlsfHeap {
public:
    explicit TlsfHeap(std::size_t capacityBytes);
    ~TlsfHeap();

    TlsfHeap(const TlsfHeap&) = delete;
    TlsfHeap& operator=(const TlsfHeap&) = delete;

    void* allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t));
    void deallocate(void* pointer) noexcept;

    bool owns(const void* pointer) const noexcept;
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t used() const noexcept { return usedBytes_; }
    std::size_t free_bytes() const noexcept { return capacity_ - usedBytes_; }
    std::size_t largest_free_block() const noexcept;

private:
    struct BlockHeader {
        std::size_t size = 0;
        BlockHeader* prevPhysical = nullptr;
        BlockHeader* prevFree = nullptr;
        BlockHeader* nextFree = nullptr;
        std::uint8_t firstLevel = 0;
        std::uint8_t secondLevel = 0;
        bool free = false;
    };

    static constexpr std::size_t kFirstLevelCount = 32;
    static constexpr std::size_t kSecondLevelCount = 8;
    static constexpr std::size_t kAlignment = alignof(std::max_align_t);
    static constexpr std::size_t kMinSplitPayload = 32;

    using FreeLists = std::array<std::array<BlockHeader*, kSecondLevelCount>, kFirstLevelCount>;

    static std::size_t align_up(std::size_t value, std::size_t alignment) noexcept;
    static std::size_t header_size() noexcept;
    static std::pair<std::uint8_t, std::uint8_t> mapping(std::size_t size) noexcept;

    BlockHeader* physical_next(BlockHeader* block) const noexcept;
    const BlockHeader* physical_next(const BlockHeader* block) const noexcept;
    void insert_free(BlockHeader* block) noexcept;
    void remove_free(BlockHeader* block) noexcept;
    BlockHeader* find_suitable(std::size_t size) const noexcept;
    void split_block(BlockHeader* block, std::size_t requestedSize) noexcept;
    BlockHeader* header_from_payload(void* pointer) const noexcept;

    std::byte* memory_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t usedBytes_ = 0;
    FreeLists freeLists_ {};
    std::uint32_t firstLevelBitmap_ = 0;
    std::array<std::uint8_t, kFirstLevelCount> secondLevelBitmap_ {};
};

} // namespace talal::core
