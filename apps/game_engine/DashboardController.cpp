#include "DashboardController.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>

using Microsoft::WRL::ComPtr;

namespace talal::dashboard {
namespace {

struct SliderSpec {
    SettingKey key;
    const wchar_t* section;
    const wchar_t* label;
    int minValue;
    int maxValue;
    const wchar_t* suffix;
};

constexpr std::array<SliderSpec, kSettingKeyCount> kSliderSpecs {{
    { SettingKey::RenderScale, L"RENDERING", L"Render Scale", 50, 150, L"%" },
    { SettingKey::TexturePool, L"PERFORMANCE", L"Texture Pool", 512, 11264, L" MB" },
    { SettingKey::Anisotropy, L"RENDERING", L"Anisotropy", 1, 16, L"x" },
    { SettingKey::AmbientOcclusion, L"RENDERING", L"Ambient Occlusion", 0, 4, L"/4" },
    { SettingKey::GlobalIllumination, L"RENDERING", L"Global Illumination", 0, 4, L"/4" },
    { SettingKey::ShadowQuality, L"SHADOWS", L"Shadow Quality", 0, 4, L"/4" },
    { SettingKey::ShadowDistance, L"SHADOWS", L"Shadow Distance", 25, 100, L"%" },
    { SettingKey::ShadowCascades, L"SHADOWS", L"Shadow Cascades", 1, 4, L"" },
    { SettingKey::ViewDistance, L"GEOMETRY", L"View Distance", 25, 100, L"%" },
    { SettingKey::TerrainQuality, L"GEOMETRY", L"Terrain Quality", 25, 100, L"%" },
    { SettingKey::GeometryDetail, L"GEOMETRY", L"Geometry Detail", 25, 100, L"%" },
    { SettingKey::VolumetricFog, L"EFFECTS", L"Volumetric Fog", 0, 4, L"/4" },
    { SettingKey::WaterQuality, L"EFFECTS", L"Water Quality", 25, 100, L"%" },
    { SettingKey::ParticleQuality, L"EFFECTS", L"Particle Quality", 0, 100, L"%" },
    { SettingKey::FoliageDensity, L"POPULATION", L"Foliage Density", 0, 100, L"%" },
    { SettingKey::TrafficDensity, L"POPULATION", L"Traffic Density", 0, 100, L"%" },
    { SettingKey::CrowdDensity, L"POPULATION", L"Crowd Density", 0, 100, L"%" },
    { SettingKey::RayTracingMode, L"RAY TRACING", L"RT Mode", 0, 3, L"/3" },
    { SettingKey::RayTracingQuality, L"RAY TRACING", L"RT Quality", 0, 3, L"/3" },
    { SettingKey::RayTracingReflections, L"RAY TRACING", L"RT Reflections", 0, 3, L"/3" },
    { SettingKey::TextureStreaming, L"PERFORMANCE", L"Texture Streaming", 0, 100, L"%" }
}};

constexpr std::array<int, 7> kFrameLimits { 30, 60, 120, 144, 165, 240, 0 };

const SliderSpec& SpecFor(SettingKey key) noexcept
{
    return kSliderSpecs[static_cast<std::size_t>(key)];
}

float NormalizeValue(SettingKey key, int value) noexcept
{
    const SliderSpec& spec = SpecFor(key);
    const int clamped = std::min(std::max(value, spec.minValue), spec.maxValue);
    if (spec.maxValue == spec.minValue) {
        return 0.0f;
    }
    return static_cast<float>(clamped - spec.minValue) / static_cast<float>(spec.maxValue - spec.minValue);
}

int DenormalizeValue(SettingKey key, float normalized) noexcept
{
    const SliderSpec& spec = SpecFor(key);
    const float clamped = std::min(std::max(normalized, 0.0f), 1.0f);
    return spec.minValue + static_cast<int>(std::lround(clamped * static_cast<float>(spec.maxValue - spec.minValue)));
}

std::wstring ToWide(std::string_view value)
{
    if (value.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

std::string ToUtf8(std::wstring_view value)
{
    if (value.empty()) {
        return {};
    }
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

std::wstring LowerCopy(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(towlower(c));
    });
    return value;
}

bool ContainsInsensitive(const std::wstring& haystack, const std::wstring& needle)
{
    return LowerCopy(haystack).find(LowerCopy(needle)) != std::wstring::npos;
}

std::wstring FeatureLevelToString(D3D_FEATURE_LEVEL level)
{
    switch (level) {
    case D3D_FEATURE_LEVEL_12_2:
        return L"12_2";
    case D3D_FEATURE_LEVEL_12_1:
        return L"12_1";
    case D3D_FEATURE_LEVEL_12_0:
        return L"12_0";
    case D3D_FEATURE_LEVEL_11_1:
        return L"11_1";
    case D3D_FEATURE_LEVEL_11_0:
        return L"11_0";
    default:
        return L"unknown";
    }
}

std::wstring ColorSpaceToString(DXGI_COLOR_SPACE_TYPE colorSpace)
{
    switch (colorSpace) {
    case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
        return L"HDR10 PQ / Rec.2020";
    case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
        return L"SDR linear / Rec.709";
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
        return L"SDR sRGB / Rec.709";
    case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
        return L"HDR10 studio PQ / Rec.2020";
    default:
        return L"DXGI color space " + std::to_wstring(static_cast<int>(colorSpace));
    }
}

std::map<std::wstring, std::wstring> BuildMonitorNameMap()
{
    std::map<std::wstring, std::wstring> result;
    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    const LONG bufferResult = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
    if (bufferResult != ERROR_SUCCESS) {
        LogWarning(L"GetDisplayConfigBufferSizes failed while resolving monitor names.");
        return result;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    const LONG queryResult = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
    if (queryResult != ERROR_SUCCESS) {
        LogWarning(L"QueryDisplayConfig failed while resolving monitor names.");
        return result;
    }

    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {};
        sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sourceName.header.size = sizeof(sourceName);
        sourceName.header.adapterId = paths[i].sourceInfo.adapterId;
        sourceName.header.id = paths[i].sourceInfo.id;

        DISPLAYCONFIG_TARGET_DEVICE_NAME targetName = {};
        targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        targetName.header.size = sizeof(targetName);
        targetName.header.adapterId = paths[i].targetInfo.adapterId;
        targetName.header.id = paths[i].targetInfo.id;

        if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS &&
            DisplayConfigGetDeviceInfo(&targetName.header) == ERROR_SUCCESS &&
            wcslen(targetName.monitorFriendlyDeviceName) > 0) {
            result[sourceName.viewGdiDeviceName] = targetName.monitorFriendlyDeviceName;
        }
    }
    return result;
}

D3D_FEATURE_LEVEL QueryFeatureLevel(IDXGIAdapter1* adapter)
{
    ComPtr<ID3D12Device> device;
    HRESULT hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    HR_CHECK(hr, L"D3D12CreateDevice");
    if (FAILED(hr)) {
        return D3D_FEATURE_LEVEL_11_0;
    }

    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevels = {};
    featureLevels.NumFeatureLevels = static_cast<UINT>(std::size(levels));
    featureLevels.pFeatureLevelsRequested = levels;
    featureLevels.MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    hr = device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featureLevels, sizeof(featureLevels));
    HR_CHECK(hr, L"ID3D12Device::CheckFeatureSupport FEATURE_LEVELS");
    return SUCCEEDED(hr) ? featureLevels.MaxSupportedFeatureLevel : D3D_FEATURE_LEVEL_11_0;
}

std::wstring FormatSliderValue(const SliderSpec& spec, int value)
{
    return std::to_wstring(value) + spec.suffix;
}

} // namespace

