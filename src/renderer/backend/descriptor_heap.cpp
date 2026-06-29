#include "renderer/backend/descriptor_heap.h"

namespace talal::renderer {

bool DescriptorHeapManager::Initialize(ID3D12Device* device)
{
    if (!device) {
        LogError(L"DescriptorHeapManager::Initialize null device", E_POINTER);
        return false;
    }
    device_ = device;

    try {
        for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(HeapType::Count); ++i) {
            const auto type = static_cast<HeapType>(i);
            HeapState& state = heaps_[i];
            state.d3dType = ToD3DType(type);
            state.capacity = Capacity(type);
            state.shaderVisible = ShaderVisible(type);
            state.descriptorSize = device_->GetDescriptorHandleIncrementSize(state.d3dType);

            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = state.d3dType;
            desc.NumDescriptors = state.capacity;
            desc.Flags = state.shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            desc.NodeMask = 0;
            HR_CHECK(device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&state.heap)), L"CreateDescriptorHeap");
        }
        return true;
    } catch (const Dx12Exception&) {
        Shutdown();
        return false;
    }
}

void DescriptorHeapManager::Shutdown() noexcept
{
    for (HeapState& state : heaps_) {
        std::scoped_lock lock(state.mutex);
        state.heap.Reset();
        state.next = 0;
        state.freeList.clear();
    }
    device_ = nullptr;
}

DescriptorHandle DescriptorHeapManager::Allocate(HeapType type) noexcept
{
    HeapState& state = State(type);
    std::scoped_lock lock(state.mutex);

    std::uint32_t index = UINT32_MAX;
    if (!state.freeList.empty()) {
        index = state.freeList.back();
        state.freeList.pop_back();
    } else if (state.next < state.capacity) {
        index = state.next++;
    } else {
        LogError(L"Descriptor heap overflow", E_OUTOFMEMORY);
        return {};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE cpu = state.heap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(index) * state.descriptorSize;

    D3D12_GPU_DESCRIPTOR_HANDLE gpu {};
    if (state.shaderVisible) {
        gpu = state.heap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += static_cast<UINT64>(index) * state.descriptorSize;
    }

    return DescriptorHandle { type, index, cpu, gpu };
}

void DescriptorHeapManager::Free(DescriptorHandle handle) noexcept
{
    if (!handle.valid()) {
        return;
    }
    HeapState& state = State(handle.heapType);
    std::scoped_lock lock(state.mutex);
    if (handle.index < state.capacity) {
        state.freeList.push_back(handle.index);
    }
}

ID3D12DescriptorHeap* DescriptorHeapManager::Heap(HeapType type) noexcept
{
    return State(type).heap.Get();
}

UINT DescriptorHeapManager::DescriptorSize(HeapType type) const noexcept
{
    return State(type).descriptorSize;
}

D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapManager::ToD3DType(HeapType type) noexcept
{
    switch (type) {
    case HeapType::RTV:
        return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    case HeapType::DSV:
        return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    case HeapType::CBV_SRV_UAV:
        return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    case HeapType::Sampler:
        return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    default:
        return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    }
}

std::uint32_t DescriptorHeapManager::Capacity(HeapType type) noexcept
{
    switch (type) {
    case HeapType::RTV:
        return 64;
    case HeapType::DSV:
        return 16;
    case HeapType::CBV_SRV_UAV:
        return 65536;
    case HeapType::Sampler:
        return 2048;
    default:
        return 0;
    }
}

bool DescriptorHeapManager::ShaderVisible(HeapType type) noexcept
{
    return type == HeapType::CBV_SRV_UAV || type == HeapType::Sampler;
}

DescriptorHeapManager::HeapState& DescriptorHeapManager::State(HeapType type) noexcept
{
    return heaps_[static_cast<std::size_t>(type)];
}

const DescriptorHeapManager::HeapState& DescriptorHeapManager::State(HeapType type) const noexcept
{
    return heaps_[static_cast<std::size_t>(type)];
}

} // namespace talal::renderer
