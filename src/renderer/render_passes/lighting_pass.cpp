#include "renderer/render_passes/lighting_pass.h"

#include <d3dcompiler.h>

namespace talal::renderer {

bool LightingPass::Initialize(
    ID3D12Device10* device,
    DescriptorHeapManager* descriptors,
    GPUResourceManager* resources,
    ShaderCompiler* compiler,
    std::uint32_t width,
    std::uint32_t height,
    DescriptorHandle firstGBufferSrv)
{
    if (!device || !descriptors || !resources || !compiler || !firstGBufferSrv.valid()) {
        LogError(L"LightingPass::Initialize invalid dependency", E_INVALIDARG);
        return false;
    }
    device_ = device;
    descriptors_ = descriptors;
    resources_ = resources;
    compiler_ = compiler;
    firstGBufferSrv_ = firstGBufferSrv;
    width_ = std::max<std::uint32_t>(1, width);
    height_ = std::max<std::uint32_t>(1, height);
    return CreateRootSignature() && CreatePipelineState() && Resize(width_, height_);
}

void LightingPass::Shutdown() noexcept
{
    ReleaseTarget();
    pipelineState_.Reset();
    rootSignature_.Reset();
    device_ = nullptr;
    descriptors_ = nullptr;
    resources_ = nullptr;
    compiler_ = nullptr;
}

bool LightingPass::Resize(std::uint32_t width, std::uint32_t height)
{
    width_ = std::max<std::uint32_t>(1, width);
    height_ = std::max<std::uint32_t>(1, height);
    ReleaseTarget();
    try {
        hdrTarget_ = resources_->CreateTexture2D(width_, height_, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, ResourceUsage::RenderTarget | ResourceUsage::ShaderResource);
        hdrRtv_ = descriptors_->Allocate(HeapType::RTV);
        hdrSrv_ = descriptors_->Allocate(HeapType::CBV_SRV_UAV);
        device_->CreateRenderTargetView(resources_->Get(hdrTarget_), nullptr, hdrRtv_.cpu);

        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(resources_->Get(hdrTarget_), &srv, hdrSrv_.cpu);
        return true;
    } catch (const Dx12Exception&) {
        ReleaseTarget();
        return false;
    }
}

void LightingPass::Execute(ID3D12GraphicsCommandList7* cmd, D3D12_GPU_VIRTUAL_ADDRESS cameraConstants)
{
    const D3D12_VIEWPORT viewport { 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f };
    const D3D12_RECT scissor { 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
    const float clear[4] { 0.0f, 0.0f, 0.0f, 1.0f };
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->OMSetRenderTargets(1, &hdrRtv_.cpu, FALSE, nullptr);
    cmd->ClearRenderTargetView(hdrRtv_.cpu, clear, 0, nullptr);
    cmd->SetGraphicsRootDescriptorTable(0, firstGBufferSrv_.gpu);
    cmd->SetGraphicsRootConstantBufferView(1, cameraConstants);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
}

bool LightingPass::CreateRootSignature()
{
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 4;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &range;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[1].Descriptor.ShaderRegister = 0;
    parameters[1].Descriptor.RegisterSpace = 0;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 2;
    desc.pParameters = parameters;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    if (FAILED(hr) && error) {
        LogInfo(Widen({ static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize() }));
    }
    HR_CHECK(hr, L"D3D12SerializeRootSignature Lighting");
    HR_CHECK(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_)), L"CreateRootSignature Lighting");
    return true;
}

bool LightingPass::CreatePipelineState()
{
    const bool debugShaders =
#if !defined(NDEBUG)
        true;
#else
        false;
#endif
    const ShaderBlob vs = compiler_->Compile({ L"shaders/hlsl/lighting.hlsl", L"FullscreenVS", L"vs_6_6", {}, debugShaders });
    const ShaderBlob ps = compiler_->Compile({ L"shaders/hlsl/lighting.hlsl", L"LightingPS", L"ps_6_6", {}, debugShaders });

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
    pso.InputLayout = {};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.SampleDesc.Count = 1;
    HR_CHECK(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipelineState_)), L"CreateGraphicsPipelineState Lighting");
    return true;
}

void LightingPass::ReleaseTarget() noexcept
{
    if (descriptors_) {
        descriptors_->Free(hdrRtv_);
        descriptors_->Free(hdrSrv_);
    }
    if (resources_) {
        resources_->Destroy(hdrTarget_);
    }
    hdrTarget_ = {};
    hdrRtv_ = {};
    hdrSrv_ = {};
}

} // namespace talal::renderer
