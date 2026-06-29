#pragma once

#include "renderer/backend/descriptor_heap.h"
#include "renderer/backend/gpu_resource_manager.h"
#include "tools/shader_compiler.h"

namespace talal::renderer {

class LightingPass {
public:
    bool Initialize(
        ID3D12Device10* device,
        DescriptorHeapManager* descriptors,
        GPUResourceManager* resources,
        ShaderCompiler* compiler,
        std::uint32_t width,
        std::uint32_t height,
        DescriptorHandle firstGBufferSrv);
    void Shutdown() noexcept;
    bool Resize(std::uint32_t width, std::uint32_t height);
    void Execute(ID3D12GraphicsCommandList7* cmd, D3D12_GPU_VIRTUAL_ADDRESS cameraConstants);

    ResourceHandle HdrTarget() const noexcept { return hdrTarget_; }
    DescriptorHandle HdrSrv() const noexcept { return hdrSrv_; }

private:
    bool CreateRootSignature();
    bool CreatePipelineState();
    void ReleaseTarget() noexcept;

    ID3D12Device10* device_ = nullptr;
    DescriptorHeapManager* descriptors_ = nullptr;
    GPUResourceManager* resources_ = nullptr;
    ShaderCompiler* compiler_ = nullptr;
    std::uint32_t width_ = 1;
    std::uint32_t height_ = 1;
    DescriptorHandle firstGBufferSrv_;
    ResourceHandle hdrTarget_;
    DescriptorHandle hdrRtv_;
    DescriptorHandle hdrSrv_;
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipelineState_;
};

} // namespace talal::renderer
