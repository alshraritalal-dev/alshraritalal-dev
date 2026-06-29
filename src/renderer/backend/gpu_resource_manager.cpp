#include "renderer/backend/gpu_resource_manager.h"

namespace talal::renderer {

bool GPUResourceManager::Initialize(DX12Device& device)
{
    try {
        D3D12MA::ALLOCATOR_DESC desc = {};
        desc.Flags = D3D12MA::ALLOCATOR_FLAG_NONE;
        desc.pDevice = device.GetDevice();
        desc.pAdapter = device.GetAdapter();
        HR_CHECK(D3D12MA::CreateAllocator(&desc, &allocator_), L"D3D12MA::CreateAllocator");
        return true;
    } catch (const Dx12Exception&) {
        Shutdown();
        return false;
    }
}

void GPUResourceManager::Shutdown() noexcept
{
    std::scoped_lock lock(mutex_);
    for (ResourceRecord& record : records_) {
        if (record.allocation) {
            record.allocation->Release();
            record.allocation = nullptr;
        }
        record.resource.Reset();
        record.occupied = false;
    }
    records_.clear();
    freeHead_ = UINT32_MAX;
    allocator_.Reset();
}

ResourceHandle GPUResourceManager::CreateBuffer(std::uint64_t sizeBytes, ResourceUsage usage, D3D12_HEAP_TYPE heapType)
{
    if (!allocator_ || sizeBytes == 0) {
        HR_CHECK(E_INVALIDARG, L"GPUResourceManager::CreateBuffer invalid argument");
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = sizeBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = ResourceFlags(usage);

    D3D12MA::ALLOCATION_DESC allocationDesc = {};
    allocationDesc.HeapType = heapType;

    ResourceRecord record;
    record.desc = desc;
    HR_CHECK(allocator_->CreateResource(
        &allocationDesc,
        &desc,
        InitialState(usage, heapType),
        nullptr,
        &record.allocation,
        IID_PPV_ARGS(&record.resource)), L"D3D12MA::Allocator::CreateResource buffer");
    return insert_record(std::move(record));
}

ResourceHandle GPUResourceManager::CreateTexture2D(
    std::uint32_t width,
    std::uint32_t height,
    DXGI_FORMAT format,
    std::uint16_t mips,
    ResourceUsage usage,
    DXGI_FORMAT clearFormat)
{
    if (!allocator_ || width == 0 || height == 0 || format == DXGI_FORMAT_UNKNOWN) {
        HR_CHECK(E_INVALIDARG, L"GPUResourceManager::CreateTexture2D invalid argument");
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = std::max<std::uint16_t>(1, mips);
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = ResourceFlags(usage);

    D3D12_CLEAR_VALUE clearValue = {};
    D3D12_CLEAR_VALUE* clearPtr = nullptr;
    const DXGI_FORMAT optimizedClearFormat = clearFormat == DXGI_FORMAT_UNKNOWN ? format : clearFormat;
    if (HasUsage(usage, ResourceUsage::RenderTarget)) {
        clearValue.Format = optimizedClearFormat;
        clearValue.Color[0] = 0.0f;
        clearValue.Color[1] = 0.0f;
        clearValue.Color[2] = 0.0f;
        clearValue.Color[3] = 1.0f;
        clearPtr = &clearValue;
    } else if (HasUsage(usage, ResourceUsage::DepthStencil)) {
        clearValue.Format = optimizedClearFormat;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;
        clearPtr = &clearValue;
    }

    D3D12MA::ALLOCATION_DESC allocationDesc = {};
    allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    ResourceRecord record;
    record.desc = desc;
    HR_CHECK(allocator_->CreateResource(
        &allocationDesc,
        &desc,
        InitialState(usage, D3D12_HEAP_TYPE_DEFAULT),
        clearPtr,
        &record.allocation,
        IID_PPV_ARGS(&record.resource)), L"D3D12MA::Allocator::CreateResource texture");
    return insert_record(std::move(record));
}

void GPUResourceManager::Destroy(ResourceHandle handle) noexcept
{
    std::scoped_lock lock(mutex_);
    ResourceRecord* record = record_for(handle);
    if (!record) {
        return;
    }
    if (record->allocation) {
        record->allocation->Release();
        record->allocation = nullptr;
    }
    record->resource.Reset();
    record->occupied = false;
    ++record->generation;
    record->nextFree = freeHead_;
    freeHead_ = handle.index;
}

ID3D12Resource* GPUResourceManager::Get(ResourceHandle handle) noexcept
{
    std::scoped_lock lock(mutex_);
    ResourceRecord* record = record_for(handle);
    return record ? record->resource.Get() : nullptr;
}

const D3D12_RESOURCE_DESC* GPUResourceManager::Description(ResourceHandle handle) const noexcept
{
    std::scoped_lock lock(mutex_);
    const ResourceRecord* record = record_for(handle);
    return record ? &record->desc : nullptr;
}

ResourceHandle GPUResourceManager::insert_record(ResourceRecord record)
{
    std::scoped_lock lock(mutex_);
    std::uint32_t index = 0;
    if (freeHead_ != UINT32_MAX) {
        index = freeHead_;
        freeHead_ = records_[index].nextFree;
        record.generation = records_[index].generation;
        records_[index] = std::move(record);
    } else {
        index = static_cast<std::uint32_t>(records_.size());
        records_.push_back(std::move(record));
    }
    records_[index].occupied = true;
    return ResourceHandle { index, records_[index].generation };
}

GPUResourceManager::ResourceRecord* GPUResourceManager::record_for(ResourceHandle handle) noexcept
{
    if (handle.index >= records_.size()) {
        return nullptr;
    }
    ResourceRecord& record = records_[handle.index];
    if (!record.occupied || record.generation != handle.generation) {
        return nullptr;
    }
    return &record;
}

const GPUResourceManager::ResourceRecord* GPUResourceManager::record_for(ResourceHandle handle) const noexcept
{
    if (handle.index >= records_.size()) {
        return nullptr;
    }
    const ResourceRecord& record = records_[handle.index];
    if (!record.occupied || record.generation != handle.generation) {
        return nullptr;
    }
    return &record;
}

D3D12_RESOURCE_FLAGS GPUResourceManager::ResourceFlags(ResourceUsage usage) noexcept
{
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (HasUsage(usage, ResourceUsage::RenderTarget)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (HasUsage(usage, ResourceUsage::DepthStencil)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }
    if (HasUsage(usage, ResourceUsage::UnorderedAccess)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    if (HasUsage(usage, ResourceUsage::DepthStencil) && !HasUsage(usage, ResourceUsage::ShaderResource)) {
        flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    }
    return flags;
}

D3D12_RESOURCE_STATES GPUResourceManager::InitialState(ResourceUsage usage, D3D12_HEAP_TYPE heapType) noexcept
{
    if (heapType == D3D12_HEAP_TYPE_UPLOAD) {
        return D3D12_RESOURCE_STATE_GENERIC_READ;
    }
    if (heapType == D3D12_HEAP_TYPE_READBACK) {
        return D3D12_RESOURCE_STATE_COPY_DEST;
    }
    if (HasUsage(usage, ResourceUsage::RenderTarget)) {
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    if (HasUsage(usage, ResourceUsage::DepthStencil)) {
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

} // namespace talal::renderer