DashboardController::DashboardController()
    : budgetAllocator_(SafeBudgetAllocator::kFallbackVramMb * SafeBudgetAllocator::kOneMb)
{
    apply_budget_settings_locked(GraphicsBudgetSettings {});
}

DashboardController::~DashboardController()
{
    Shutdown();
}

bool DashboardController::Initialize(std::filesystem::path repoRoot)
{
    repoRoot_ = std::move(repoRoot);
    settingsPath_ = repoRoot_ / "config" / "scalability.json";
    coreTopology_ = talal::core::query_cpu_topology();

    {
        std::unique_lock lock(settingsMutex_);
        adapters_ = enumerate_graphics_hardware();
        select_default_hardware_locked();
        apply_preset_locked();
        const std::wstring warning = budgetAllocator_.warning();
        statusText_ = warning.empty() ? L"Dashboard initialized." : warning;
    }

    serializer_ = std::make_unique<ScalabilitySerializer>(
        settingsPath_,
        [this] { return snapshot_for_serializer(); },
        [this](const SerializableSettings& settings) { apply_loaded_settings(settings); },
        [this] { return default_serializable_settings(); });
    serializer_->LoadFromDisk();
    serializer_->Start();
    return true;
}

void DashboardController::Shutdown()
{
    if (serializer_) {
        serializer_->Stop();
        serializer_.reset();
    }
}

