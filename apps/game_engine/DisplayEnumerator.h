#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_6.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace talal::dashboard {

void LogError(std::wstring_view message, HRESULT hr) noexcept;
void LogWarning(std::wstring_view message) noexcept;

#define HR_CHECK(hr, msg) \
    do { \
        const HRESULT talal_hr_check_value = (hr); \
        if (FAILED(talal_hr_check_value)) { \
            ::talal::dashboard::LogError((msg), talal_hr_check_value); \
        } \
    } while (false)

struct DisplayMode {
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    std::uint32_t refreshNumerator = 60;
    std::uint32_t refreshDenominator = 1;
    DXGI_MODE_SCALING scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    DXGI_MODE_SCANLINE_ORDER scanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;

    std::uint32_t refresh_hz() const noexcept;
};

std::vector<DisplayMode> EnumerateSupportedModes(IDXGIOutput* output);
DisplayMode FallbackDisplayMode() noexcept;
std::wstring FormatDisplayMode(const DisplayMode& mode);
bool QueryHdr10Capability(IDXGIOutput* output) noexcept;
bool TryGetOutputDescription(IDXGIOutput* output, DXGI_OUTPUT_DESC& description) noexcept;
DXGI_COLOR_SPACE_TYPE QueryOutputColorSpace(IDXGIOutput* output) noexcept;

} // namespace talal::dashboard
