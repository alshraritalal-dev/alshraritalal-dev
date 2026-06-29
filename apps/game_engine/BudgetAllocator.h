#pragma once

#include <cstdint>
#include <shared_mutex>
#include <string>

namespace talal::dashboard {

struct GraphicsBudgetSettings {
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
    bool hdrEnabled = true;
};

struct BudgetCeilings {
    std::uint64_t dedicatedVramBytes = 0;
    std::uint64_t usableBytes = 0;
    std::uint64_t texturePoolBytes = 0;
    std::uint64_t shadowsBytes = 0;
    std::uint64_t worldGeometryBytes = 0;
    std::uint64_t postProcessBytes = 0;
    std::uint64_t rayTracingReserveBytes = 0;
    std::uint64_t transientBytes = 0;
    std::uint64_t safeHeadroomBytes = 0;
};

struct BudgetEstimate {
    std::uint64_t dedicatedVramBytes = 0;
    std::uint64_t usableBytes = 0;
    std::uint64_t texturePoolBytes = 0;
    std::uint64_t shadowsBytes = 0;
    std::uint64_t worldGeometryBytes = 0;
    std::uint64_t postProcessBytes = 0;
    std::uint64_t rayTracingReserveBytes = 0;
    std::uint64_t transientBytes = 0;
    std::uint64_t totalBytes = 0;
    std::uint64_t safeHeadroomBytes = 0;

    bool within_budget() const noexcept;
    std::int64_t headroom_bytes() const noexcept;
    std::int64_t headroom_mb() const noexcept;
    std::uint32_t used_percent() const noexcept;
    std::uint32_t headroom_percent() const noexcept;
};

class SafeBudgetAllocator {
public:
    static constexpr std::uint64_t kFallbackVramMb = 4096;
    static constexpr std::uint64_t kOneMb = 1024ull * 1024ull;

    explicit SafeBudgetAllocator(std::uint64_t dedicatedVramBytes = kFallbackVramMb * kOneMb);

    void set_total_vram_bytes(std::uint64_t dedicatedVramBytes);
    BudgetCeilings ceilings() const;
    BudgetEstimate estimate(const GraphicsBudgetSettings& settings) const;
    bool can_accept(const GraphicsBudgetSettings& settings) const;
    GraphicsBudgetSettings clamp_to_safe_budget(const GraphicsBudgetSettings& settings) const;
    std::wstring warning() const;

    static std::uint64_t mb_to_bytes(std::uint64_t valueMb) noexcept;
    static std::uint64_t bytes_to_mb(std::uint64_t valueBytes) noexcept;

private:
    static BudgetCeilings compute_ceilings(std::uint64_t dedicatedVramBytes) noexcept;
    static BudgetEstimate estimate_unlocked(const BudgetCeilings& ceilings, const GraphicsBudgetSettings& settings) noexcept;
    static void clamp_category_ceilings(GraphicsBudgetSettings& settings, const BudgetCeilings& ceilings) noexcept;
    static bool reduce_one_step(GraphicsBudgetSettings& settings) noexcept;

    mutable std::shared_mutex mutex_;
    std::uint64_t dedicatedVramBytes_ = kFallbackVramMb * kOneMb;
    BudgetCeilings ceilings_ {};
    std::wstring warning_;
};

} // namespace talal::dashboard