DashboardViewModel DashboardController::BuildViewModel() const
{
    std::shared_lock lock(settingsMutex_);
    DashboardViewModel view;
    view.selectedAdapter = state_.selectedAdapter;
    view.selectedDisplayMode = state_.selectedDisplayModeEntry;
    view.selectedFrameLimit = frame_limit_index_locked();
    view.hdrEnabled = state_.hdrEnabled;
    view.vsync = state_.vsync;
    view.statusText = statusText_;

    for (const AdapterInfo& adapter : adapters_) {
        std::wstringstream item;
        item << adapter.name << L" (" << SafeBudgetAllocator::bytes_to_mb(adapter.dedicatedVideoMemoryBytes)
             << L" MB VRAM, FL " << FeatureLevelToString(adapter.featureLevel) << L")";
        view.adapterItems.push_back(item.str());
    }

    const AdapterInfo* adapter = selected_adapter_locked();
    for (const ModeEntry& entry : displayModeEntries_) {
        if (!adapter || entry.outputIndex >= adapter->outputs.size()) {
            continue;
        }
        const OutputInfo& output = adapter->outputs[entry.outputIndex];
        if (entry.modeIndex < output.modes.size()) {
            view.displayModeItems.push_back(FormatDisplayMode(output.modes[entry.modeIndex]));
        }
    }
    if (view.displayModeItems.empty()) {
        view.displayModeItems.push_back(FormatDisplayMode(FallbackDisplayMode()));
    }

    for (int limit : kFrameLimits) {
        view.frameLimitItems.push_back(limit == 0 ? L"Unlimited" : std::to_wstring(limit));
    }

    const GraphicsBudgetSettings budgetSettings = budget_settings_from_state_locked();
    const BudgetEstimate estimate = budgetAllocator_.estimate(budgetSettings);
    const std::uint64_t usedMb = SafeBudgetAllocator::bytes_to_mb(estimate.totalBytes);
    const std::uint64_t usableMb = SafeBudgetAllocator::bytes_to_mb(estimate.usableBytes);
    const std::int64_t headroomMb = estimate.headroom_mb();
    view.vramProgressPercent = static_cast<int>(estimate.used_percent());
    if (estimate.headroom_percent() > 15) {
        view.vramSeverity = BudgetSeverity::Green;
    } else if (estimate.headroom_percent() >= 5) {
        view.vramSeverity = BudgetSeverity::Yellow;
    } else {
        view.vramSeverity = BudgetSeverity::Red;
    }
    std::wstringstream vram;
    vram << L"Used: " << usedMb << L" MB / " << usableMb << L" MB | Headroom: " << headroomMb << L" MB";
    view.vramText = vram.str();

    std::array<const wchar_t*, 7> sectionOrder {
        L"RENDERING",
        L"SHADOWS",
        L"GEOMETRY",
        L"EFFECTS",
        L"POPULATION",
        L"RAY TRACING",
        L"PERFORMANCE"
    };
    for (const wchar_t* sectionName : sectionOrder) {
        SectionViewModel section;
        section.name = sectionName;
        for (const SliderSpec& spec : kSliderSpecs) {
            if (wcscmp(spec.section, sectionName) != 0) {
                continue;
            }
            const int value = DenormalizeValue(spec.key, state_.normalizedSliders[static_cast<std::size_t>(spec.key)]);
            section.sliders.push_back(SliderViewModel {
                spec.key,
                spec.label,
                spec.suffix,
                spec.minValue,
                spec.maxValue,
                value,
                FormatSliderValue(spec, value)
            });
        }
        view.sections.push_back(std::move(section));
    }

    const OutputInfo* output = selected_output_locked();
    const DisplayMode* mode = selected_mode_locked();
    view.hdrAllowed = output ? output->hdr10Capable : false;
    std::wstringstream hardware;
    hardware << L"Core Systems Boot Probe\r\n";
    hardware << talal::core::describe_cpu_topology(coreTopology_) << L"\r\n\r\n";
    hardware << L"DXGI Adapter Enumeration\r\n";
    hardware << L"Adapters found: " << adapters_.size() << L"\r\n\r\n";
    for (std::size_t i = 0; i < adapters_.size(); ++i) {
        const AdapterInfo& gpu = adapters_[i];
        hardware << (i == state_.selectedAdapter ? L"> " : L"  ") << gpu.name << L"\r\n";
        hardware << L"    Vendor: 0x" << std::hex << gpu.vendorId << L" Device: 0x" << gpu.deviceId << std::dec
                 << L" VRAM: " << SafeBudgetAllocator::bytes_to_mb(gpu.dedicatedVideoMemoryBytes) << L" MB"
                 << L" FL " << FeatureLevelToString(gpu.featureLevel);
        if (gpu.rtx4070) {
            hardware << L" RTX 4070 preset match";
        }
        hardware << L"\r\n";
        for (std::size_t j = 0; j < gpu.outputs.size(); ++j) {
            const OutputInfo& out = gpu.outputs[j];
            hardware << L"    " << (output == &out ? L"* " : L"- ")
                     << out.friendlyName << L" (" << out.deviceName << L") "
                     << out.currentWidth << L"x" << out.currentHeight << L" @ " << out.currentRefreshHz << L"Hz "
                     << (out.hdr10Capable ? L"HDR10 " : L"SDR ")
                     << ColorSpaceToString(out.colorSpace) << L" | modes " << out.modes.size() << L"\r\n";
        }
        hardware << L"\r\n";
    }
    view.hardwareText = hardware.str();

    std::wstringstream hdrPath;
    if (output && state_.hdrEnabled && output->hdr10Capable) {
        hdrPath << L"HDR path: DXGI HDR10 output enabled for "
                << (mode ? FormatDisplayMode(*mode) : FormatDisplayMode(FallbackDisplayMode()))
                << L". Back buffer target R10G10B10A2_UNORM, color space RGB_FULL_G2084_NONE_P2020.";
    } else if (output && output->hdr10Capable) {
        hdrPath << L"HDR path: HDR10 display detected, but output is disabled in settings.";
    } else {
        hdrPath << L"HDR path: SDR Rec.709 output. HDR toggle is gated until the selected display reports HDR10.";
    }
    view.hdrPathText = hdrPath.str();

    std::wstringstream title;
    title << L"GameEngine - DEMO_WORKSTATION - ";
    title << (adapter ? adapter->name : L"No DXGI adapter");
    view.titleText = title.str();
    return view;
}

