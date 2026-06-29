#pragma once

#include "renderer/backend/dx12_common.h"

#include <string>

namespace talal::renderer {

class DX12Device {
public:
    bool Initialize(std::wstring preferredAdapterName = L"RTX 4070");
    void Shutdown() noexcept;

    ID3D12Device10* GetDevice() noexcept { return device_.Get(); }
    ID3D12Device10* GetDevice() const noexcept { return device_.Get(); }
    IDXGIFactory7* GetFactory() noexcept { return factory_.Get(); }
    IDXGIAdapter4* GetAdapter() noexcept { return adapter_.Get(); }

    const D3D12_FEATURE_DATA_D3D12_OPTIONS& Options() const noexcept { return options_; }
    const D3D12_FEATURE_DATA_SHADER_MODEL& ShaderModel() const noexcept { return shaderModel_; }
    const D3D12_FEATURE_DATA_D3D12_OPTIONS5& RaytracingOptions() const noexcept { return raytracingOptions_; }
    const D3D12_FEATURE_DATA_D3D12_OPTIONS7& MeshShaderOptions() const noexcept { return meshShaderOptions_; }
    const std::wstring& AdapterName() const noexcept { return adapterName_; }
    std::uint64_t DedicatedVideoMemoryBytes() const noexcept { return dedicatedVideoMemoryBytes_; }
    bool SupportsDxr11() const noexcept;
    bool SupportsMeshShaders() const noexcept;

private:
    void EnableDebugLayer();
    ComPtr<IDXGIAdapter4> SelectAdapter(std::wstring_view preferredAdapterName);
    void QueryFeatures();

    ComPtr<IDXGIFactory7> factory_;
    ComPtr<IDXGIAdapter4> adapter_;
    ComPtr<ID3D12Device10> device_;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options_ {};
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel_ {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 raytracingOptions_ {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 meshShaderOptions_ {};
    std::wstring adapterName_;
    std::uint64_t dedicatedVideoMemoryBytes_ = 0;
};

} // namespace talal::renderer
