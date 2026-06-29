#include "BudgetAllocator.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>

namespace talal::dashboard {
namespace {

void LogBudgetWarning(const wchar_t* message) noexcept
{
    OutputDebugStringW(L"[DEMO_WORKSTATION][Budget] ");
    OutputDebugStringW(message);
    OutputDebugStringW(L"\n");
}

int ClampInt(int value, int minValue, int maxValue) noexcept
{
    return std::min(std::max(value, minValue), maxValue);
}

std::uint64_t PercentOf(std::uint64_t bytes, std::uint64_t percent) noexcept
{
    return (bytes / 100ull) * percent + ((bytes % 100ull) * percent) / 100ull;
}

std::uint64_t Mb(int value) noexcept
{
    return SafeBudgetAllocator::mb_to_bytes(static_cast<std::uint64_t>(std::max(value, 0)));
}

bool ReduceBy(int& value, int floor, int step) noexcept
{
    if (value <= floor) {
        return false;
    }
    value = std::max(floor, value - step);
    return true;
}

bool ReduceQuality(int& value) noexcept
{
    return ReduceBy(value, 0, 1);
}

bool ReducePercent(int& value, int floor) noexcept
{
    return ReduceBy(value, floor, 5);
}

} // namespace

bool BudgetEstimate::within_budget() const noexcept
{
    return totalBytes <= usableBytes;
}

std::int64_t BudgetEstimate::headroom_bytes() const noexcept
{
    if (usableBytes >= totalBytes) {
        return static_cast<std::int64_t>(usableBytes - totalBytes);
    }
    return -static_cast<std::int64_t>(totalBytes - usableBytes);
}

std::int64_t BudgetEstimate::headroom_mb() const noexcept
{
    const std::int64_t bytes = headroom_bytes();
    if (bytes >= 0) {
        return static_cast<std::int64_t>(SafeBudgetAllocator::bytes_to_mb(static_cast<std::uint64_t>(bytes)));
    }
    return -static_cast<std::int64_t>(SafeBudgetAllocator::bytes_to_mb(static_cast<std::uint64_t>(-bytes)));
}

std::uint32_t BudgetEstimate::used_percent() const noexcept
{
    if (usableBytes == 0) {
        return 100;
    }
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(100, (totalBytes * 100ull) / usableBytes));
}

std::uint32_t BudgetEstimate::headroom_percent() const noexcept
{
    if (usableBytes == 0 || totalBytes >= usableBytes) {
        return 0;
    }
    return static_cast<std::uint32_t>(((usableBytes - totalBytes) * 100ull) / usableBytes);
}

SafeBudgetAllocator::SafeBudgetAllocator(std::uint64_t dedicatedVramBytes)
{
    set_total_vram_bytes(dedicatedVramBytes);
}

void SafeBudgetAllocator::set_total_vram_bytes(std::uint64_t dedicatedVramBytes)
{
    std::unique_lock lock(mutex_);
    warning_.clear();
    if (dedicatedVramBytes == 0) {
        dedicatedVramBytes = kFallbackVramMb * kOneMb;
        warning_ = L"DXGI returned 0 dedicated VRAM; using conservative 4096 MB budget.";
        LogBudgetWarning(warning_.c_str());
    }
    dedicatedVramBytes_ = dedicatedVramBytes;
    ceilings_ = compute_ceilings(dedicatedVramBytes_);
}

BudgetCeilings SafeBudgetAllocator::ceilings() const
{
    std::shared_lock lock(mutex_);
    return ceilings_;
}

BudgetEstimate SafeBudgetAllocator::estimate(const GraphicsBudgetSettings& settings) const
{
    std::shared_lock lock(mutex_);
    return estimate_unlocked(ceilings_, settings);
}

bool SafeBudgetAllocator::can_accept(const GraphicsBudgetSettings& settings) const
{
    std::shared_lock lock(mutex_);
    return estimate_unlocked(ceilings_, settings).within_budget();
}

GraphicsBudgetSettings SafeBudgetAllocator::clamp_to_safe_budget(const GraphicsBudgetSettings& settings) const
{
    std::shared_lock lock(mutex_);
    GraphicsBudgetSettings clamped = settings;
    clamp_category_ceilings(clamped, ceilings_);

    for (int guard = 0; guard < 512 && !estimate_unlocked(ceilings_, clamped).within_budget(); ++guard) {
        if (!reduce_one_step(clamped)) {
            break;
        }
    }

    return clamped;
}