bool DashboardController::SetSliderValue(SettingKey key, int value)
{
    bool accepted = false;
    {
        std::unique_lock lock(settingsMutex_);
        const float previous = state_.normalizedSliders[static_cast<std::size_t>(key)];
        state_.normalizedSliders[static_cast<std::size_t>(key)] = NormalizeValue(key, value);
        const GraphicsBudgetSettings candidate = budget_settings_from_state_locked();
        if (budgetAllocator_.can_accept(candidate)) {
            statusText_ = L"Settings changed.";
            accepted = true;
        } else {
            state_.normalizedSliders[static_cast<std::size_t>(key)] = previous;
            statusText_ = L"VRAM limit reached - reduce another setting.";
        }
    }
    if (accepted) {
        mark_dirty();
    }
    return accepted;
}

int DashboardController::SliderValue(SettingKey key) const
{
    std::shared_lock lock(settingsMutex_);
    return DenormalizeValue(key, state_.normalizedSliders[static_cast<std::size_t>(key)]);
}

void DashboardController::SelectAdapter(std::size_t index)
{
    {
        std::unique_lock lock(settingsMutex_);
        if (index >= adapters_.size()) {
            return;
        }
        state_.selectedAdapter = index;
        rebuild_display_mode_entries_locked();
        state_.selectedDisplayModeEntry = 0;
        const AdapterInfo* adapter = selected_adapter_locked();
        budgetAllocator_.set_total_vram_bytes(adapter ? adapter->dedicatedVideoMemoryBytes : 0);
        apply_budget_settings_locked(budgetAllocator_.clamp_to_safe_budget(budget_settings_from_state_locked()));
        statusText_ = L"Adapter selected.";
    }
    mark_dirty();
}

void DashboardController::SelectDisplayMode(std::size_t index)
{
    {
        std::unique_lock lock(settingsMutex_);
        if (index >= displayModeEntries_.size()) {
            return;
        }
        state_.selectedDisplayModeEntry = index;
        const OutputInfo* output = selected_output_locked();
        if (output) {
            state_.hdrEnabled = output->hdr10Capable;
        }
        statusText_ = L"Display mode selected.";
    }
    mark_dirty();
}

void DashboardController::SetFrameLimitByIndex(std::size_t index)
{
    {
        std::unique_lock lock(settingsMutex_);
        if (index >= kFrameLimits.size()) {
            return;
        }
        state_.frameLimit = kFrameLimits[index];
        statusText_ = L"Frame limiter changed.";
    }
    mark_dirty();
}

void DashboardController::SetHdrEnabled(bool enabled)
{
    bool accepted = false;
    {
        std::unique_lock lock(settingsMutex_);
        const OutputInfo* output = selected_output_locked();
        if (enabled && (!output || !output->hdr10Capable)) {
            state_.hdrEnabled = false;
            statusText_ = L"HDR10 is not available for the selected display mode.";
            return;
        }
        const bool previous = state_.hdrEnabled;
        state_.hdrEnabled = enabled;
        if (budgetAllocator_.can_accept(budget_settings_from_state_locked())) {
            statusText_ = enabled ? L"HDR enabled." : L"HDR disabled.";
            accepted = true;
        } else {
            state_.hdrEnabled = previous;
            statusText_ = L"VRAM limit reached - reduce another setting.";
        }
    }
    if (accepted) {
        mark_dirty();
    }
}

void DashboardController::SetVsync(bool enabled)
{
    {
        std::unique_lock lock(settingsMutex_);
        state_.vsync = enabled;
        statusText_ = enabled ? L"VSync enabled." : L"VSync disabled.";
    }
    mark_dirty();
}

void DashboardController::ApplyHardwarePreset()
{
    {
        std::unique_lock lock(settingsMutex_);
        apply_preset_locked();
        statusText_ = L"Safe hardware preset applied.";
    }
    mark_dirty();
}

void DashboardController::RefreshHardware()
{
    {
        std::unique_lock lock(settingsMutex_);
        adapters_ = enumerate_graphics_hardware();
        select_default_hardware_locked();
        apply_budget_settings_locked(budgetAllocator_.clamp_to_safe_budget(budget_settings_from_state_locked()));
        statusText_ = L"Hardware re-detected.";
    }
    mark_dirty();
}

void DashboardController::QueueSave()
{
    {
        std::unique_lock lock(settingsMutex_);
        statusText_ = L"Save queued for IO thread.";
    }
    mark_dirty();
}

int DashboardController::CurrentFrameLimit() const
{
    std::shared_lock lock(settingsMutex_);
    return state_.frameLimit;
}

