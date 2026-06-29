#pragma once

#include "renderer/backend/descriptor_heap.h"
#include "renderer/backend/gpu_resource_manager.h"
#include "renderer/backend/upload_ring_buffer.h"
#include "renderer/camera.h"
#include "tools/shader_compiler.h"

#include <DirectXMath.h>

namespace talal::renderer {

class GBufferPass {
public:
    struct SceneVertex {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 normal;
        DirectX::XMFLOAT4 tangent;
        DirectX::XMFLOAT2 uv;
    };

    struct ObjectConstants {
        DirectX::XMFLOAT4X4 world;
        DirectX::XMFLOAT4 baseColorMetallic;
        DirectX::XMFLOAT4 roughnessEmissivePad;
    };

    bool Initialize(
        ID3D12Device10* device,
        DescriptorHeapManager* descriptors,
        GPUResourceManager* resources,
        ShaderCompiler* compiler,
        std::uint32_t width,
        std::uint32_t height);
    void Shutdown() noexcept;
    bool Resize(std::uint32_t width, std::uint32_t height);
    void Execute(ID3D12GraphicsCommandList7* cmd, D3D12_GPU_VIRTUAL_ADDRESS cameraConstants);

    DescriptorHandle FirstSrv() const noexcept { return albedoSrv_; }
    ResourceHandle Albedo() const noexcept { return albedo_; }
    ResourceHandle Normal() const noexcept { return normal_; }
    ResourceHandle Mrao() const noexcept { return mrao_; }
    ResourceHandle Depth() const noexcept { return depth_; }

private:
    bool CreateRootSignature();
    bool CreatePipelineState();
    bool CreateGeometry();
    void ReleaseTargets() noexcept;
    ObjectConstants ObjectForCube(int cubeIndex) const noexcept;

    ID3D12Device10* device_ = nullptr;
    DescriptorHeapManager* descriptors_ = nullptr;
    GPUResourceManager* resources_ = nullptr;
    ShaderCompiler* compiler_ = nullptr;
    std::uint32_t width_ = 1;
    std::uint32_t height_ = 1;
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipelineState_;
    ResourceHandle vertexBuffer_;
    ResourceHandle indexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vbv_ {};
    D3D12_INDEX_BUFFER_VIEW ibv_ {};
    UINT indexCount_ = 0;
    ResourceHandle albedo_;
    ResourceHandle normal_;
    ResourceHandle mrao_;
    ResourceHandle depth_;
    DescriptorHandle albedoRtv_;
    DescriptorHandle normalRtv_;
    DescriptorHandle mraoRtv_;
    DescriptorHandle depthDsv_;
    DescriptorHandle albedoSrv_;
    DescriptorHandle normalSrv_;
    DescriptorHandle mraoSrv_;
    DescriptorHandle depthSrv_;
};

} // namespace talal::renderer
