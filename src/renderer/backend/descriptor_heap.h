#pragma once

#include "renderer/backend/dx12_common.h"

#include <array>
#include <mutex>
#include <vector>

namespace talal::renderer {

enum class HeapType : std::uint8_t {
    RTV = 0,
    DSV = 1,
    CBV_SRV_UAV = 2,
    Sampler = 3,
    Count = 4
};

struct DescriptorHandle {
    HeapType heapType = HeapType::RTV;
    std::uint32_t index = UINT32_MAX;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu {};

    bool valid() const noexcept { return index != UINT32_MAX && cpu.ptr != 0; }
};

class DescriptorHeapManager {
public:
    bool Initialize(ID3D12Device* device);
    void Shutdown() noexcept;

    DescriptorHandle Allocate(HeapType type) noexcept;
    void Free(DescriptorHandle handle) noexcept;
    ID3D12DescriptorHeap* Heap(HeapType type) noexcept;
    ID3D12DescriptorHeap* ShaderVisibleHeap() noexcept { return Heap(HeapType::CBV_SRV_UAV); }
    UINT DescriptorSize(HeapType type) const noexcept;

private:
    struct HeapState {
        ComPtr<ID3D12DescriptorHeap> heap;
        D3D12_DESCRIPTOR_HEAP_TYPE d3dType = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        std::uint32_t capacity = 0;
        UINT descriptorSize = 0;
        std::uint32_t next = 0;
        bool shaderVisible = false;
        std::vector<std::uint32_t> freeList;
        std::mutex mutex;
    };

    static D3D12_DESCRIPTOR_HEAP_TYPE ToD3DType(HeapType type) noexcept;
    static std::uint32_t Capacity(HeapType type) noexcept;
    static bool ShaderVisible(HeapType type) noexcept;
    HeapState& State(HeapType type) noexcept;
    const HeapState& State(HeapType type) const noexcept;

    ID3D12Device* device_ = nullptr;
    std::array<HeapState, static_cast<std::size_t>(HeapType::Count)> heaps_;
};

} // namespace talal::renderer