std::wstring SafeBudgetAllocator::warning() const
{
    std::shared_lock lock(mutex_);
    return warning_;
}

std::uint64_t SafeBudgetAllocator::mb_to_bytes(std::uint64_t valueMb) noexcept
{
    return valueMb * kOneMb;
}

std::uint64_t SafeBudgetAllocator::bytes_to_mb(std::uint64_t valueBytes) noexcept
{
    return valueBytes / kOneMb;
}

BudgetCeilings SafeBudgetAllocator::compute_ceilings(std::uint64_t dedicatedVramBytes) noexcept
{
    BudgetCeilings ceilings;
    ceilings.dedicatedVramBytes = dedicatedVramBytes;
    ceilings.texturePoolBytes = PercentOf(dedicatedVramBytes, 55);
    ceilings.shadowsBytes = PercentOf(dedicatedVramBytes, 10);
    ceilings.worldGeometryBytes = PercentOf(dedicatedVramBytes, 12);
    ceilings.postProcessBytes = PercentOf(dedicatedVramBytes, 5);
    ceilings.rayTracingReserveBytes = PercentOf(dedicatedVramBytes, 8);
    ceilings.transientBytes = PercentOf(dedicatedVramBytes, 5);
    ceilings.safeHeadroomBytes = PercentOf(dedicatedVramBytes, 5);
    ceilings.usableBytes = dedicatedVramBytes - ceilings.safeHeadroomBytes;
    return ceilings;
}

BudgetEstimate SafeBudgetAllocator::estimate_unlocked(const BudgetCeilings& ceilings, const GraphicsBudgetSettings& settings) noexcept
{
    const int renderScale = ClampInt(settings.renderScalePercent, 50, 150);
    const std::uint64_t renderScaleSquared = static_cast<std::uint64_t>(renderScale) * static_cast<std::uint64_t>(renderScale);
    const std::uint64_t scaledBackbufferMb = (384ull * renderScaleSquared) / 10000ull;
    const std::uint64_t hdrTargetMb = settings.hdrEnabled ? 128ull : 64ull;

    BudgetEstimate estimate;
    estimate.dedicatedVramBytes = ceilings.dedicatedVramBytes;
    estimate.usableBytes = ceilings.usableBytes;
    estimate.safeHeadroomBytes = ceilings.safeHeadroomBytes;
    estimate.texturePoolBytes = Mb(ClampInt(settings.texturePoolMb, 512, 24576));
    estimate.shadowsBytes = Mb(
        192 +
        ClampInt(settings.shadowQuality, 0, 4) * 64 +
        ClampInt(settings.shadowCascades, 1, 4) * 32 +
        ClampInt(settings.shadowDistancePercent, 25, 100));
    estimate.worldGeometryBytes = Mb(
        256 +
        ClampInt(settings.viewDistancePercent, 25, 100) * 2 +
        ClampInt(settings.geometryDetailPercent, 25, 100) * 2 +
        ClampInt(settings.terrainQualityPercent, 25, 100) +
        ClampInt(settings.foliageDensityPercent, 0, 100) +
        ClampInt(settings.trafficDensityPercent, 0, 100) / 2 +
        ClampInt(settings.crowdDensityPercent, 0, 100) / 2);
    estimate.postProcessBytes = Mb(
        static_cast<int>(scaledBackbufferMb + hdrTargetMb) +
        192 +
        ClampInt(settings.ambientOcclusionQuality, 0, 4) * 48 +
        ClampInt(settings.globalIlluminationQuality, 0, 4) * 64 +
        ClampInt(settings.reflectionQuality, 0, 4) * 64 +
        ClampInt(settings.volumetricFogQuality, 0, 4) * 32 +
        ClampInt(settings.waterQualityPercent, 25, 100) / 2 +
        ClampInt(settings.particleQualityPercent, 0, 100) / 2);
    estimate.rayTracingReserveBytes = Mb(
        ClampInt(settings.rayTracingMode, 0, 3) == 0
            ? 0
            : ClampInt(settings.rayTracingMode, 0, 3) * 512 +
                ClampInt(settings.rayTracingQuality, 0, 3) * 384 +
                ClampInt(settings.rayTracingReflections, 0, 3) * 256);
    estimate.transientBytes = Mb(1536 + (100 - ClampInt(settings.textureStreamingAggressiveness, 0, 100)) * 8);
    estimate.totalBytes =
        estimate.texturePoolBytes +
        estimate.shadowsBytes +
        estimate.worldGeometryBytes +
        estimate.postProcessBytes +
        estimate.rayTracingReserveBytes +
        estimate.transientBytes;
    return estimate;
}

