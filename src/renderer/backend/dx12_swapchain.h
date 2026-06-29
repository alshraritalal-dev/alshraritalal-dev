#pragma once

#include "renderer/backend/descriptor_heap.h"
#include "renderer/backend/dx12_command_queue.h"

#include <array>
#include <chrono>

namespace talal::renderer {

struct SwapchainDesc {
    HWND hwnd = nullptr;
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    bool hdr10 = false;
    bool vsync = false;
    int frameLimit = 240;
};

class FrameLimiter {
public:
    void Wait(int frameLimit) noexcept;

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point nextFrame_ {};
};

class DX12Swapchain {
public:
    bool Initialize(
        IDXGIFactory7* factory,
        ID3D12Device* device,
        DX12CommandQueues* queues,
        DescriptorHeapManager* descriptors,
        const SwapchainDesc& desc);
    void Shutdown() noexcept;

    bool Resize(std::uint32_t width, std::uint32_t height);
    void Present();
    ID3D12Resource* CurrentBackBuffer() noexcept;
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentRTV() const noexcept;
    std::uint32_t CurrentFrameIndex() const noexcept { return frameIndex_; }
    std::uint32_t Width() const noexcept { return width_; }
    std::uint32_t Height() const noexcept { return height_; }
    DXGI_FORMAT Format() const noexcept { return format_; }
    bool HDR10() const noexcept { return hdr10_; }
    void SetFrameLimit(int frameLimit) noexcept { frameLimit_ = frameLimit; }
    void SetVSync(bool vsync) noexcept { vsync_ = vsync; }

private:
    bool QueryTearingSupport(IDXGIFactory7* factory) noexcept;
    bool CreateBackBufferViews();
    void ReleaseBackBuffers() noexcept;

    ID3D12Device* device_ = nullptr;
    DX12CommandQueues* queues_ = nullptr;
    DescriptorHeapManager* descriptors_ = nullptr;
    HWND hwnd_ = nullptr;
    ComPtr<IDXGISwapChain4> swapchain_;
    std::array<ComPtr<ID3D12Resource>, kFramesInFlight> backBuffers_;
    std::array<DescriptorHandle, kFramesInFlight> rtvHandles_ {};
    std::uint32_t width_ = 1920;
    std::uint32_t height_ = 1080;
    std::uint32_t frameIndex_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_R8G8B8A8_UNORM;
    bool hdr10_ = false;
    bool vsync_ = false;
    bool allowTearing_ = false;
    int frameLimit_ = 240;
    FrameLimiter limiter_;
};

} // namespace talal::renderer
