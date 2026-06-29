#include "renderer/backend/dx12_device.h"

#include <algorithm>

namespace talal::renderer {
namespace {

std::wstring Lower(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(towlower(c));
    });
    return text;
}

bool ContainsInsensitive(std::wstring_view haystack, std::wstring_view needle)
{
    return Lower(std::wstring(haystack)).find(Lower(std::wstring(needle))) != std::wstring::npos;
}

} // namespace

bool DX12Device::Initialize(std::wstring preferredAdapterName)
{
    try {
        EnableDebugLayer();

        UINT factoryFlags = 0;
#if !defined(NDEBUG)
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
        HR_CHECK(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_)), L"CreateDXGIFactory2");

        adapter_ = SelectAdapter(preferredAdapterName);
        if (!adapter_) {
            LogError(L"No hardware adapter supports Direct3D 12 feature level 12_2", DXGI_ERROR_UNSUPPORTED);
            return false;
        }

        ComPtr<ID3D12Device> baseDevice;
        HR_CHECK(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&baseDevice)), L"D3D12CreateDevice FL12_2");
        HR_CHECK(baseDevice.As(&device_), L"Query ID3D12Device10");

        DXGI_ADAPTER_DESC3 desc = {};
        HR_CHECK(adapter_->GetDesc3(&desc), L"IDXGIAdapter4::GetDesc3");
        adapterName_ = desc.Description;
        dedicatedVideoMemoryBytes_ = desc.DedicatedVideoMemory;

        QueryFeatures();
        LogInfo(L"DX12 device initialized on " + adapterName_);
        return true;
    } catch (const Dx12Exception&) {
        Shutdown();
        return false;
    }
}

void DX12Device::Shutdown() noexcept
{
    device_.Reset();
    adapter_.Reset();
    factory_.Reset();
}

bool DX12Device::SupportsDxr11() const noexcept
{
    return raytracingOptions_.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
}

bool DX12Device::SupportsMeshShaders() const noexcept
{
    return meshShaderOptions_.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;
}

void DX12Device::EnableDebugLayer()
{
#if !defined(NDEBUG)
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
        ComPtr<ID3D12Debug1> debug1;
        if (SUCCEEDED(debug.As(&debug1))) {
            debug1->SetEnableGPUBasedValidation(FALSE);
        }
    }
#endif
}

ComPtr<IDXGIAdapter4> DX12Device::SelectAdapter(std::wstring_view preferredAdapterName)
{
    ComPtr<IDXGIAdapter4> preferred;
    ComPtr<IDXGIAdapter4> firstSupported;

    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter1;
        HRESULT hr = factory_->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter1));
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        HR_CHECK(hr, L"IDXGIFactory6::EnumAdapterByGpuPreference");

        DXGI_ADAPTER_DESC1 desc = {};
        HR_CHECK(adapter1->GetDesc1(&desc), L"IDXGIAdapter1::GetDesc1");
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }

        if (FAILED(D3D12CreateDevice(adapter1.Get(), D3D_FEATURE_LEVEL_12_2, __uuidof(ID3D12Device), nullptr))) {
            continue;
        }

        ComPtr<IDXGIAdapter4> adapter4;
        HR_CHECK(adapter1.As(&adapter4), L"Query IDXGIAdapter4");
        if (!firstSupported) {
            firstSupported = adapter4;
        }
        if (ContainsInsensitive(desc.Description, preferredAdapterName)) {
            preferred = adapter4;
            break;
        }
    }

    return preferred ? preferred : firstSupported;
}

void DX12Device::QueryFeatures()
{
    HR_CHECK(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options_, sizeof(options_)), L"CheckFeatureSupport D3D12_OPTIONS");

    shaderModel_.HighestShaderModel = D3D_SHADER_MODEL_6_6;
    HRESULT shaderModelResult = device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel_, sizeof(shaderModel_));
    if (FAILED(shaderModelResult)) {
        shaderModel_.HighestShaderModel = D3D_SHADER_MODEL_6_5;
        HR_CHECK(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel_, sizeof(shaderModel_)), L"CheckFeatureSupport SHADER_MODEL");
    }

    HR_CHECK(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &raytracingOptions_, sizeof(raytracingOptions_)), L"CheckFeatureSupport D3D12_OPTIONS5");
    HR_CHECK(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &meshShaderOptions_, sizeof(meshShaderOptions_)), L"CheckFeatureSupport D3D12_OPTIONS7");
}

} // namespace talal::renderer