void SafeBudgetAllocator::clamp_category_ceilings(GraphicsBudgetSettings& settings, const BudgetCeilings& ceilings) noexcept
{
    const auto textureCeilingMb = static_cast<int>(bytes_to_mb(ceilings.texturePoolBytes));
    settings.texturePoolMb = ClampInt(settings.texturePoolMb, 512, std::max(512, textureCeilingMb));

    for (int guard = 0; guard < 128 && estimate_unlocked(ceilings, settings).shadowsBytes > ceilings.shadowsBytes; ++guard) {
        if (ReducePercent(settings.shadowDistancePercent, 25)) {
            continue;
        }
        if (ReduceQuality(settings.shadowQuality)) {
            continue;
        }
        if (ReduceBy(settings.shadowCascades, 1, 1)) {
            continue;
        }
        break;
    }

    for (int guard = 0; guard < 256 && estimate_unlocked(ceilings, settings).worldGeometryBytes > ceilings.worldGeometryBytes; ++guard) {
        if (ReducePercent(settings.viewDistancePercent, 25)) {
            continue;
        }
        if (ReducePercent(settings.geometryDetailPercent, 25)) {
            continue;
        }
        if (ReducePercent(settings.terrainQualityPercent, 25)) {
            continue;
        }
        if (ReducePercent(settings.foliageDensityPercent, 0)) {
            continue;
        }
        if (ReducePercent(settings.trafficDensityPercent, 0)) {
            continue;
        }
        if (ReducePercent(settings.crowdDensityPercent, 0)) {
            continue;
        }
        break;
    }

    for (int guard = 0; guard < 256 && estimate_unlocked(ceilings, settings).postProcessBytes > ceilings.postProcessBytes; ++guard) {
        if (ReduceQuality(settings.globalIlluminationQuality)) {
            continue;
        }
        if (ReduceQuality(settings.reflectionQuality)) {
            continue;
        }
        if (ReduceQuality(settings.ambientOcclusionQuality)) {
            continue;
        }
        if (ReduceQuality(settings.volumetricFogQuality)) {
            continue;
        }
        if (ReducePercent(settings.waterQualityPercent, 25)) {
            continue;
        }
        if (ReducePercent(settings.particleQualityPercent, 0)) {
            continue;
        }
        if (ReducePercent(settings.renderScalePercent, 50)) {
            continue;
        }
        break;
    }

    for (int guard = 0; guard < 32 && estimate_unlocked(ceilings, settings).rayTracingReserveBytes > ceilings.rayTracingReserveBytes; ++guard) {
        if (ReduceQuality(settings.rayTracingReflections)) {
            continue;
        }
        if (ReduceQuality(settings.rayTracingQuality)) {
            continue;
        }
        if (ReduceQuality(settings.rayTracingMode)) {
            continue;
        }
        break;
    }

    while (estimate_unlocked(ceilings, settings).transientBytes > ceilings.transientBytes &&
           settings.textureStreamingAggressiveness < 100) {
        settings.textureStreamingAggressiveness = std::min(100, settings.textureStreamingAggressiveness + 5);
    }
}

bool SafeBudgetAllocator::reduce_one_step(GraphicsBudgetSettings& settings) noexcept
{
    if (ReduceBy(settings.texturePoolMb, 512, 256)) {
        return true;
    }
    if (ReduceQuality(settings.rayTracingReflections)) {
        return true;
    }
    if (ReduceQuality(settings.rayTracingQuality)) {
        return true;
    }
    if (ReduceQuality(settings.rayTracingMode)) {
        return true;
    }
    if (ReducePercent(settings.viewDistancePercent, 25)) {
        return true;
    }
    if (ReducePercent(settings.geometryDetailPercent, 25)) {
        return true;
    }
    if (ReducePercent(settings.shadowDistancePercent, 25)) {
        return true;
    }
    if (ReduceQuality(settings.shadowQuality)) {
        return true;
    }
    if (ReduceQuality(settings.globalIlluminationQuality)) {
        return true;
    }
    if (ReduceQuality(settings.reflectionQuality)) {
        return true;
    }
    if (ReducePercent(settings.renderScalePercent, 50)) {
        return true;
    }
    if (settings.textureStreamingAggressiveness < 100) {
        settings.textureStreamingAggressiveness = std::min(100, settings.textureStreamingAggressiveness + 5);
        return true;
    }
    return false;
}

} // namespace talal::dashboard
