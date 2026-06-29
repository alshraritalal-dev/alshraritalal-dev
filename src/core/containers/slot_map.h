#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace talal::core {

struct SlotMapHandle {
    std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t generation = 0;

    friend bool operator==(const SlotMapHandle&, const SlotMapHandle&) = default;
};

template <typename T>
class SlotMap {
public:
    SlotMapHandle insert(T value)
    {
        std::uint32_t index = 0;
        if (freeHead_ != kInvalid) {
            index = freeHead_;
            freeHead_ = slots_[index].nextFree;
        } else {
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.push_back({});
        }

        Slot& slot = slots_[index];
        slot.value.emplace(std::move(value));
        slot.occupied = true;
        ++size_;
        return SlotMapHandle { index, slot.generation };
    }

    bool erase(SlotMapHandle handle)
    {
        Slot* slot = slot_for(handle);
        if (!slot) {
            return false;
        }

        slot->value.reset();
        slot->occupied = false;
        ++slot->generation;
        slot->nextFree = freeHead_;
        freeHead_ = handle.index;
        --size_;
        return true;
    }

    T* get(SlotMapHandle handle)
    {
        Slot* slot = slot_for(handle);
        return slot ? std::addressof(*slot->value) : nullptr;
    }

    const T* get(SlotMapHandle handle) const
    {
        const Slot* slot = slot_for(handle);
        return slot ? std::addressof(*slot->value) : nullptr;
    }

    bool contains(SlotMapHandle handle) const
    {
        return slot_for(handle) != nullptr;
    }

    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

private:
    static constexpr std::uint32_t kInvalid = std::numeric_limits<std::uint32_t>::max();

    struct Slot {
        std::optional<T> value;
        std::uint32_t generation = 1;
        std::uint32_t nextFree = kInvalid;
        bool occupied = false;
    };

    Slot* slot_for(SlotMapHandle handle)
    {
        if (handle.index >= slots_.size()) {
            return nullptr;
        }
        Slot& slot = slots_[handle.index];
        if (!slot.occupied || slot.generation != handle.generation) {
            return nullptr;
        }
        return &slot;
    }

    const Slot* slot_for(SlotMapHandle handle) const
    {
        if (handle.index >= slots_.size()) {
            return nullptr;
        }
        const Slot& slot = slots_[handle.index];
        if (!slot.occupied || slot.generation != handle.generation) {
            return nullptr;
        }
        return &slot;
    }

    std::vector<Slot> slots_;
    std::uint32_t freeHead_ = kInvalid;
    std::size_t size_ = 0;
};

} // namespace talal::core
