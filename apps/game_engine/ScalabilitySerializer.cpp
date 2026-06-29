#include "ScalabilitySerializer.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace talal::dashboard {
namespace {

using json = nlohmann::json;

void LogSerializerError(const std::wstring& message) noexcept
{
    try {
        std::wstring line = L"[DEMO_WORKSTATION][Serializer] ";
        line.append(message);
        line.append(L"\n");
        OutputDebugStringW(line.c_str());
    } catch (...) {
        OutputDebugStringW(L"[DEMO_WORKSTATION][Serializer] error logging failed\n");
    }
}

json ToJson(const SerializableSettings& settings)
{
    json doc;
    doc["version"] = 2;
    doc["machine"] = settings.machine;
    doc["profile"] = settings.profile;
    doc["backend"] = settings.backend;
    doc["cpu"] = {
        { "name", "Intel Core i7-12700F" },
        { "threads", 20 },
        { "baseline", "AVX2" }
    };
    doc["adapter"] = {
        { "name", settings.adapterName },
        { "dedicated_vram_mb", settings.dedicatedVramMb }
    };
    doc["display"] = {
        { "friendly_name", settings.displayName },
        { "mode", "borderless" },
        { "width", settings.displayWidth },
        { "height", settings.displayHeight },
        { "refresh_hz", settings.refreshHz },
        { "hdr_detected", settings.hdrDetected },
        { "hdr_enabled", settings.hdrEnabled },
        { "color_space", settings.colorSpace },
        { "vsync", settings.vsync ? "on" : "off" },
        { "frame_limit", settings.frameLimit == 0 ? "unlimited" : std::to_string(settings.frameLimit) }
    };
    doc["quality"] = {
        { "render_scale_percent", settings.renderScalePercent },
        { "texture_pool_mb", settings.texturePoolMb },
        { "anisotropy", settings.anisotropy },
        { "shadow_quality", settings.shadowQuality },
        { "shadow_distance_percent", settings.shadowDistancePercent },
        { "shadow_cascades", settings.shadowCascades },
        { "ambient_occlusion_quality", settings.ambientOcclusionQuality },
        { "global_illumination_quality", settings.globalIlluminationQuality },
        { "reflection_quality", settings.reflectionQuality },
        { "volumetric_fog_quality", settings.volumetricFogQuality },
        { "foliage_density_percent", settings.foliageDensityPercent },
        { "view_distance_percent", settings.viewDistancePercent },
        { "geometry_detail_percent", settings.geometryDetailPercent },
        { "terrain_quality_percent", settings.terrainQualityPercent },
        { "water_quality_percent", settings.waterQualityPercent },
        { "traffic_density_percent", settings.trafficDensityPercent },
        { "crowd_density_percent", settings.crowdDensityPercent },
        { "particle_quality_percent", settings.particleQualityPercent },
        { "texture_streaming_aggressiveness", settings.textureStreamingAggressiveness },
        { "ray_tracing_mode", settings.rayTracingMode },
        { "ray_tracing_quality", settings.rayTracingQuality },
        { "ray_tracing_reflections", settings.rayTracingReflections }
    };
    doc["budgets"] = {
        { "dedicated_vram_mb", settings.dedicatedVramMb },
        { "usable_vram_budget_mb", settings.usableVramBudgetMb },
        { "estimated_total_mb", settings.estimatedTotalMb },
        { "estimated_headroom_mb", settings.estimatedHeadroomMb }
    };
    doc["hdr_output_path"] = settings.hdrEnabled && settings.hdrDetected
        ? "DXGI HDR10: R10G10B10A2_UNORM, RGB_FULL_G2084_NONE_P2020, HDR metadata"
        : "SDR Rec.709";
    return doc;
}

int ReadFrameLimit(const json& display, int fallback)
{
    if (!display.contains("frame_limit")) {
        return fallback;
    }
    if (display["frame_limit"].is_number_integer()) {
        return display["frame_limit"].get<int>();
    }
    if (display["frame_limit"].is_string()) {
        const std::string value = display["frame_limit"].get<std::string>();
        if (value == "unlimited") {
            return 0;
        }
        try {
            return std::stoi(value);
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

template <typename T>
T JsonValue(const json& object, const char* key, T fallback)
{
    if (!object.contains(key)) {
        return fallback;
    }
    try {
        return object.at(key).get<T>();
    } catch (...) {
        return fallback;
    }
}

SerializableSettings FromJsonWithDefaults(const json& doc, const SerializableSettings& defaults)
{
    SerializableSettings settings = defaults;
    settings.version = JsonValue<int>(doc, "version", 1);
    settings.machine = JsonValue<std::string>(doc, "machine", settings.machine);
    settings.profile = JsonValue<std::string>(doc, "profile", settings.profile);
    settings.backend = JsonValue<std::string>(doc, "backend", settings.backend);

    const json adapter = doc.contains("adapter") && doc["adapter"].is_object() ? doc["adapter"] : json::object();
    settings.adapterName = JsonValue<std::string>(adapter, "name", settings.adapterName);
    settings.dedicatedVramMb = JsonValue<std::uint64_t>(adapter, "dedicated_vram_mb", settings.dedicatedVramMb);

    const json display = doc.contains("display") && doc["display"].is_object() ? doc["display"] : json::object();
    settings.displayName = JsonValue<std::string>(display, "friendly_name", settings.displayName);
    settings.displayWidth = JsonValue<int>(display, "width", settings.displayWidth);
    settings.displayHeight = JsonValue<int>(display, "height", settings.displayHeight);
    settings.refreshHz = JsonValue<int>(display, "refresh_hz", settings.refreshHz);
    settings.hdrDetected = JsonValue<bool>(display, "hdr_detected", settings.hdrDetected);
    settings.hdrEnabled = JsonValue<bool>(display, "hdr_enabled", settings.hdrEnabled);
    settings.colorSpace = JsonValue<std::string>(display, "color_space", settings.colorSpace);
    settings.frameLimit = ReadFrameLimit(display, settings.frameLimit);
    if (display.contains("vsync") && display["vsync"].is_string()) {
        settings.vsync = display["vsync"].get<std::string>() == "on";
    }

    const json quality = doc.contains("quality") && doc["quality"].is_object() ? doc["quality"] : json::object();
    settings.renderScalePercent = JsonValue<int>(quality, "render_scale_percent", settings.renderScalePercent);
    settings.texturePoolMb = JsonValue<int>(quality, "texture_pool_mb", settings.texturePoolMb);
    settings.anisotropy = JsonValue<int>(quality, "anisotropy", settings.anisotropy);
    settings.shadowQuality = JsonValue<int>(quality, "shadow_quality", settings.shadowQuality);
    settings.shadowDistancePercent = JsonValue<int>(quality, "shadow_distance_percent", settings.shadowDistancePercent);
    settings.shadowCascades = JsonValue<int>(quality, "shadow_cascades", settings.shadowCascades);
    settings.ambientOcclusionQuality = JsonValue<int>(quality, "ambient_occlusion_quality", settings.ambientOcclusionQuality);
    settings.globalIlluminationQuality = JsonValue<int>(quality, "global_illumination_quality", settings.globalIlluminationQuality);
    settings.reflectionQuality = JsonValue<int>(quality, "reflection_quality", settings.reflectionQuality);
    settings.volumetricFogQuality = JsonValue<int>(quality, "volumetric_fog_quality", settings.volumetricFogQuality);
    settings.foliageDensityPercent = JsonValue<int>(quality, "foliage_density_percent", settings.foliageDensityPercent);
    settings.viewDistancePercent = JsonValue<int>(quality, "view_distance_percent", settings.viewDistancePercent);
    settings.geometryDetailPercent = JsonValue<int>(quality, "geometry_detail_percent", settings.geometryDetailPercent);
    settings.terrainQualityPercent = JsonValue<int>(quality, "terrain_quality_percent", settings.terrainQualityPercent);
    settings.waterQualityPercent = JsonValue<int>(quality, "water_quality_percent", settings.waterQualityPercent);
    settings.trafficDensityPercent = JsonValue<int>(quality, "traffic_density_percent", settings.trafficDensityPercent);
    settings.crowdDensityPercent = JsonValue<int>(quality, "crowd_density_percent", settings.crowdDensityPercent);
    settings.particleQualityPercent = JsonValue<int>(quality, "particle_quality_percent", settings.particleQualityPercent);
    settings.textureStreamingAggressiveness = JsonValue<int>(quality, "texture_streaming_aggressiveness", settings.textureStreamingAggressiveness);
    settings.rayTracingMode = JsonValue<int>(quality, "ray_tracing_mode", settings.rayTracingMode);
    settings.rayTracingQuality = JsonValue<int>(quality, "ray_tracing_quality", settings.rayTracingQuality);
    settings.rayTracingReflections = JsonValue<int>(quality, "ray_tracing_reflections", settings.rayTracingReflections);
    return settings;
}

} // namespace

ScalabilitySerializer::ScalabilitySerializer(
    std::filesystem::path settingsPath,
    SnapshotProvider snapshotProvider,
    ApplySettings applySettings,
    DefaultProvider defaultProvider)
    : settingsPath_(std::move(settingsPath))
    , tempPath_(settingsPath_)
    , snapshotProvider_(std::move(snapshotProvider))
    , applySettings_(std::move(applySettings))
    , defaultProvider_(std::move(defaultProvider))
{
    tempPath_.replace_filename(L"scalability.tmp");
}

ScalabilitySerializer::~ScalabilitySerializer()
{
    Stop();
}

void ScalabilitySerializer::Start()
{
    if (ioThread_.joinable()) {
        return;
    }
    stopRequested_.store(false, std::memory_order_release);
    ioThread_ = std::thread([this] { thread_main(); });
}

void ScalabilitySerializer::Stop()
{
    stopRequested_.store(true, std::memory_order_release);
    wakeCv_.notify_all();
    if (ioThread_.joinable()) {
        ioThread_.join();
    }
}

void ScalabilitySerializer::MarkDirty() noexcept
{
    dirtyFlag_.store(true, std::memory_order_release);
    wakeCv_.notify_one();
}

void ScalabilitySerializer::FlushIfDirty()
{
    if (!dirtyFlag_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    const SerializableSettings snapshot = snapshotProvider_();
    if (!write_snapshot(snapshot)) {
        dirtyFlag_.store(true, std::memory_order_release);
    }
}

bool ScalabilitySerializer::LoadFromDisk()
{
    bool migrated = false;
    try {
        const SerializableSettings loaded = read_json_or_default(settingsPath_, migrated);
        applySettings_(loaded);
        if (migrated) {
            dirtyFlag_.store(true, std::memory_order_release);
        }
        return !migrated;
    } catch (const std::exception& exception) {
        DeleteFileW(settingsPath_.wstring().c_str());
        SerializableSettings defaults = defaultProvider_();
        applySettings_(defaults);
        set_last_error(L"Config corrupted - reset to defaults.");
        LogSerializerError(last_error());
        write_snapshot(defaults);
        (void)exception;
        return false;
    }
}

std::wstring ScalabilitySerializer::last_error() const
{
    std::scoped_lock lock(errorMutex_);
    return lastError_;
}

void ScalabilitySerializer::thread_main()
{
    SetThreadDescription(GetCurrentThread(), L"TalalScalabilityIO");
    while (!stopRequested_.load(std::memory_order_acquire)) {
        std::unique_lock lock(wakeMutex_);
        wakeCv_.wait_for(lock, std::chrono::milliseconds(16), [this] {
            return dirtyFlag_.load(std::memory_order_acquire) || stopRequested_.load(std::memory_order_acquire);
        });
        lock.unlock();
        FlushIfDirty();
    }
    FlushIfDirty();
}

bool ScalabilitySerializer::write_snapshot(const SerializableSettings& settings)
{
    try {
        std::filesystem::create_directories(settingsPath_.parent_path());
        {
            std::ofstream file(tempPath_, std::ios::binary | std::ios::trunc);
            if (!file) {
                set_last_error(L"Failed to open scalability.tmp for writing.");
                LogSerializerError(last_error());
                return false;
            }
            file << std::setw(2) << ToJson(settings) << "\n";
            if (!file) {
                set_last_error(L"Failed to write scalability.tmp.");
                LogSerializerError(last_error());
                return false;
            }
        }

        if (MoveFileExW(tempPath_.wstring().c_str(), settingsPath_.wstring().c_str(), MOVEFILE_REPLACE_EXISTING) == FALSE) {
            std::wstringstream stream;
            stream << L"MoveFileExW failed with error " << GetLastError();
            set_last_error(stream.str());
            LogSerializerError(last_error());
            return false;
        }
        set_last_error({});
        return true;
    } catch (const std::exception&) {
        set_last_error(L"Unexpected serializer write failure.");
        LogSerializerError(last_error());
        return false;
    }
}

SerializableSettings ScalabilitySerializer::read_json_or_default(const std::filesystem::path& path, bool& migrated)
{
    SerializableSettings defaults = defaultProvider_();
    migrated = false;
    if (!std::filesystem::exists(path)) {
        migrated = true;
        return defaults;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        migrated = true;
        set_last_error(L"Config unreadable - reset to defaults.");
        return defaults;
    }

    json doc;
    file >> doc;
    if (!doc.is_object()) {
        throw std::runtime_error("scalability root is not an object");
    }

    const int version = JsonValue<int>(doc, "version", JsonValue<int>(doc, "schema_version", 1));
    if (version < 2) {
        migrated = true;
        doc["version"] = 2;
    }

    SerializableSettings settings = FromJsonWithDefaults(doc, defaults);
    settings.version = 2;
    return settings;
}

void ScalabilitySerializer::set_last_error(std::wstring message)
{
    std::scoped_lock lock(errorMutex_);
    lastError_ = std::move(message);
}

} // namespace talal::dashboard
