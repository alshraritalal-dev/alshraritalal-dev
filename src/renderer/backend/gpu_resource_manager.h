#pragma once

#include "renderer/backend/dx12_device.h"

#include <D3D12MemAlloc.h>

#include <mutex>
#include <vector>

namespace talal::renderer {

enum class ResourceUsage : std::uint32_t {
    None = 0,
    VertexBuffer = 1u << 0,
    IndexBuffer = 1u << 1,
    ConstantBuffer = 1u << 2,
    ShaderResource = 1u << 3,
    RenderTarget = 1u << 4,
    DepthStencil = 1u << 5,
    UnorderedAccess = 1u << 6,
    CopySource = 1u << 7,
    CopyDest = 1u << 8
};

constexpr ResourceUsage operator|(ResourceUsage left, ResourceUsage right) noexcept
{
    return static_cast<ResourceUsage>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

constexpr bool HasUsage(ResourceUsage value, ResourceUsage flag) noexcept
{
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

struct ResourceHandle {
    std::uint32_t index = UINT32_MAX;
    std::uint32_t generation = 0;

    bool valid() const noexcept { return index != UINT32_MAX; }
    friend bool operator==(const ResourceHandle&, const ResourceHandle&) = default;
};

class GPUResourceManager {
public:
    bool Initialize(DX12Device& device);
    void Shutdown() noexcept;

    ResourceHandle CreateBuffer(std::uint64_t sizeBytes, ResourceUsage usage, D3D12_HEAP_TYPE heapType);
    ResourceHandle CreateTexture2D(
        std::uint32_t width,
        std::uint32_t height,
        DXGI_FORMAT format,
        std::uint16_t mips,
        ResourceUsage usage,
        DXGI_FORMAT clearFormat = DXGI_FORMAT_UNKNOWN);
    void Destroy(ResourceHandle handle) noexcept;

    ID3D12Resource* Get(ResourceHandle handle) noexcept;
    const D3D12_RESOURCE_DESC* Description(ResourceHandle handle) const noexcept;

private:
    struct ResourceRecord {
        ComPtr<ID3D12Resource> resource;
        D3D12MA::Allocation* allocation = nullptr;
        D3D12_RESOURCE_DESC desc {};
        std::uint32_t generation = 1;
        std::uint32_t nextFree = UINT32_MAX;
        bool occupied = false;
    };

    ResourceHandle insert_record(ResourceRecord record);
    ResourceRecord* record_for(ResourceHandle handle) noexcept;
    const ResourceRecord* record_for(ResourceHandle handle) const noexcept;
    static D3D12_RESOURCE_FLAGS ResourceFlags(ResourceUsage usage) noexcept;
    static D3D12_RESOURCE_STATES InitialState(ResourceUsage usage, D3D12_HEAP_TYPE heapType) noexcept;

    mutable std::mutex mutex_;
    ComPtr<D3D12MA::Allocator> allocator_;
    std::vector<ResourceRecord> records_;
    std::uint32_t freeHead_ = UINT32_MAX;
};

} // namespace talal::renderer
