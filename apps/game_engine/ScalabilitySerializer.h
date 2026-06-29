#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace talal::dashboard {

struct SerializableSettings {
    int version = 2;
    std::string machine = "DEMO_WORKSTATION";
    std::string profile = "RTX 4070 12GB - High Refresh Ultra Raster";
    std::string backend = "dx12";
    std::string adapterName;
    std::string displayName;
    std::string colorSpace;
    std::uint64_t dedicatedVramMb = 0;
    int displayWidth = 1920;
    int displayHeight = 1080;
    int refreshHz = 60;
    int frameLimit = 240;
    bool hdrEnabled = true;
    bool hdrDetected = false;
    bool vsync = false;
    int renderScalePercent = 100;
    int texturePoolMb = 7680;
    int anisotropy = 16;
    int shadowQuality = 4;
    int shadowDistancePercent = 85;
    int shadowCascades = 4;
    int ambientOcclusionQuality = 3;
    int globalIlluminationQuality = 3;
    int reflectionQuality = 3;
    int volumetricFogQuality = 3;
    int foliageDensityPercent = 100;
    int viewDistancePercent = 85;
    int geometryDetailPercent = 90;
    int terrainQualityPercent = 90;
    int waterQualityPercent = 85;
    int trafficDensityPercent = 80;
    int crowdDensityPercent = 70;
    int particleQualityPercent = 85;
    int textureStreamingAggressiveness = 85;
    int rayTracingMode = 0;
    int rayTracingQuality = 0;
    int rayTracingReflections = 0;
    std::uint64_t usableVramBudgetMb = 0;
    std::uint64_t estimatedTotalMb = 0;
    std::int64_t estimatedHeadroomMb = 0;
};

class ScalabilitySerializer {
public:
    using SnapshotProvider = std::function<SerializableSettings()>;
    using ApplySettings = std::function<void(const SerializableSettings&)>;
    using DefaultProvider = std::function<SerializableSettings()>;

    ScalabilitySerializer(
        std::filesystem::path settingsPath,
        SnapshotProvider snapshotProvider,
        ApplySettings applySettings,
        DefaultProvider defaultProvider);
    ~ScalabilitySerializer();

    ScalabilitySerializer(const ScalabilitySerializer&) = delete;
    ScalabilitySerializer& operator=(const ScalabilitySerializer&) = delete;

    void Start();
    void Stop();
    void MarkDirty() noexcept;
    void FlushIfDirty();
    bool LoadFromDisk();
    std::wstring last_error() const;

private:
    void thread_main();
    bool write_snapshot(const SerializableSettings& settings);
    SerializableSettings read_json_or_default(const std::filesystem::path& path, bool& migrated);
    void set_last_error(std::wstring message);

    std::filesystem::path settingsPath_;
    std::filesystem::path tempPath_;
    SnapshotProvider snapshotProvider_;
    ApplySettings applySettings_;
    DefaultProvider defaultProvider_;
    std::atomic<bool> dirtyFlag_ = false;
    std::atomic<bool> stopRequested_ = false;
    std::thread ioThread_;
    std::condition_variable wakeCv_;
    std::mutex wakeMutex_;
    mutable std::mutex errorMutex_;
    std::wstring lastError_;
};

} // namespace talal::dashboard
