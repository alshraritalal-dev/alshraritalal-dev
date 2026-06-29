#include "renderer/backend/dx12_swapchain.h"

#include <thread>
#include <sstream>

namespace talal::renderer {

void FrameLimiter::Wait(int frameLimit) noexcept
{
    if (frameLimit <= 0) {
        nextFrame_ = Clock::now();
        return;
    }

    const auto interval = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / static_cast<double>(frameLimit)));
    const auto now = Clock::now();
    if (nextFrame_ == Clock::time_point {}) {
        nextFrame_ = now + interval;
        return;
    }

    if (now < nextFrame_) {
        const auto remaining = nextFrame_ - now;
        if (remaining > std::chrono::milliseconds(2)) {
            std::this_thread::sleep_for(remaining - std::chrono::milliseconds(1));
        }
        while (Clock::now() < nextFrame_) {
            YieldProcessor();
        }
        nextFrame_ += interval;
    } else {
        nextFrame_ = now + interval;
    }
}

bool DX12Swapchain::Initialize(
    IDXGIFactory7* factory,
    ID3D12Device* device,
    DX12CommandQueues* queues,
    DescriptorHeapManager* descriptors,
    const SwapchainDesc& desc)
{
    if (!factory || !device || !queues || !descriptors || !desc.hwnd) {
        LogError(L"DX12Swapchain::Initialize null dependency", E_POINTER);
        return false;
    }

    try {
        device_ = device;
        queues_ = queues;
        descriptors_ = descriptors;
        hwnd_ = desc.hwnd;
        width_ = std::max<std::uint32_t>(1, desc.width);
        height_ = std::max<std::uint32_t>(1, desc.height);
        hdr10_ = desc.hdr10;
        vsync_ = desc.vsync;
        frameLimit_ = desc.frameLimit;
        format_ = hdr10_ ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
        allowTearing_ = QueryTearingSupport(factory);

        DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
        swapDesc.Width = width_;
        swapDesc.Height = height_;
        swapDesc.Format = format_;
        swapDesc.Stereo = FALSE;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.SampleDesc.Quality = 0;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount = kFramesInFlight;
        swapDesc.Scaling = DXGI_SCALING_STRETCH;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        swapDesc.Flags = allowTearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        ComPtr<IDXGISwapChain1> swapchain1;
        HR_CHECK(factory->CreateSwapChainForHwnd(queues_->Get(QueueType::Direct), hwnd_, &swapDesc, nullptr, nullptr, &swapchain1), L"CreateSwapChainForHwnd");
        HR_CHECK(factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER), L"MakeWindowAssociation");
        HR_CHECK(swapchain1.As(&swapchain_), L"Query IDXGISwapChain4");

        if (hdr10_) {
            UINT colorSpaceSupport = 0;
            HRESULT hr = swapchain_->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &colorSpaceSupport);
            HR_CHECK(hr, L"IDXGISwapChain4::CheckColorSpaceSupport");
            if (SUCCEEDED(hr) && (colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0) {
                HR_CHECK(swapchain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020), L"IDXGISwapChain4::SetColorSpace1 HDR10");
            }
        }

        frameIndex_ = swapchain_->GetCurrentBackBufferIndex();
        return CreateBackBufferViews();
    } catch (const Dx12Exception&) {
        Shutdown();
        return false;
    }
}

void DX12Swapchain::Shutdown() noexcept
{
    ReleaseBackBuffers();
    swapchain_.Reset();
    hwnd_ = nullptr;
    descriptors_ = nullptr;
    queues_ = nullptr;
    device_ = nullptr;
}

bool DX12Swapchain::Resize(std::uint32_t width, std::uint32_t height)
{
    width = std::max<std::uint32_t>(1, width);
    height = std::max<std::uint32_t>(1, height);
    if (!swapchain_ || (width == width_ && height == height_)) {
        return true;
    }

    try {
        queues_->FlushAll();
        ReleaseBackBuffers();
        const UINT flags = allowTearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        HR_CHECK(swapchain_->ResizeBuffers(kFramesInFlight, width, height, format_, flags), L"IDXGISwapChain::ResizeBuffers");
        width_ = width;
        height_ = height;
        frameIndex_ = swapchain_->GetCurrentBackBufferIndex();
        return CreateBackBufferViews();
    } catch (const Dx12Exception&) {
        return false;
    }
}

void DX12Swapchain::Present()
{
    limiter_.Wait(frameLimit_);
    const UINT syncInterval = vsync_ ? 1u : 0u;
    const UINT flags = (!vsync_ && allowTearing_) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    const HRESULT hr = swapchain_->Present(syncInterval, flags);
    if (FAILED(hr)) {
        std::wstringstream message;
        message << L"IDXGISwapChain::Present";
        if (device_) {
            const HRESULT removedReason = device_->GetDeviceRemovedReason();
            message << L" device_removed_reason=" << HResultToString(removedReason);
        }
        HR_CHECK(hr, message.str());
    }
    frameIndex_ = swapchain_->GetCurrentBackBufferIndex();
}

ID3D12Resource* DX12Swapchain::CurrentBackBuffer() noexcept
{
    return backBuffers_[frameIndex_].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Swapchain::CurrentRTV() const noexcept
{
    return rtvHandles_[frameIndex_].cpu;
}

bool DX12Swapchain::QueryTearingSupport(IDXGIFactory7* factory) noexcept
{
    BOOL allowTearing = FALSE;
    HRESULT hr = factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
    if (FAILED(hr)) {
        LogError(L"DXGI_FEATURE_PRESENT_ALLOW_TEARING", hr);
        return false;
    }
    return allowTearing == TRUE;
}

bool DX12Swapchain::CreateBackBufferViews()
{
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        HR_CHECK(swapchain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i])), L"IDXGISwapChain::GetBuffer");
        rtvHandles_[i] = descriptors_->Allocate(HeapType::RTV);
        if (!rtvHandles_[i].valid()) {
            HR_CHECK(E_OUTOFMEMORY, L"Allocate swapchain RTV");
        }
        device_->CreateRenderTargetView(backBuffers_[i].Get(), nullptr, rtvHandles_[i].cpu);
    }
    return true;
}

void DX12Swapchain::ReleaseBackBuffers() noexcept
{
    if (descriptors_) {
        for (DescriptorHandle& handle : rtvHandles_) {
            descriptors_->Free(handle);
            handle = {};
        }
    }
    for (auto& buffer : backBuffers_) {
        buffer.Reset();
    }
}

} // namespace talal::renderer
