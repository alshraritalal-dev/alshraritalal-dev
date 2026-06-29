#pragma once

#include "renderer/backend/dx12_common.h"

#include <array>
#include <mutex>

namespace talal::renderer {

enum class QueueType : std::uint8_t {
    Direct = 0,
    Compute = 1,
    Copy = 2,
    Count = 3
};

class DX12CommandQueues {
public:
    bool Initialize(ID3D12Device* device);
    void Shutdown() noexcept;

    ID3D12CommandQueue* Get(QueueType type) noexcept;
    UINT64 Signal(QueueType type) noexcept;
    void WaitForFence(QueueType type, UINT64 value) noexcept;
    void WaitForGPU(QueueType type) noexcept;
    void FlushAll() noexcept;

private:
    struct QueueState {
        ComPtr<ID3D12CommandQueue> queue;
        ComPtr<ID3D12Fence> fence;
        UINT64 fenceValue = 0;
        HANDLE fenceEvent = nullptr;
        std::mutex mutex;
    };

    static D3D12_COMMAND_LIST_TYPE CommandListType(QueueType type) noexcept;
    QueueState& State(QueueType type) noexcept;

    std::array<QueueState, static_cast<std::size_t>(QueueType::Count)> queues_;
};

} // namespace talal::renderer
