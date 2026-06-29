#include "renderer/renderer_mvp.h"

#include "core/platform/high_res_timer.h"

#include <array>
#include <fstream>
#include <iomanip>

namespace talal::renderer {
namespace {

constexpr UINT kTimestampBeginFrame = 0;
constexpr UINT kTimestampGBufferBegin = 1;
constexpr UINT kTimestampGBufferEnd = 2;
constexpr UINT kTimestampLightingEnd = 3;
constexpr UINT kTimestampTonemapEnd = 4;
constexpr UINT kTimestampCount = 5;

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

} // namespace

bool RendererMvp::Initialize(HWND hwnd, std::filesystem::path repoRoot, std::uint32_t width, std::uint32_t height, bool hdr10, bool vsync, int frameLimit)
{
    hwnd_ = hwnd;
    repoRoot_ = std::move(repoRoot);

    try {
        if (!device_.Initialize(L"RTX 4070")) {
            return false;
        }
        if (!queues_.Initialize(device_.GetDevice())) {
            return false;
        }
        if (!descriptors_.Initialize(device_.GetDevice())) {
            return false;
        }
        if (!resources_.Initialize(device_)) {
            return false;
        }
        if (!upload_.Initialize(device_.GetDevice(), &queues_)) {
            return false;
        }
        if (!swapchain_.Initialize(
                device_.GetFactory(),
                device_.GetDevice(),
                &queues_,
                &descriptors_,
                SwapchainDesc { hwnd, width, height, hdr10, vsync, frameLimit })) {
            return false;
        }
        if (!commandLists_.Initialize(device_.GetDevice(), &queues_)) {
            return false;
        }
        const std::filesystem::path shaderRoot = repoRoot_ / "shaders" / "hlsl";
        const std::filesystem::path cacheRoot = repoRoot_ / "out" / "build" / "tala100-debug" / "shader_cache";
        if (!shaderCompiler_.Initialize(shaderRoot, cacheRoot)) {
            return false;
        }
        shaderCompiler_.StartHotReloadWatcher();

        camera_.SetPerspective(static_cast<float>(width) / static_cast<float>(std::max<std::uint32_t>(height, 1)));
        camera_.SetLookAt({ 0.0f, 2.0f, -5.0f }, { 0.0f, 0.5f, 0.0f });

        if (!gbufferPass_.Initialize(device_.GetDevice(), &descriptors_, &resources_, &shaderCompiler_, width, height)) {
            return false;
        }
        if (!lightingPass_.Initialize(device_.GetDevice(), &descriptors_, &resources_, &shaderCompiler_, width, height, gbufferPass_.FirstSrv())) {
            return false;
        }
        if (!tonemappingPass_.Initialize(device_.GetDevice(), &shaderCompiler_, swapchain_.Format(), lightingPass_.HdrSrv())) {
            return false;
        }
        defaultMaterial_ = materials_.CreateDefaultMaterial();
        if (!InitializeTimestampQueries()) {
            return false;
        }
        RebuildFrameGraph();
        initialized_ = true;
        return true;
    } catch (const Dx12Exception&) {
        Shutdown();
        return false;
    }
}

void RendererMvp::Shutdown() noexcept
{
    if (!initialized_ && !device_.GetDevice()) {
        return;
    }
    queues_.FlushAll();
    ShutdownTimestampQueries();
    tonemappingPass_.Shutdown();
    lightingPass_.Shutdown();
    gbufferPass_.Shutdown();
    frameGraph_.Clear();
    shaderCompiler_.Shutdown();
    commandLists_.Shutdown();
    swapchain_.Shutdown();
    upload_.Shutdown();
    resources_.Shutdown();
    descriptors_.Shutdown();
    queues_.Shutdown();
    device_.Shutdown();
    initialized_ = false;
}

bool RendererMvp::Resize(std::uint32_t width, std::uint32_t height)
{
    if (!initialized_) {
        return false;
    }
    queues_.FlushAll();
    if (!swapchain_.Resize(width, height)) {
        return false;
    }
    camera_.SetPerspective(static_cast<float>(swapchain_.Width()) / static_cast<float>(std::max<std::uint32_t>(swapchain_.Height(), 1)));
    if (!gbufferPass_.Resize(swapchain_.Width(), swapchain_.Height())) {
        return false;
    }
    if (!lightingPass_.Resize(swapchain_.Width(), swapchain_.Height())) {
        return false;
    }
    tonemappingPass_.Shutdown();
    if (!tonemappingPass_.Initialize(device_.GetDevice(), &shaderCompiler_, swapchain_.Format(), lightingPass_.HdrSrv())) {
        return false;
    }
    RebuildFrameGraph();
    return true;
}

void RendererMvp::SetPresentation(bool vsync, int frameLimit) noexcept
{
    swapchain_.SetVSync(vsync);
    swapchain_.SetFrameLimit(frameLimit);
}

void RendererMvp::Render(HWND inputHwnd)
{
    if (!initialized_) {
        return;
    }

    talal::core::HighResTimer frameTimer;
    camera_.UpdateFlyControls(inputHwnd, 1.0f / 60.0f);
    currentCameraConstants_ = camera_.Upload(upload_);
    currentBackBufferRtv_ = swapchain_.CurrentRTV();

    ID3D12GraphicsCommandList7* cmd = commandLists_.Reset(swapchain_.CurrentFrameIndex(), QueueType::Direct);
    ID3D12DescriptorHeap* heaps[] = { descriptors_.ShaderVisibleHeap(), descriptors_.Heap(HeapType::Sampler) };
    cmd->SetDescriptorHeaps(2, heaps);
    cmd->EndQuery(timestampHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, kTimestampBeginFrame);

    D3D12_RESOURCE_BARRIER begin = Transition(swapchain_.CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmd->ResourceBarrier(1, &begin);

    if (frameGraph_.Dirty()) {
        frameGraph_.Compile();
    }
    frameGraph_.Execute(*cmd, resources_);

    D3D12_RESOURCE_BARRIER end = Transition(swapchain_.CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    cmd->ResourceBarrier(1, &end);
    ResolveTimestamps(cmd);
    commandLists_.Submit(QueueType::Direct);
    swapchain_.Present();
    ReadbackTimestamps();

    const double frameMs = frameTimer.milliseconds();
    stats_.averageFrameMs = stats_.averageFrameMs == 0.0 ? frameMs : stats_.averageFrameMs * 0.95 + frameMs * 0.05;
    stats_.averageFps = stats_.averageFrameMs > 0.0 ? 1000.0 / stats_.averageFrameMs : 0.0;
    ++frameCounter_;
    if ((frameCounter_ % 5) == 0) {
        std::filesystem::create_directories(repoRoot_ / "project_core_state");
        std::ofstream statsFile(repoRoot_ / "project_core_state" / "renderer_phase3_stats.txt", std::ios::trunc);
        statsFile << std::fixed << std::setprecision(3)
                  << "avg_frame_ms=" << stats_.averageFrameMs << "\n"
                  << "avg_fps=" << stats_.averageFps << "\n"
                  << "gbuffer_gpu_ms=" << stats_.gbufferGpuMs << "\n"
                  << "lighting_gpu_ms=" << stats_.lightingGpuMs << "\n"
                  << "tonemap_gpu_ms=" << stats_.tonemapGpuMs << "\n";
    }
}

bool RendererMvp::InitializeTimestampQueries()
{
    D3D12_QUERY_HEAP_DESC queryDesc = {};
    queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryDesc.Count = kTimestampCount;
    HR_CHECK(device_.GetDevice()->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&timestampHeap_)), L"CreateQueryHeap timestamp");

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC buffer = {};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = sizeof(UINT64) * kTimestampCount;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HR_CHECK(device_.GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&timestampReadback_)), L"CreateCommittedResource timestamp readback");
    HR_CHECK(queues_.Get(QueueType::Direct)->GetTimestampFrequency(&timestampFrequency_), L"GetTimestampFrequency");
    timestampFrequency_ = std::max<UINT64>(1, timestampFrequency_);
    return true;
}

