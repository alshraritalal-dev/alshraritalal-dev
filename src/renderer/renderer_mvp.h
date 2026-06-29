#pragma once

#include "renderer/backend/descriptor_heap.h"
#include "renderer/backend/dx12_command_list.h"
#include "renderer/backend/dx12_device.h"
#include "renderer/backend/dx12_swapchain.h"
#include "renderer/backend/gpu_resource_manager.h"
#include "renderer/backend/upload_ring_buffer.h"
#include "renderer/camera.h"
#include "renderer/frame_graph.h"
#include "renderer/pipeline/pbr_material.h"
#include "renderer/render_passes/gbuffer_pass.h"
#include "renderer/render_passes/lighting_pass.h"
#include "renderer/render_passes/tonemapping_pass.h"
#include "tools/shader_compiler.h"

#include <filesystem>

namespace talal::renderer {

struct RendererStats {
    double averageFrameMs = 0.0;
    double averageFps = 0.0;
    double gbufferGpuMs = 0.0;
    double lightingGpuMs = 0.0;
    double tonemapGpuMs = 0.0;
};

class RendererMvp {
public:
    bool Initialize(HWND hwnd, std::filesystem::path repoRoot, std::uint32_t width, std::uint32_t height, bool hdr10, bool vsync, int frameLimit);
    void Shutdown() noexcept;
    bool Resize(std::uint32_t width, std::uint32_t height);
    void SetPresentation(bool vsync, int frameLimit) noexcept;
    void Render(HWND inputHwnd);
    RendererStats Stats() const noexcept { return stats_; }
    bool Ready() const noexcept { return initialized_; }

private:
    bool InitializeTimestampQueries();
    void ShutdownTimestampQueries() noexcept;
    void RebuildFrameGraph();
    void ResolveTimestamps(ID3D12GraphicsCommandList7* cmd);
    void ReadbackTimestamps();

    bool initialized_ = false;
    HWND hwnd_ = nullptr;
    std::filesystem::path repoRoot_;
    DX12Device device_;
    DX12CommandQueues queues_;
    DescriptorHeapManager descriptors_;
    GPUResourceManager resources_;
    UploadRingBuffer upload_;
    DX12Swapchain swapchain_;
    DX12CommandListPool commandLists_;
    ShaderCompiler shaderCompiler_;
    FrameGraph frameGraph_;
    GBufferPass gbufferPass_;
    LightingPass lightingPass_;
    TonemappingPass tonemappingPass_;
    MaterialRegistry materials_;
    MaterialRegistry::Handle defaultMaterial_;
    Camera camera_;
    D3D12_GPU_VIRTUAL_ADDRESS currentCameraConstants_ = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE currentBackBufferRtv_ {};
    std::uint32_t frameCounter_ = 0;
    ComPtr<ID3D12QueryHeap> timestampHeap_;
    ComPtr<ID3D12Resource> timestampReadback_;
    UINT64 timestampFrequency_ = 1;
    RendererStats stats_;
};

} // namespace talal::renderer
