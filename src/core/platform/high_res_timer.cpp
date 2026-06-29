#include "core/platform/high_res_timer.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace talal::core {

namespace {

std::int64_t QueryCounter() noexcept
{
    LARGE_INTEGER value = {};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

std::int64_t QueryFrequency() noexcept
{
    static const std::int64_t frequency = [] {
        LARGE_INTEGER value = {};
        QueryPerformanceFrequency(&value);
        return value.QuadPart;
    }();
    return frequency;
}

} // namespace

HighResTimer::HighResTimer()
{
    reset();
}

void HighResTimer::reset() noexcept
{
    start_ = QueryCounter();
}

double HighResTimer::seconds() const noexcept
{
    return static_cast<double>(QueryCounter() - start_) * seconds_per_tick();
}

double HighResTimer::milliseconds() const noexcept
{
    return seconds() * 1000.0;
}

std::uint64_t HighResTimer::ticks() const noexcept
{
    return static_cast<std::uint64_t>(QueryCounter() - start_);
}

double HighResTimer::seconds_per_tick() noexcept
{
    return 1.0 / static_cast<double>(QueryFrequency());
}

} // namespace talal::core
