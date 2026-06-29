#include "renderer/render_passes/gbuffer_pass.h"

#include <d3dcompiler.h>

#include <array>
#include <cstring>

namespace talal::renderer {
namespace {

constexpr DXGI_FORMAT kAlbedoFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
constexpr DXGI_FORMAT kNormalFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
constexpr DXGI_FORMAT kMraoFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kDepthResourceFormat = DXGI_FORMAT_R32_TYPELESS;
constexpr DXGI_FORMAT kDepthDsvFormat = DXGI_FORMAT_D32_FLOAT;
constexpr DXGI_FORMAT kDepthSrvFormat = DXGI_FORMAT_R32_FLOAT;

std::array<GBufferPass::SceneVertex, 24> CubeVertices()
{
    using V = GBufferPass::SceneVertex;
    const DirectX::XMFLOAT4 tangent { 1.0f, 0.0f, 0.0f, 1.0f };
    return {
        V { {-1,-1,-1}, {0,0,-1}, tangent, {0,1} }, V { {-1, 1,-1}, {0,0,-1}, tangent, {0,0} }, V { { 1, 1,-1}, {0,0,-1}, tangent, {1,0} }, V { { 1,-1,-1}, {0,0,-1}, tangent, {1,1} },
        V { { 1,-1, 1}, {0,0, 1}, tangent, {0,1} }, V { { 1, 1, 1}, {0,0, 1}, tangent, {0,0} }, V { {-1, 1, 1}, {0,0, 1}, tangent, {1,0} }, V { {-1,-1, 1}, {0,0, 1}, tangent, {1,1} },
        V { {-1,-1, 1}, {-1,0,0}, tangent, {0,1} }, V { {-1, 1, 1}, {-1,0,0}, tangent, {0,0} }, V { {-1, 1,-1}, {-1,0,0}, tangent, {1,0} }, V { {-1,-1,-1}, {-1,0,0}, tangent, {1,1} },
        V { { 1,-1,-1}, { 1,0,0}, tangent, {0,1} }, V { { 1, 1,-1}, { 1,0,0}, tangent, {0,0} }, V { { 1, 1, 1}, { 1,0,0}, tangent, {1,0} }, V { { 1,-1, 1}, { 1,0,0}, tangent, {1,1} },
        V { {-1, 1,-1}, {0, 1,0}, tangent, {0,1} }, V { {-1, 1, 1}, {0, 1,0}, tangent, {0,0} }, V { { 1, 1, 1}, {0, 1,0}, tangent, {1,0} }, V { { 1, 1,-1}, {0, 1,0}, tangent, {1,1} },
        V { {-1,-1, 1}, {0,-1,0}, tangent, {0,1} }, V { {-1,-1,-1}, {0,-1,0}, tangent, {0,0} }, V { { 1,-1,-1}, {0,-1,0}, tangent, {1,0} }, V { { 1,-1, 1}, {0,-1,0}, tangent, {1,1} }
    };
}

std::array<std::uint16_t, 36> CubeIndices()
{
    return {
        0,1,2, 0,2,3,
        4,5,6, 4,6,7,
        8,9,10, 8,10,11,
        12,13,14, 12,14,15,
        16,17,18, 16,18,19,
        20,21,22, 20,22,23
    };
}

} // namespace

bool GBufferPass::Initialize(
    ID3D12Device10* device,
    DescriptorHeapManager* descriptors,
    GPUResourceManager* resources,
    ShaderCompiler* compiler,
    std::uint32_t width,
    std::uint32_t height)
{
    if (!device || !descriptors || !resources || !compiler) {
        LogError(L"GBufferPass::Initialize null dependency", E_POINTER);
        return false;
    }
    device_ = device;
    descriptors_ = descriptors;
    resources_ = resources;
    compiler_ = compiler;
    width_ = std::max<std::uint32_t>(1, width);
    height_ = std::max<std::uint32_t>(1, height);
    return CreateRootSignature() && CreatePipelineState() && CreateGeometry() && Resize(width_, height_);
}

void GBufferPass::Shutdown() noexcept
{
    ReleaseTargets();
    if (resources_) {
        resources_->Destroy(vertexBuffer_);
        resources_->Destroy(indexBuffer_);
    }
    vertexBuffer_ = {};
    indexBuffer_ = {};
    pipelineState_.Reset();
    rootSignature_.Reset();
    device_ = nullptr;
    descriptors_ = nullptr;
    resources_ = nullptr;
    compiler_ = nullptr;
}

bool GBufferPass::Resize(std::uint32_t width, std::uint32_t height)
{
    width_ = std::max<std::uint32_t>(1, width);
    height_ = std::max<std::uint32_t>(1, height);
    ReleaseTargets();

    try {
        albedo_ = resources_->CreateTexture2D(width_, height_, kAlbedoFormat, 1, ResourceUsage::RenderTarget | ResourceUsage::ShaderResource);
        normal_ = resources_->CreateTexture2D(width_, height_, kNormalFormat, 1, ResourceUsage::RenderTarget | ResourceUsage::ShaderResource);
        mrao_ = resources_->CreateTexture2D(width_, height_, kMraoFormat, 1, ResourceUsage::RenderTarget | ResourceUsage::ShaderResource);
        depth_ = resources_->CreateTexture2D(
            width_,
            height_,
            kDepthResourceFormat,
            1,
            ResourceUsage::DepthStencil | ResourceUsage::ShaderResource,
            kDepthDsvFormat);

        albedoRtv_ = descriptors_->Allocate(HeapType::RTV);
        normalRtv_ = descriptors_->Allocate(HeapType::RTV);
        mraoRtv_ = descriptors_->Allocate(HeapType::RTV);
        depthDsv_ = descriptors_->Allocate(HeapType::DSV);
        albedoSrv_ = descriptors_->Allocate(HeapType::CBV_SRV_UAV);
        normalSrv_ = descriptors_->Allocate(HeapType::CBV_SRV_UAV);
        mraoSrv_ = descriptors_->Allocate(HeapType::CBV_SRV_UAV);
        depthSrv_ = descriptors_->Allocate(HeapType::CBV_SRV_UAV);

        device_->CreateRenderTargetView(resources_->Get(albedo_), nullptr, albedoRtv_.cpu);
        device_->CreateRenderTargetView(resources_->Get(normal_), nullptr, normalRtv_.cpu);
        device_->CreateRenderTargetView(resources_->Get(mrao_), nullptr, mraoRtv_.cpu);
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
        dsv.Format = kDepthDsvFormat;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsv.Flags = D3D12_DSV_FLAG_NONE;
        device_->CreateDepthStencilView(resources_->Get(depth_), &dsv, depthDsv_.cpu);

        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        srv.Format = kAlbedoFormat;
        device_->CreateShaderResourceView(resources_->Get(albedo_), &srv, albedoSrv_.cpu);
        srv.Format = kNormalFormat;
        device_->CreateShaderResourceView(resources_->Get(normal_), &srv, normalSrv_.cpu);
        srv.Format = kMraoFormat;
        device_->CreateShaderResourceView(resources_->Get(mrao_), &srv, mraoSrv_.cpu);
        srv.Format = kDepthSrvFormat;
        device_->CreateShaderResourceView(resources_->Get(depth_), &srv, depthSrv_.cpu);
        return true;
    } catch (const Dx12Exception&) {
        ReleaseTargets();
        return false;
    }
}

void GBufferPass::Execute(ID3D12GraphicsCommandList7* cmd, D3D12_GPU_VIRTUAL_ADDRESS cameraConstants)
{
    const D3D12_VIEWPORT viewport { 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f };
    const D3D12_RECT scissor { 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
    const std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 3> rtvs { albedoRtv_.cpu, normalRtv_.cpu, mraoRtv_.cpu };
    const std::array<float, 4> clear { 0.0f, 0.0f, 0.0f, 1.0f };
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->OMSetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.data(), FALSE, &depthDsv_.cpu);
    for (D3D12_CPU_DESCRIPTOR_HANDLE rtv : rtvs) {
        cmd->ClearRenderTargetView(rtv, clear.data(), 0, nullptr);
    }
    cmd->ClearDepthStencilView(depthDsv_.cpu, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    cmd->SetGraphicsRootConstantBufferView(0, cameraConstants);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &vbv_);
    cmd->IASetIndexBuffer(&ibv_);
    for (int cube = 0; cube < 3; ++cube) {
        const ObjectConstants object = ObjectForCube(cube);
        cmd->SetGraphicsRoot32BitConstants(1, sizeof(ObjectConstants) / sizeof(std::uint32_t), &object, 0);
        cmd->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
    }
}

bool GBufferPass::CreateRootSignature()
{
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].Descriptor.RegisterSpace = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.ShaderRegister = 1;
    parameters[1].Constants.RegisterSpace = 0;
    parameters[1].Constants.Num32BitValues = sizeof(ObjectConstants) / sizeof(std::uint32_t);
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 2;
    desc.pParameters = parameters;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    if (FAILED(hr) && error) {
        LogInfo(Widen({ static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize() }));
    }
    HR_CHECK(hr, L"D3D12SerializeRootSignature GBuffer");
    HR_CHECK(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_)), L"CreateRootSignature GBuffer");
    return true;
}