std::vector<DashboardController::AdapterInfo> DashboardController::enumerate_graphics_hardware() const
{
    std::vector<AdapterInfo> adapters;
    const auto monitorNames = BuildMonitorNameMap();

    ComPtr<IDXGIFactory6> factory;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    HR_CHECK(hr, L"CreateDXGIFactory2");
    if (FAILED(hr)) {
        return adapters;
    }

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapterByGpuPreference(
            adapterIndex,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&adapter));
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        HR_CHECK(hr, L"IDXGIFactory6::EnumAdapterByGpuPreference");
        if (FAILED(hr)) {
            continue;
        }

        DXGI_ADAPTER_DESC1 desc = {};
        hr = adapter->GetDesc1(&desc);
        HR_CHECK(hr, L"IDXGIAdapter1::GetDesc1");
        if (FAILED(hr)) {
            continue;
        }

        AdapterInfo info;
        info.name = desc.Description;
        info.vendorId = desc.VendorId;
        info.deviceId = desc.DeviceId;
        info.dedicatedVideoMemoryBytes = desc.DedicatedVideoMemory;
        info.software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        info.rtx4070 = ContainsInsensitive(info.name, L"RTX 4070");
        info.featureLevel = QueryFeatureLevel(adapter.Get());

        for (UINT outputIndex = 0;; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            hr = adapter->EnumOutputs(outputIndex, &output);
            if (hr == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            HR_CHECK(hr, L"IDXGIAdapter1::EnumOutputs");
            if (FAILED(hr)) {
                continue;
            }

            DXGI_OUTPUT_DESC outputDesc = {};
            if (!TryGetOutputDescription(output.Get(), outputDesc)) {
                continue;
            }

            OutputInfo out;
            out.deviceName = outputDesc.DeviceName;
            out.attached = outputDesc.AttachedToDesktop != FALSE;
            out.desktop = outputDesc.DesktopCoordinates;
            out.primary = out.desktop.left == 0 && out.desktop.top == 0;
            out.hdr10Capable = QueryHdr10Capability(output.Get());
            out.colorSpace = QueryOutputColorSpace(output.Get());

            const auto friendly = monitorNames.find(out.deviceName);
            out.friendlyName = friendly != monitorNames.end() ? friendly->second : out.deviceName;

            DEVMODEW mode = {};
            mode.dmSize = sizeof(mode);
            if (EnumDisplaySettingsExW(out.deviceName.c_str(), ENUM_CURRENT_SETTINGS, &mode, 0) != FALSE) {
                out.currentWidth = static_cast<int>(mode.dmPelsWidth);
                out.currentHeight = static_cast<int>(mode.dmPelsHeight);
                out.currentRefreshHz = static_cast<int>(mode.dmDisplayFrequency);
            } else {
                out.currentWidth = out.desktop.right - out.desktop.left;
                out.currentHeight = out.desktop.bottom - out.desktop.top;
                out.currentRefreshHz = 60;
                LogWarning(L"EnumDisplaySettingsExW failed; using desktop rectangle and 60Hz fallback.");
            }

            out.modes = EnumerateSupportedModes(output.Get());
            if (out.modes.empty()) {
                LogWarning(L"Display mode enumeration returned empty; using 1920x1080 @ 60Hz fallback.");
                out.modes.push_back(FallbackDisplayMode());
            }
            info.outputs.push_back(std::move(out));
        }

        adapters.push_back(std::move(info));
    }

    return adapters;
}

void DashboardController::select_default_hardware_locked()
{
    state_.selectedAdapter = 0;
    for (std::size_t i = 0; i < adapters_.size(); ++i) {
        if (adapters_[i].rtx4070) {
            state_.selectedAdapter = i;
            break;
        }
    }

    const AdapterInfo* adapter = selected_adapter_locked();
    budgetAllocator_.set_total_vram_bytes(adapter ? adapter->dedicatedVideoMemoryBytes : 0);
    rebuild_display_mode_entries_locked();

    int bestScore = -1;
    state_.selectedDisplayModeEntry = 0;
    for (std::size_t i = 0; i < displayModeEntries_.size(); ++i) {
        const ModeEntry& entry = displayModeEntries_[i];
        const OutputInfo& output = adapters_[state_.selectedAdapter].outputs[entry.outputIndex];
        const DisplayMode& mode = output.modes[entry.modeIndex];
        const int score = static_cast<int>(mode.refresh_hz()) + (output.hdr10Capable ? 1000 : 0);
        if (score > bestScore) {
            bestScore = score;
            state_.selectedDisplayModeEntry = i;
        }
    }
}

void DashboardController::rebuild_display_mode_entries_locked()
{
    displayModeEntries_.clear();
    const AdapterInfo* adapter = selected_adapter_locked();
    if (!adapter) {
        return;
    }
    for (std::size_t outputIndex = 0; outputIndex < adapter->outputs.size(); ++outputIndex) {
        const OutputInfo& output = adapter->outputs[outputIndex];
        for (std::size_t modeIndex = 0; modeIndex < output.modes.size(); ++modeIndex) {
            displayModeEntries_.push_back(ModeEntry { outputIndex, modeIndex });
        }
    }
    if (displayModeEntries_.empty()) {
        displayModeEntries_.push_back(ModeEntry {});
    }
    state_.selectedDisplayModeEntry = std::min(state_.selectedDisplayModeEntry, displayModeEntries_.size() - 1);
}

