#include "renderer/backend/dx12_command_queue.h"

namespace talal::renderer {

bool DX12CommandQueues::Initialize(ID3D12Device* device)
{
    if (!device) {
        LogError(L"DX12CommandQueues::Initialize received null device", E_POINTER);
        return false;
    }

    try {
        for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(QueueType::Count); ++i) {
            const auto type = static_cast<QueueType>(i);
            QueueState& state = queues_[i];

            D3D12_COMMAND_QUEUE_DESC desc = {};
            desc.Type = CommandListType(type);
            desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            desc.NodeMask = 0;
            HR_CHECK(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&state.queue)), L"CreateCommandQueue");
            HR_CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&state.fence)), L"CreateFence");
            state.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!state.fenceEvent) {
                HR_CHECK(HRESULT_FROM_WIN32(GetLastError()), L"CreateEvent fence");
            }
        }
        return true;
    } catch (const Dx12Exception&) {
        Shutdown();
        return false;
    }
}

void DX12CommandQueues::Shutdown() noexcept
{
    FlushAll();
    for (QueueState& state : queues_) {
        if (state.fenceEvent) {
            CloseHandle(state.fenceEvent);
            state.fenceEvent = nullptr;
        }
        state.fence.Reset();
        state.queue.Reset();
        state.fenceValue = 0;
    }
}

ID3D12CommandQueue* DX12CommandQueues::Get(QueueType type) noexcept
{
    return State(type).queue.Get();
}

UINT64 DX12CommandQueues::Signal(QueueType type) noexcept
{
    QueueState& state = State(type);
    std::scoped_lock lock(state.mutex);
    const UINT64 value = ++state.fenceValue;
    const HRESULT hr = state.queue ? state.queue->Signal(state.fence.Get(), value) : E_POINTER;
    if (FAILED(hr)) {
        LogError(L"ID3D12CommandQueue::Signal", hr);
        return 0;
    }
    return value;
}

void DX12CommandQueues::WaitForFence(QueueType type, UINT64 value) noexcept
{
    if (value == 0) {
        return;
    }

    QueueState& state = State(type);
    std::scoped_lock lock(state.mutex);
    if (!state.fence || !state.fenceEvent || state.fence->GetCompletedValue() >= value) {
        return;
    }

    const HRESULT hr = state.fence->SetEventOnCompletion(value, state.fenceEvent);
    if (FAILED(hr)) {
        LogError(L"ID3D12Fence::SetEventOnCompletion", hr);
        return;
    }
    WaitForSingleObject(state.fenceEvent, INFINITE);
}

void DX12CommandQueues::WaitForGPU(QueueType type) noexcept
{
    QueueState& state = State(type);
    const UINT64 value = Signal(type);
    if (value == 0 || !state.fence) {
        return;
    }
    WaitForFence(type, value);
}

void DX12CommandQueues::FlushAll() noexcept
{
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(QueueType::Count); ++i) {
        WaitForGPU(static_cast<QueueType>(i));
    }
}

D3D12_COMMAND_LIST_TYPE DX12CommandQueues::CommandListType(QueueType type) noexcept
{
    switch (type) {
    case QueueType::Direct:
        return D3D12_COMMAND_LIST_TYPE_DIRECT;
    case QueueType::Compute:
        return D3D12_COMMAND_LIST_TYPE_COMPUTE;
    case QueueType::Copy:
        return D3D12_COMMAND_LIST_TYPE_COPY;
    default:
        return D3D12_COMMAND_LIST_TYPE_DIRECT;
    }
}

DX12CommandQueues::QueueState& DX12CommandQueues::State(QueueType type) noexcept
{
    return queues_[static_cast<std::size_t>(type)];
}

} // namespace talal::renderer