bool GBufferPass::CreatePipelineState()
{
    const bool debugShaders =
#if !defined(NDEBUG)
        true;
#else
        false;
#endif
    const ShaderBlob vs = compiler_->Compile({ L"shaders/hlsl/gbuffer.hlsl", L"SceneVS", L"vs_6_6", {}, debugShaders });
    const ShaderBlob ps = compiler_->Compile({ L"shaders/hlsl/gbuffer.hlsl", L"GBufferPS", L"ps_6_6", {}, debugShaders });

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(SceneVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(SceneVertex, normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(SceneVertex, tangent), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(SceneVertex, uv), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = rootSignature_.Get();
    pso.VS = { vs.bytes.data(), vs.bytes.size() };
    pso.PS = { ps.bytes.data(), ps.bytes.size() };
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.BlendState.RenderTarget[2].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pso.RasterizerState.FrontCounterClockwise = FALSE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = TRUE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pso.InputLayout = { inputLayout, static_cast<UINT>(std::size(inputLayout)) };
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 3;
    pso.RTVFormats[0] = kAlbedoFormat;
    pso.RTVFormats[1] = kNormalFormat;
    pso.RTVFormats[2] = kMraoFormat;
    pso.DSVFormat = kDepthDsvFormat;
    pso.SampleDesc.Count = 1;
    HR_CHECK(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipelineState_)), L"CreateGraphicsPipelineState GBuffer");
    return true;
}

