#pragma once

#include <cstdint>

namespace talal::core {

class HighResTimer {
public:
    HighResTimer();

    void reset() noexcept;
    double seconds() const noexcept;
    double milliseconds() const noexcept;
    std::uint64_t ticks() const noexcept;

    static double seconds_per_tick() noexcept;

private:
    std::int64_t start_ = 0;
};

} // namespace talal::core