void DashboardController::apply_preset_locked()
{
    GraphicsBudgetSettings preset;
    preset.texturePoolMb = 7680;
    preset.renderScalePercent = 100;
    preset.anisotropy = 16;
    preset.shadowQuality = 4;
    preset.shadowDistancePercent = 85;
    preset.shadowCascades = 4;
    preset.ambientOcclusionQuality = 3;
    preset.globalIlluminationQuality = 3;
    preset.reflectionQuality = 3;
    preset.volumetricFogQuality = 3;
    preset.foliageDensityPercent = 100;
    preset.viewDistancePercent = 85;
    preset.geometryDetailPercent = 90;
    preset.terrainQualityPercent = 90;
    preset.waterQualityPercent = 85;
    preset.trafficDensityPercent = 80;
    preset.crowdDensityPercent = 70;
    preset.particleQualityPercent = 85;
    preset.textureStreamingAggressiveness = 85;
    preset.rayTracingMode = 0;
    preset.rayTracingQuality = 0;
    preset.rayTracingReflections = 0;
    preset.hdrEnabled = state_.hdrEnabled;

    const AdapterInfo* adapter = selected_adapter_locked();
    if (adapter && adapter->rtx4070 && SafeBudgetAllocator::bytes_to_mb(adapter->dedicatedVideoMemoryBytes) >= 11000) {
        state_.presetName = L"RTX 4070 12GB - Safe High Refresh Ultra Raster";
    } else {
        state_.presetName = L"Safe High Preset";
        preset.texturePoolMb = 4096;
        preset.viewDistancePercent = 75;
        preset.geometryDetailPercent = 75;
        preset.terrainQualityPercent = 75;
    }

    apply_budget_settings_locked(budgetAllocator_.clamp_to_safe_budget(preset));
    const OutputInfo* output = selected_output_locked();
    state_.hdrEnabled = output ? output->hdr10Capable : false;
    const DisplayMode* mode = selected_mode_locked();
    const std::uint32_t hz = mode ? mode->refresh_hz() : 60;
    if (hz >= 240) {
        state_.frameLimit = 240;
    } else if (hz >= 165) {
        state_.frameLimit = 165;
    } else if (hz >= 144) {
        state_.frameLimit = 144;
    } else if (hz >= 120) {
        state_.frameLimit = 120;
    } else {
        state_.frameLimit = 60;
    }
}

void DashboardController::apply_budget_settings_locked(const GraphicsBudgetSettings& settings)
{
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::RenderScale)] = NormalizeValue(SettingKey::RenderScale, settings.renderScalePercent);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::TexturePool)] = NormalizeValue(SettingKey::TexturePool, settings.texturePoolMb);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::Anisotropy)] = NormalizeValue(SettingKey::Anisotropy, settings.anisotropy);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::AmbientOcclusion)] = NormalizeValue(SettingKey::AmbientOcclusion, settings.ambientOcclusionQuality);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::GlobalIllumination)] = NormalizeValue(SettingKey::GlobalIllumination, settings.globalIlluminationQuality);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::ShadowQuality)] = NormalizeValue(SettingKey::ShadowQuality, settings.shadowQuality);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::ShadowDistance)] = NormalizeValue(SettingKey::ShadowDistance, settings.shadowDistancePercent);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::ShadowCascades)] = NormalizeValue(SettingKey::ShadowCascades, settings.shadowCascades);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::ViewDistance)] = NormalizeValue(SettingKey::ViewDistance, settings.viewDistancePercent);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::TerrainQuality)] = NormalizeValue(SettingKey::TerrainQuality, settings.terrainQualityPercent);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::GeometryDetail)] = NormalizeValue(SettingKey::GeometryDetail, settings.geometryDetailPercent);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::VolumetricFog)] = NormalizeValue(SettingKey::VolumetricFog, settings.volumetricFogQuality);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::WaterQuality)] = NormalizeValue(SettingKey::WaterQuality, settings.waterQualityPercent);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::ParticleQuality)] = NormalizeValue(SettingKey::ParticleQuality, settings.particleQualityPercent);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::FoliageDensity)] = NormalizeValue(SettingKey::FoliageDensity, settings.foliageDensityPercent);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::TrafficDensity)] = NormalizeValue(SettingKey::TrafficDensity, settings.trafficDensityPercent);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::CrowdDensity)] = NormalizeValue(SettingKey::CrowdDensity, settings.crowdDensityPercent);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::RayTracingMode)] = NormalizeValue(SettingKey::RayTracingMode, settings.rayTracingMode);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::RayTracingQuality)] = NormalizeValue(SettingKey::RayTracingQuality, settings.rayTracingQuality);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::RayTracingReflections)] = NormalizeValue(SettingKey::RayTracingReflections, settings.rayTracingReflections);
    state_.normalizedSliders[static_cast<std::size_t>(SettingKey::TextureStreaming)] = NormalizeValue(SettingKey::TextureStreaming, settings.textureStreamingAggressiveness);
}

