#pragma once

#include "renderer/backend/dx12_command_queue.h"

#include <array>

namespace talal::renderer {

class DX12CommandListPool {
public:
    bool Initialize(ID3D12Device10* device, DX12CommandQueues* queues);
    void Shutdown() noexcept;

    ID3D12GraphicsCommandList7* Reset(std::uint32_t frameIndex, QueueType type);
    void Close();
    UINT64 Submit(QueueType type);
    void BeginProfileEvent(const wchar_t* name, std::uint64_t color = 0xff00a2ffull) noexcept;
    void EndProfileEvent() noexcept;

private:
    struct FrameCommands {
        ComPtr<ID3D12CommandAllocator> directAllocator;
        ComPtr<ID3D12CommandAllocator> computeAllocator;
        ComPtr<ID3D12CommandAllocator> copyAllocator;
        std::array<UINT64, static_cast<std::size_t>(QueueType::Count)> fenceValues {};
    };

    ID3D12CommandAllocator* AllocatorFor(FrameCommands& frame, QueueType type) noexcept;
    UINT64& FenceValueFor(FrameCommands& frame, QueueType type) noexcept;

    ID3D12Device10* device_ = nullptr;
    DX12CommandQueues* queues_ = nullptr;
    std::array<FrameCommands, kFramesInFlight> frames_;
    ComPtr<ID3D12GraphicsCommandList7> commandList_;
    QueueType activeType_ = QueueType::Direct;
    std::uint32_t activeFrameIndex_ = 0;
    bool open_ = false;
};

} // namespace talal::renderer
