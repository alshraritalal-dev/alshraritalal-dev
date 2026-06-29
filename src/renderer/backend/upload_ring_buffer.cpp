#include "renderer/backend/upload_ring_buffer.h"

namespace talal::renderer {

bool UploadRingBuffer::Initialize(ID3D12Device* device, DX12CommandQueues* queues, std::size_t capacityBytes)
{
    if (!device || !queues || capacityBytes == 0) {
        LogError(L"UploadRingBuffer::Initialize invalid argument", E_INVALIDARG);
        return false;
    }

    try {
        queues_ = queues;
        capacity_ = AlignUp(capacityBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = capacity_;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HR_CHECK(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&resource_)), L"CreateCommittedResource upload ring");

        HR_CHECK(resource_->Map(0, nullptr, reinterpret_cast<void**>(&mapped_)), L"ID3D12Resource::Map upload ring");
        gpuBase_ = resource_->GetGPUVirtualAddress();
        return true;
    } catch (const Dx12Exception&) {
        Shutdown();
        return false;
    }
}

void UploadRingBuffer::Shutdown() noexcept
{
    std::scoped_lock lock(mutex_);
    if (resource_ && mapped_) {
        resource_->Unmap(0, nullptr);
    }
    mapped_ = nullptr;
    gpuBase_ = 0;
    resource_.Reset();
    queues_ = nullptr;
    capacity_ = 0;
    offset_ = 0;
}

void* UploadRingBuffer::MapRegion(std::size_t bytes, std::size_t alignment)
{
    if (!mapped_ || bytes == 0 || bytes > capacity_) {
        HR_CHECK(E_INVALIDARG, L"UploadRingBuffer::MapRegion invalid request");
    }

    std::scoped_lock lock(mutex_);
    const std::size_t alignedOffset = AlignUp(offset_, alignment);
    if (alignedOffset + bytes > capacity_) {
        queues_->WaitForGPU(QueueType::Direct);
        offset_ = 0;
    }

    const std::size_t regionOffset = AlignUp(offset_, alignment);
    offset_ = regionOffset + bytes;
    return mapped_ + regionOffset;
}

D3D12_GPU_VIRTUAL_ADDRESS UploadRingBuffer::GPUAddress(void* mapped) const noexcept
{
    if (!mapped_ || !mapped) {
        return 0;
    }
    const auto* pointer = static_cast<const std::byte*>(mapped);
    const std::ptrdiff_t offset = pointer - mapped_;
    if (offset < 0 || static_cast<std::size_t>(offset) >= capacity_) {
        return 0;
    }
    return gpuBase_ + static_cast<std::uint64_t>(offset);
}

void UploadRingBuffer::Flush(ID3D12GraphicsCommandList7* cmd) noexcept
{
    (void)cmd;
}

std::size_t UploadRingBuffer::AlignUp(std::size_t value, std::size_t alignment) noexcept
{
    const std::size_t mask = alignment - 1;
    return (value + mask) & ~mask;
}

} // namespace talal::renderer