GraphicsBudgetSettings DashboardController::budget_settings_from_state_locked() const
{
    GraphicsBudgetSettings settings;
    settings.renderScalePercent = DenormalizeValue(SettingKey::RenderScale, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::RenderScale)]);
    settings.texturePoolMb = DenormalizeValue(SettingKey::TexturePool, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::TexturePool)]);
    settings.anisotropy = DenormalizeValue(SettingKey::Anisotropy, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::Anisotropy)]);
    settings.ambientOcclusionQuality = DenormalizeValue(SettingKey::AmbientOcclusion, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::AmbientOcclusion)]);
    settings.globalIlluminationQuality = DenormalizeValue(SettingKey::GlobalIllumination, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::GlobalIllumination)]);
    settings.shadowQuality = DenormalizeValue(SettingKey::ShadowQuality, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::ShadowQuality)]);
    settings.shadowDistancePercent = DenormalizeValue(SettingKey::ShadowDistance, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::ShadowDistance)]);
    settings.shadowCascades = DenormalizeValue(SettingKey::ShadowCascades, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::ShadowCascades)]);
    settings.viewDistancePercent = DenormalizeValue(SettingKey::ViewDistance, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::ViewDistance)]);
    settings.terrainQualityPercent = DenormalizeValue(SettingKey::TerrainQuality, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::TerrainQuality)]);
    settings.geometryDetailPercent = DenormalizeValue(SettingKey::GeometryDetail, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::GeometryDetail)]);
    settings.volumetricFogQuality = DenormalizeValue(SettingKey::VolumetricFog, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::VolumetricFog)]);
    settings.waterQualityPercent = DenormalizeValue(SettingKey::WaterQuality, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::WaterQuality)]);
    settings.particleQualityPercent = DenormalizeValue(SettingKey::ParticleQuality, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::ParticleQuality)]);
    settings.foliageDensityPercent = DenormalizeValue(SettingKey::FoliageDensity, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::FoliageDensity)]);
    settings.trafficDensityPercent = DenormalizeValue(SettingKey::TrafficDensity, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::TrafficDensity)]);
    settings.crowdDensityPercent = DenormalizeValue(SettingKey::CrowdDensity, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::CrowdDensity)]);
    settings.rayTracingMode = DenormalizeValue(SettingKey::RayTracingMode, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::RayTracingMode)]);
    settings.rayTracingQuality = DenormalizeValue(SettingKey::RayTracingQuality, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::RayTracingQuality)]);
    settings.rayTracingReflections = DenormalizeValue(SettingKey::RayTracingReflections, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::RayTracingReflections)]);
    settings.textureStreamingAggressiveness = DenormalizeValue(SettingKey::TextureStreaming, state_.normalizedSliders[static_cast<std::size_t>(SettingKey::TextureStreaming)]);
    settings.hdrEnabled = state_.hdrEnabled;
    return settings;
}

SerializableSettings DashboardController::snapshot_for_serializer() const
{
    std::shared_lock lock(settingsMutex_);
    SerializableSettings settings;
    const GraphicsBudgetSettings budgetSettings = budget_settings_from_state_locked();
    const BudgetEstimate estimate = budgetAllocator_.estimate(budgetSettings);
    const AdapterInfo* adapter = selected_adapter_locked();
    const OutputInfo* output = selected_output_locked();
    const DisplayMode* mode = selected_mode_locked();

    settings.profile = ToUtf8(state_.presetName);
    settings.adapterName = adapter ? ToUtf8(adapter->name) : "";
    settings.displayName = output ? ToUtf8(output->friendlyName) : "";
    settings.colorSpace = output ? ToUtf8(ColorSpaceToString(output->colorSpace)) : "unknown";
    settings.dedicatedVramMb = adapter ? SafeBudgetAllocator::bytes_to_mb(adapter->dedicatedVideoMemoryBytes) : 0;
    settings.displayWidth = mode ? static_cast<int>(mode->width) : 1920;
    settings.displayHeight = mode ? static_cast<int>(mode->height) : 1080;
    settings.refreshHz = mode ? static_cast<int>(mode->refresh_hz()) : 60;
    settings.frameLimit = state_.frameLimit;
    settings.hdrEnabled = state_.hdrEnabled;
    settings.hdrDetected = output ? output->hdr10Capable : false;
    settings.vsync = state_.vsync;
    settings.renderScalePercent = budgetSettings.renderScalePercent;
    settings.texturePoolMb = budgetSettings.texturePoolMb;
    settings.anisotropy = budgetSettings.anisotropy;
    settings.shadowQuality = budgetSettings.shadowQuality;
    settings.shadowDistancePercent = budgetSettings.shadowDistancePercent;
    settings.shadowCascades = budgetSettings.shadowCascades;
    settings.ambientOcclusionQuality = budgetSettings.ambientOcclusionQuality;
    settings.globalIlluminationQuality = budgetSettings.globalIlluminationQuality;
    settings.reflectionQuality = budgetSettings.reflectionQuality;
    settings.volumetricFogQuality = budgetSettings.volumetricFogQuality;
    settings.foliageDensityPercent = budgetSettings.foliageDensityPercent;
    settings.viewDistancePercent = budgetSettings.viewDistancePercent;
    settings.geometryDetailPercent = budgetSettings.geometryDetailPercent;
    settings.terrainQualityPercent = budgetSettings.terrainQualityPercent;
    settings.waterQualityPercent = budgetSettings.waterQualityPercent;
    settings.trafficDensityPercent = budgetSettings.trafficDensityPercent;
    settings.crowdDensityPercent = budgetSettings.crowdDensityPercent;
    settings.particleQualityPercent = budgetSettings.particleQualityPercent;
    settings.textureStreamingAggressiveness = budgetSettings.textureStreamingAggressiveness;
    settings.rayTracingMode = budgetSettings.rayTracingMode;
    settings.rayTracingQuality = budgetSettings.rayTracingQuality;
    settings.rayTracingReflections = budgetSettings.rayTracingReflections;
    settings.usableVramBudgetMb = SafeBudgetAllocator::bytes_to_mb(estimate.usableBytes);
    settings.estimatedTotalMb = SafeBudgetAllocator::bytes_to_mb(estimate.totalBytes);
    settings.estimatedHeadroomMb = estimate.headroom_mb();
    return settings;
}

