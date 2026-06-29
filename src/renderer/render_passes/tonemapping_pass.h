#pragma once

#include "renderer/backend/descriptor_heap.h"
#include "tools/shader_compiler.h"

namespace talal::renderer {

class TonemappingPass {
public:
    bool Initialize(ID3D12Device10* device, ShaderCompiler* compiler, DXGI_FORMAT backBufferFormat, DescriptorHandle hdrSrv);
    void Shutdown() noexcept;
    void SetOutputFormat(DXGI_FORMAT format);
    bool RecreatePipeline();
    void Execute(
        ID3D12GraphicsCommandList7* cmd,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,
        std::uint32_t width,
        std::uint32_t height);

private:
    bool CreateRootSignature();
    bool CreatePipelineState();

    ID3D12Device10* device_ = nullptr;
    ShaderCompiler* compiler_ = nullptr;
    DXGI_FORMAT backBufferFormat_ = DXGI_FORMAT_R8G8B8A8_UNORM;
    DescriptorHandle hdrSrv_;
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipelineState_;
};

} // namespace talal::renderer
