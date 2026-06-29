#pragma once

#include "renderer/backend/dx12_command_queue.h"

#include <cstddef>
#include <mutex>

namespace talal::renderer {

class UploadRingBuffer {
public:
    static constexpr std::size_t kDefaultCapacity = 256ull * 1024ull * 1024ull;

    bool Initialize(ID3D12Device* device, DX12CommandQueues* queues, std::size_t capacityBytes = kDefaultCapacity);
    void Shutdown() noexcept;

    void* MapRegion(std::size_t bytes, std::size_t alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    D3D12_GPU_VIRTUAL_ADDRESS GPUAddress(void* mapped) const noexcept;
    void Flush(ID3D12GraphicsCommandList7* cmd) noexcept;
    ID3D12Resource* Resource() noexcept { return resource_.Get(); }

private:
    static std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept;

    mutable std::mutex mutex_;
    DX12CommandQueues* queues_ = nullptr;
    ComPtr<ID3D12Resource> resource_;
    std::byte* mapped_ = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpuBase_ = 0;
    std::size_t capacity_ = 0;
    std::size_t offset_ = 0;
};

} // namespace talal::renderer