SerializableSettings DashboardController::default_serializable_settings() const
{
    return snapshot_for_serializer();
}

void DashboardController::apply_loaded_settings(const SerializableSettings& serialized)
{
    std::unique_lock lock(settingsMutex_);
    GraphicsBudgetSettings settings;
    settings.renderScalePercent = serialized.renderScalePercent;
    settings.texturePoolMb = serialized.texturePoolMb;
    settings.anisotropy = serialized.anisotropy;
    settings.shadowQuality = serialized.shadowQuality;
    settings.shadowDistancePercent = serialized.shadowDistancePercent;
    settings.shadowCascades = serialized.shadowCascades;
    settings.ambientOcclusionQuality = serialized.ambientOcclusionQuality;
    settings.globalIlluminationQuality = serialized.globalIlluminationQuality;
    settings.reflectionQuality = serialized.reflectionQuality;
    settings.volumetricFogQuality = serialized.volumetricFogQuality;
    settings.foliageDensityPercent = serialized.foliageDensityPercent;
    settings.viewDistancePercent = serialized.viewDistancePercent;
    settings.geometryDetailPercent = serialized.geometryDetailPercent;
    settings.terrainQualityPercent = serialized.terrainQualityPercent;
    settings.waterQualityPercent = serialized.waterQualityPercent;
    settings.trafficDensityPercent = serialized.trafficDensityPercent;
    settings.crowdDensityPercent = serialized.crowdDensityPercent;
    settings.particleQualityPercent = serialized.particleQualityPercent;
    settings.textureStreamingAggressiveness = serialized.textureStreamingAggressiveness;
    settings.rayTracingMode = serialized.rayTracingMode;
    settings.rayTracingQuality = serialized.rayTracingQuality;
    settings.rayTracingReflections = serialized.rayTracingReflections;
    settings.hdrEnabled = serialized.hdrEnabled;
    apply_budget_settings_locked(budgetAllocator_.clamp_to_safe_budget(settings));
    state_.frameLimit = serialized.frameLimit;
    state_.hdrEnabled = serialized.hdrEnabled && (!selected_output_locked() || selected_output_locked()->hdr10Capable);
    state_.vsync = serialized.vsync;
    state_.presetName = ToWide(serialized.profile);

    for (std::size_t i = 0; i < displayModeEntries_.size(); ++i) {
        const AdapterInfo* adapter = selected_adapter_locked();
        if (!adapter) {
            break;
        }
        const ModeEntry& entry = displayModeEntries_[i];
        const OutputInfo& output = adapter->outputs[entry.outputIndex];
        const DisplayMode& mode = output.modes[entry.modeIndex];
        if (static_cast<int>(mode.width) == serialized.displayWidth &&
            static_cast<int>(mode.height) == serialized.displayHeight &&
            static_cast<int>(mode.refresh_hz()) == serialized.refreshHz) {
            state_.selectedDisplayModeEntry = i;
            break;
        }
    }
    statusText_ = L"Loaded scalability.json.";
}

const DashboardController::AdapterInfo* DashboardController::selected_adapter_locked() const noexcept
{
    if (adapters_.empty() || state_.selectedAdapter >= adapters_.size()) {
        return nullptr;
    }
    return &adapters_[state_.selectedAdapter];
}

const DashboardController::OutputInfo* DashboardController::selected_output_locked() const noexcept
{
    const AdapterInfo* adapter = selected_adapter_locked();
    if (!adapter || displayModeEntries_.empty() || state_.selectedDisplayModeEntry >= displayModeEntries_.size()) {
        return nullptr;
    }
    const ModeEntry& entry = displayModeEntries_[state_.selectedDisplayModeEntry];
    if (entry.outputIndex >= adapter->outputs.size()) {
        return nullptr;
    }
    return &adapter->outputs[entry.outputIndex];
}

const DisplayMode* DashboardController::selected_mode_locked() const noexcept
{
    const AdapterInfo* adapter = selected_adapter_locked();
    if (!adapter || displayModeEntries_.empty() || state_.selectedDisplayModeEntry >= displayModeEntries_.size()) {
        return nullptr;
    }
    const ModeEntry& entry = displayModeEntries_[state_.selectedDisplayModeEntry];
    if (entry.outputIndex >= adapter->outputs.size()) {
        return nullptr;
    }
    const OutputInfo& output = adapter->outputs[entry.outputIndex];
    if (entry.modeIndex >= output.modes.size()) {
        return nullptr;
    }
    return &output.modes[entry.modeIndex];
}

std::size_t DashboardController::frame_limit_index_locked() const noexcept
{
    for (std::size_t i = 0; i < kFrameLimits.size(); ++i) {
        if (kFrameLimits[i] == state_.frameLimit) {
            return i;
        }
    }
    return 5;
}

void DashboardController::set_status_locked(std::wstring text)
{
    statusText_ = std::move(text);
}

void DashboardController::mark_dirty() const noexcept
{
    if (serializer_) {
        serializer_->MarkDirty();
    }
}

} // namespace talal::dashboard
