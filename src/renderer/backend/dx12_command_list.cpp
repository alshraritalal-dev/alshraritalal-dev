#include "renderer/backend/dx12_command_list.h"

#if defined(TALAL_PROFILE_BUILD)
#include <pix3.h>
#endif

namespace talal::renderer {

bool DX12CommandListPool::Initialize(ID3D12Device10* device, DX12CommandQueues* queues)
{
    if (!device || !queues) {
        LogError(L"DX12CommandListPool::Initialize null dependency", E_POINTER);
        return false;
    }

    try {
        device_ = device;
        queues_ = queues;
        for (FrameCommands& frame : frames_) {
            HR_CHECK(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.directAllocator)), L"CreateCommandAllocator Direct");
            HR_CHECK(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&frame.computeAllocator)), L"CreateCommandAllocator Compute");
            HR_CHECK(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&frame.copyAllocator)), L"CreateCommandAllocator Copy");
        }
        HR_CHECK(device_->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&commandList_)), L"CreateCommandList1");
        return true;
    } catch (const Dx12Exception&) {
        Shutdown();
        return false;
    }
}

void DX12CommandListPool::Shutdown() noexcept
{
    commandList_.Reset();
    for (FrameCommands& frame : frames_) {
        frame.directAllocator.Reset();
        frame.computeAllocator.Reset();
        frame.copyAllocator.Reset();
    }
    device_ = nullptr;
    queues_ = nullptr;
    open_ = false;
}

ID3D12GraphicsCommandList7* DX12CommandListPool::Reset(std::uint32_t frameIndex, QueueType type)
{
    activeFrameIndex_ = frameIndex % kFramesInFlight;
    FrameCommands& frame = frames_[activeFrameIndex_];
    queues_->WaitForFence(type, FenceValueFor(frame, type));
    ID3D12CommandAllocator* allocator = AllocatorFor(frame, type);
    HR_CHECK(allocator->Reset(), L"ID3D12CommandAllocator::Reset");
    HR_CHECK(commandList_->Reset(allocator, nullptr), L"ID3D12GraphicsCommandList::Reset");
    activeType_ = type;
    open_ = true;
    return commandList_.Get();
}

void DX12CommandListPool::Close()
{
    if (open_) {
        HR_CHECK(commandList_->Close(), L"ID3D12GraphicsCommandList::Close");
        open_ = false;
    }
}

UINT64 DX12CommandListPool::Submit(QueueType type)
{
    Close();
    ID3D12CommandList* lists[] = { commandList_.Get() };
    queues_->Get(type)->ExecuteCommandLists(1, lists);
    const UINT64 fenceValue = queues_->Signal(type);
    FenceValueFor(frames_[activeFrameIndex_], type) = fenceValue;
    return fenceValue;
}

void DX12CommandListPool::BeginProfileEvent(const wchar_t* name, std::uint64_t color) noexcept
{
#if defined(TALAL_PROFILE_BUILD)
    if (commandList_ && name) {
        PIXBeginEvent(commandList_.Get(), color, name);
    }
#else
    (void)name;
    (void)color;
#endif
}

void DX12CommandListPool::EndProfileEvent() noexcept
{
#if defined(TALAL_PROFILE_BUILD)
    if (commandList_) {
        PIXEndEvent(commandList_.Get());
    }
#endif
}

ID3D12CommandAllocator* DX12CommandListPool::AllocatorFor(FrameCommands& frame, QueueType type) noexcept
{
    switch (type) {
    case QueueType::Direct:
        return frame.directAllocator.Get();
    case QueueType::Compute:
        return frame.computeAllocator.Get();
    case QueueType::Copy:
        return frame.copyAllocator.Get();
    default:
        return frame.directAllocator.Get();
    }
}

UINT64& DX12CommandListPool::FenceValueFor(FrameCommands& frame, QueueType type) noexcept
{
    return frame.fenceValues[static_cast<std::size_t>(type)];
}

} // namespace talal::renderer
