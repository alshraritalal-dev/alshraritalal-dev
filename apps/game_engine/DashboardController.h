#pragma once

#include "BudgetAllocator.h"
#include "DisplayEnumerator.h"
#include "ScalabilitySerializer.h"
#include "core/platform/cpu_topology.h"

#include <d3d12.h>

#include <array>
#include <filesystem>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

namespace talal::dashboard {

enum class SettingKey : std::uint32_t {
    RenderScale,
    TexturePool,
    Anisotropy,
    AmbientOcclusion,
    GlobalIllumination,
    ShadowQuality,
    ShadowDistance,
    ShadowCascades,
    ViewDistance,
    TerrainQuality,
    GeometryDetail,
    VolumetricFog,
    WaterQuality,
    ParticleQuality,
    FoliageDensity,
    TrafficDensity,
    CrowdDensity,
    RayTracingMode,
    RayTracingQuality,
    RayTracingReflections,
    TextureStreaming,
    Count
};

constexpr std::size_t kSettingKeyCount = static_cast<std::size_t>(SettingKey::Count);

enum class BudgetSeverity {
    Green,
    Yellow,
    Red
};

struct SliderViewModel {
    SettingKey key = SettingKey::RenderScale;
    std::wstring label;
    std::wstring suffix;
    int minValue = 0;
    int maxValue = 100;
    int value = 0;
    std::wstring formattedValue;
};

struct SectionViewModel {
    std::wstring name;
    std::vector<SliderViewModel> sliders;
};

struct DashboardViewModel {
    std::vector<std::wstring> adapterItems;
    std::vector<std::wstring> displayModeItems;
    std::vector<std::wstring> frameLimitItems;
    std::vector<SectionViewModel> sections;
    std::size_t selectedAdapter = 0;
    std::size_t selectedDisplayMode = 0;
    std::size_t selectedFrameLimit = 0;
    bool hdrEnabled = false;
    bool hdrAllowed = false;
    bool vsync = false;
    std::wstring hardwareText;
    std::wstring hdrPathText;
    std::wstring vramText;
    std::wstring statusText;
    std::wstring titleText;
    int vramProgressPercent = 0;
    BudgetSeverity vramSeverity = BudgetSeverity::Green;
};

class DashboardController {
public:
    DashboardController();
    ~DashboardController();

    DashboardController(const DashboardController&) = delete;
    DashboardController& operator=(const DashboardController&) = delete;

    bool Initialize(std::filesystem::path repoRoot);
    void Shutdown();

    DashboardViewModel BuildViewModel() const;
    bool SetSliderValue(SettingKey key, int value);
    int SliderValue(SettingKey key) const;
    void SelectAdapter(std::size_t index);
    void SelectDisplayMode(std::size_t index);
    void SetFrameLimitByIndex(std::size_t index);
    void SetHdrEnabled(bool enabled);
    void SetVsync(bool enabled);
    void ApplyHardwarePreset();
    void RefreshHardware();
    void QueueSave();
    int CurrentFrameLimit() const;

private:
    struct OutputInfo {
        std::wstring deviceName;
        std::wstring friendlyName;
        int currentWidth = 1920;
        int currentHeight = 1080;
        int currentRefreshHz = 60;
        bool attached = false;
        bool primary = false;
        bool hdr10Capable = false;
        DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        RECT desktop = {};
        std::vector<DisplayMode> modes;
    };

    struct AdapterInfo {
        std::wstring name;
        std::uint32_t vendorId = 0;
        std::uint32_t deviceId = 0;
        std::uint64_t dedicatedVideoMemoryBytes = 0;
        bool software = false;
        bool rtx4070 = false;
        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
        std::vector<OutputInfo> outputs;
    };

    struct ModeEntry {
        std::size_t outputIndex = 0;
        std::size_t modeIndex = 0;
    };

    struct DashboardState {
        std::array<float, kSettingKeyCount> normalizedSliders {};
        std::size_t selectedAdapter = 0;
        std::size_t selectedDisplayModeEntry = 0;
        int frameLimit = 240;
        bool hdrEnabled = true;
        bool vsync = false;
        std::wstring presetName = L"RTX 4070 12GB - High Refresh Ultra Raster";
    };

    std::vector<AdapterInfo> enumerate_graphics_hardware() const;
    void select_default_hardware_locked();
    void rebuild_display_mode_entries_locked();
    void apply_preset_locked();
    void apply_budget_settings_locked(const GraphicsBudgetSettings& settings);
    GraphicsBudgetSettings budget_settings_from_state_locked() const;
    SerializableSettings snapshot_for_serializer() const;
    SerializableSettings default_serializable_settings() const;
    void apply_loaded_settings(const SerializableSettings& settings);
    const AdapterInfo* selected_adapter_locked() const noexcept;
    const OutputInfo* selected_output_locked() const noexcept;
    const DisplayMode* selected_mode_locked() const noexcept;
    std::size_t frame_limit_index_locked() const noexcept;
    void set_status_locked(std::wstring text);
    void mark_dirty() const noexcept;

    mutable std::shared_mutex settingsMutex_;
    std::filesystem::path repoRoot_;
    std::filesystem::path settingsPath_;
    talal::core::CpuTopology coreTopology_;
    SafeBudgetAllocator budgetAllocator_;
    std::vector<AdapterInfo> adapters_;
    std::vector<ModeEntry> displayModeEntries_;
    DashboardState state_;
    std::wstring statusText_;
    std::unique_ptr<ScalabilitySerializer> serializer_;
};

} // namespace talal::dashboard
