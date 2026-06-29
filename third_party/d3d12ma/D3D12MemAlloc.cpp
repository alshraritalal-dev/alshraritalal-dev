#include "D3D12MemAlloc.h"

#include <new>

namespace D3D12MA {

Allocation::Allocation(ID3D12Resource* resource) noexcept
    : resource_(resource)
{
    if (resource_) {
        resource_->AddRef();
    }
}

Allocation::~Allocation() noexcept
{
    if (resource_) {
        resource_->Release();
        resource_ = nullptr;
    }
}

ULONG Allocation::AddRef() noexcept
{
    return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG Allocation::Release() noexcept
{
    const ULONG value = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (value == 0) {
        delete this;
    }
    return value;
}

Allocator::Allocator(ID3D12Device* device) noexcept
    : device_(device)
{
    if (device_) {
        device_->AddRef();
    }
}

Allocator::~Allocator() noexcept
{
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
}

ULONG Allocator::AddRef() noexcept
{
    return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG Allocator::Release() noexcept
{
    const ULONG value = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (value == 0) {
        delete this;
    }
    return value;
}

HRESULT Allocator::CreateResource(
    const ALLOCATION_DESC* allocationDesc,
    const D3D12_RESOURCE_DESC* resourceDesc,
    D3D12_RESOURCE_STATES initialState,
    const D3D12_CLEAR_VALUE* optimizedClearValue,
    Allocation** allocation,
    REFIID riidResource,
    void** resource) noexcept
{
    if (!device_ || !allocationDesc || !resourceDesc || !allocation || !resource) {
        return E_INVALIDARG;
    }

    *allocation = nullptr;
    *resource = nullptr;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = allocationDesc->HeapType;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    ID3D12Resource* created = nullptr;
    const HRESULT hr = device_->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        resourceDesc,
        initialState,
        optimizedClearValue,
        __uuidof(ID3D12Resource),
        reinterpret_cast<void**>(&created));
    if (FAILED(hr)) {
        return hr;
    }

    Allocation* createdAllocation = new (std::nothrow) Allocation(created);
    if (!createdAllocation) {
        created->Release();
        return E_OUTOFMEMORY;
    }

    const HRESULT qi = created->QueryInterface(riidResource, resource);
    created->Release();
    if (FAILED(qi)) {
        createdAllocation->Release();
        return qi;
    }

    *allocation = createdAllocation;
    return S_OK;
}

HRESULT CreateAllocator(const ALLOCATOR_DESC* desc, Allocator** allocator) noexcept
{
    if (!desc || !desc->pDevice || !allocator) {
        return E_INVALIDARG;
    }
    *allocator = new (std::nothrow) Allocator(desc->pDevice);
    return *allocator ? S_OK : E_OUTOFMEMORY;
}

} // namespace D3D12MA
