#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <atomic>

namespace D3D12MA {

enum ALLOCATOR_FLAGS {
    ALLOCATOR_FLAG_NONE = 0
};

struct ALLOCATOR_DESC {
    ALLOCATOR_FLAGS Flags = ALLOCATOR_FLAG_NONE;
    ID3D12Device* pDevice = nullptr;
    IDXGIAdapter* pAdapter = nullptr;
};

struct ALLOCATION_DESC {
    D3D12_HEAP_TYPE HeapType = D3D12_HEAP_TYPE_DEFAULT;
};

class Allocation {
public:
    explicit Allocation(ID3D12Resource* resource) noexcept;
    ULONG AddRef() noexcept;
    ULONG Release() noexcept;
    ID3D12Resource* GetResource() const noexcept { return resource_; }

private:
    ~Allocation() noexcept;

    std::atomic<ULONG> refCount_ = 1;
    ID3D12Resource* resource_ = nullptr;
};

class Allocator {
public:
    explicit Allocator(ID3D12Device* device) noexcept;
    ULONG AddRef() noexcept;
    ULONG Release() noexcept;
    HRESULT CreateResource(
        const ALLOCATION_DESC* allocationDesc,
        const D3D12_RESOURCE_DESC* resourceDesc,
        D3D12_RESOURCE_STATES initialState,
        const D3D12_CLEAR_VALUE* optimizedClearValue,
        Allocation** allocation,
        REFIID riidResource,
        void** resource) noexcept;

private:
    ~Allocator() noexcept;

    std::atomic<ULONG> refCount_ = 1;
    ID3D12Device* device_ = nullptr;
};

HRESULT CreateAllocator(const ALLOCATOR_DESC* desc, Allocator** allocator) noexcept;

} // namespace D3D12MA