bool GBufferPass::CreateGeometry()
{
    const auto vertices = CubeVertices();
    const auto indices = CubeIndices();
    const std::uint64_t vbBytes = sizeof(vertices);
    const std::uint64_t ibBytes = sizeof(indices);
    vertexBuffer_ = resources_->CreateBuffer(vbBytes, ResourceUsage::VertexBuffer, D3D12_HEAP_TYPE_UPLOAD);
    indexBuffer_ = resources_->CreateBuffer(ibBytes, ResourceUsage::IndexBuffer, D3D12_HEAP_TYPE_UPLOAD);

    void* mapped = nullptr;
    HR_CHECK(resources_->Get(vertexBuffer_)->Map(0, nullptr, &mapped), L"Map cube vertex buffer");
    std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(SceneVertex));
    resources_->Get(vertexBuffer_)->Unmap(0, nullptr);
    HR_CHECK(resources_->Get(indexBuffer_)->Map(0, nullptr, &mapped), L"Map cube index buffer");
    std::memcpy(mapped, indices.data(), indices.size() * sizeof(std::uint16_t));
    resources_->Get(indexBuffer_)->Unmap(0, nullptr);

    vbv_.BufferLocation = resources_->Get(vertexBuffer_)->GetGPUVirtualAddress();
    vbv_.StrideInBytes = sizeof(SceneVertex);
    vbv_.SizeInBytes = static_cast<UINT>(vbBytes);
    ibv_.BufferLocation = resources_->Get(indexBuffer_)->GetGPUVirtualAddress();
    ibv_.Format = DXGI_FORMAT_R16_UINT;
    ibv_.SizeInBytes = static_cast<UINT>(ibBytes);
    indexCount_ = static_cast<UINT>(indices.size());
    return true;
}

void GBufferPass::ReleaseTargets() noexcept
{
    if (descriptors_) {
        descriptors_->Free(albedoRtv_);
        descriptors_->Free(normalRtv_);
        descriptors_->Free(mraoRtv_);
        descriptors_->Free(depthDsv_);
        descriptors_->Free(albedoSrv_);
        descriptors_->Free(normalSrv_);
        descriptors_->Free(mraoSrv_);
        descriptors_->Free(depthSrv_);
    }
    if (resources_) {
        resources_->Destroy(albedo_);
        resources_->Destroy(normal_);
        resources_->Destroy(mrao_);
        resources_->Destroy(depth_);
    }
    albedo_ = {};
    normal_ = {};
    mrao_ = {};
    depth_ = {};
    albedoRtv_ = {};
    normalRtv_ = {};
    mraoRtv_ = {};
    depthDsv_ = {};
    albedoSrv_ = {};
    normalSrv_ = {};
    mraoSrv_ = {};
    depthSrv_ = {};
}

GBufferPass::ObjectConstants GBufferPass::ObjectForCube(int cubeIndex) const noexcept
{
    const float x = static_cast<float>(cubeIndex - 1) * 2.2f;
    const DirectX::XMMATRIX scale = DirectX::XMMatrixScaling(0.75f, 0.75f, 0.75f);
    const DirectX::XMMATRIX rotation = DirectX::XMMatrixRotationRollPitchYaw(0.35f, 0.55f * static_cast<float>(cubeIndex + 1), 0.0f);
    const DirectX::XMMATRIX translation = DirectX::XMMatrixTranslation(x, 0.75f, 0.0f);
    ObjectConstants object;
    DirectX::XMStoreFloat4x4(&object.world, DirectX::XMMatrixTranspose(scale * rotation * translation));
    if (cubeIndex == 0) {
        object.baseColorMetallic = { 1.0f, 0.05f, 0.03f, 0.0f };
        object.roughnessEmissivePad = { 0.8f, 0.0f, 0.0f, 0.0f };
    } else if (cubeIndex == 1) {
        object.baseColorMetallic = { 0.05f, 0.25f, 1.0f, 1.0f };
        object.roughnessEmissivePad = { 0.2f, 0.0f, 0.0f, 0.0f };
    } else {
        object.baseColorMetallic = { 1.0f, 1.0f, 1.0f, 0.5f };
        object.roughnessEmissivePad = { 0.5f, 0.0f, 0.0f, 0.0f };
    }
    return object;
}

} // namespace talal::renderer