void RendererMvp::ShutdownTimestampQueries() noexcept
{
    timestampReadback_.Reset();
    timestampHeap_.Reset();
}

void RendererMvp::RebuildFrameGraph()
{
    frameGraph_.Clear();
    const FrameGraphResource albedo = frameGraph_.DeclareTexture(L"Albedo", gbufferPass_.Albedo(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    const FrameGraphResource normal = frameGraph_.DeclareTexture(L"Normal", gbufferPass_.Normal(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    const FrameGraphResource mrao = frameGraph_.DeclareTexture(L"MRAO", gbufferPass_.Mrao(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    const FrameGraphResource depth = frameGraph_.DeclareTexture(L"Depth", gbufferPass_.Depth(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
    const FrameGraphResource hdr = frameGraph_.DeclareTexture(L"HDR", lightingPass_.HdrTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);

    frameGraph_.AddPass(
        L"GBuffer",
        FrameGraphPassBuilder {}.Write(albedo).Write(normal).Write(mrao).Write(depth).SideEffect(),
        [this](ID3D12GraphicsCommandList7& cmd) {
            cmd.EndQuery(timestampHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, kTimestampGBufferBegin);
            gbufferPass_.Execute(&cmd, currentCameraConstants_);
            cmd.EndQuery(timestampHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, kTimestampGBufferEnd);
        });

    frameGraph_.AddPass(
        L"Lighting",
        FrameGraphPassBuilder {}.Read(albedo).Read(normal).Read(mrao).Read(depth).Write(hdr).SideEffect(),
        [this](ID3D12GraphicsCommandList7& cmd) {
            lightingPass_.Execute(&cmd, currentCameraConstants_);
            cmd.EndQuery(timestampHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, kTimestampLightingEnd);
        });

    frameGraph_.AddPass(
        L"Tonemapping",
        FrameGraphPassBuilder {}.Read(hdr).SideEffect(),
        [this](ID3D12GraphicsCommandList7& cmd) {
            tonemappingPass_.Execute(&cmd, currentBackBufferRtv_, swapchain_.Width(), swapchain_.Height());
            cmd.EndQuery(timestampHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, kTimestampTonemapEnd);
        });
}

void RendererMvp::ResolveTimestamps(ID3D12GraphicsCommandList7* cmd)
{
    cmd->ResolveQueryData(timestampHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, kTimestampCount, timestampReadback_.Get(), 0);
}

void RendererMvp::ReadbackTimestamps()
{
    if ((frameCounter_ % 16) != 0) {
        return;
    }
    queues_.WaitForGPU(QueueType::Direct);
    UINT64* mapped = nullptr;
    D3D12_RANGE readRange { 0, sizeof(UINT64) * kTimestampCount };
    if (FAILED(timestampReadback_->Map(0, &readRange, reinterpret_cast<void**>(&mapped)))) {
        return;
    }
    const double tickMs = 1000.0 / static_cast<double>(timestampFrequency_);
    const double gbuffer = static_cast<double>(mapped[kTimestampGBufferEnd] - mapped[kTimestampGBufferBegin]) * tickMs;
    const double lighting = static_cast<double>(mapped[kTimestampLightingEnd] - mapped[kTimestampGBufferEnd]) * tickMs;
    const double tonemap = static_cast<double>(mapped[kTimestampTonemapEnd] - mapped[kTimestampLightingEnd]) * tickMs;
    timestampReadback_->Unmap(0, nullptr);
    stats_.gbufferGpuMs = stats_.gbufferGpuMs == 0.0 ? gbuffer : stats_.gbufferGpuMs * 0.9 + gbuffer * 0.1;
    stats_.lightingGpuMs = stats_.lightingGpuMs == 0.0 ? lighting : stats_.lightingGpuMs * 0.9 + lighting * 0.1;
    stats_.tonemapGpuMs = stats_.tonemapGpuMs == 0.0 ? tonemap : stats_.tonemapGpuMs * 0.9 + tonemap * 0.1;
}

} // namespace talal::renderer
