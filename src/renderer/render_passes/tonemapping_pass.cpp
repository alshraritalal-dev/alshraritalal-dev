#include "renderer/render_passes/tonemapping_pass.h"

#include <d3dcompiler.h>

namespace talal::renderer {

bool TonemappingPass::Initialize(ID3D12Device10* device, ShaderCompiler* compiler, DXGI_FORMAT backBufferFormat, DescriptorHandle hdrSrv)
{
    if (!device || !compiler || !hdrSrv.valid()) {
        LogError(L"TonemappingPass::Initialize invalid dependency", E_INVALIDARG);
        return false;
    }
    device_ = device;
    compiler_ = compiler;
    backBufferFormat_ = backBufferFormat;
    hdrSrv_ = hdrSrv;
    return CreateRootSignature() && CreatePipelineState();
}

void TonemappingPass::Shutdown() noexcept
{
    pipelineState_.Reset();
    rootSignature_.Reset();
    device_ = nullptr;
    compiler_ = nullptr;
    hdrSrv_ = {};
}

void TonemappingPass::SetOutputFormat(DXGI_FORMAT format)
{
    backBufferFormat_ = format;
}

bool TonemappingPass::RecreatePipeline()
{
    pipelineState_.Reset();
    return CreatePipelineState();
}

void TonemappingPass::Execute(
    ID3D12GraphicsCommandList7* cmd,
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,
    std::uint32_t width,
    std::uint32_t height)
{
    const D3D12_VIEWPORT viewport { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
    const D3D12_RECT scissor { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->OMSetRenderTargets(1, &backBufferRtv, FALSE, nullptr);
    cmd->SetGraphicsRootDescriptorTable(0, hdrSrv_.gpu);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
}

bool TonemappingPass::CreateRootSignature()
{
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    if (FAILED(hr) && error) {
        LogInfo(Widen({ static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize() }));
    }
    HR_CHECK(hr, L"D3D12SerializeRootSignature Tonemapping");
    HR_CHECK(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_)), L"CreateRootSignature Tonemapping");
    return true;
}

bool TonemappingPass::CreatePipelineState()
{
    const bool debugShaders =
#if !defined(NDEBUG)
        true;
#else
        false;
#endif
    const ShaderBlob vs = compiler_->Compile({ L"shaders/hlsl/tonemapping.hlsl", L"FullscreenVS", L"vs_6_6", {}, debugShaders });
    const ShaderBlob ps = compiler_->Compile({ L"shaders/hlsl/tonemapping.hlsl", L"TonemapPS", L"ps_6_6", {}, debugShaders });

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = rootSignature_.Get();
    pso.VS = { vs.bytes.data(), vs.bytes.size() };
    pso.PS = { ps.bytes.data(), ps.bytes.size() };
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = backBufferFormat_;
    pso.SampleDesc.Count = 1;
    HR_CHECK(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipelineState_)), L"CreateGraphicsPipelineState Tonemapping");
    return true;
}

} // namespace talal::renderer
