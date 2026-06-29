#include "DisplayEnumerator.h"

#include <wrl/client.h>

#include <algorithm>
#include <map>
#include <sstream>

using Microsoft::WRL::ComPtr;

namespace talal::dashboard {
namespace {

constexpr UINT kModeFlags = DXGI_ENUM_MODES_INTERLACED | DXGI_ENUM_MODES_SCALING;

std::wstring HResultText(HRESULT hr)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(hr),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstring result;
    if (length > 0 && buffer) {
        result.assign(buffer, length);
        while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) {
            result.pop_back();
        }
    } else {
        std::wstringstream stream;
        stream << L"HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        result = stream.str();
    }

    if (buffer) {
        LocalFree(buffer);
    }
    return result;
}

int ScalingRank(DXGI_MODE_SCALING scaling) noexcept
{
    switch (scaling) {
    case DXGI_MODE_SCALING_CENTERED:
        return 3;
    case DXGI_MODE_SCALING_UNSPECIFIED:
        return 2;
    case DXGI_MODE_SCALING_STRETCHED:
        return 1;
    default:
        return 0;
    }
}

int ScanlineRank(DXGI_MODE_SCANLINE_ORDER ordering) noexcept
{
    switch (ordering) {
    case DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE:
        return 3;
    case DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED:
        return 2;
    default:
        return 1;
    }
}

bool IsBetterDuplicate(const DisplayMode& candidate, const DisplayMode& existing) noexcept
{
    if (ScalingRank(candidate.scaling) != ScalingRank(existing.scaling)) {
        return ScalingRank(candidate.scaling) > ScalingRank(existing.scaling);
    }
    if (ScanlineRank(candidate.scanlineOrdering) != ScanlineRank(existing.scanlineOrdering)) {
        return ScanlineRank(candidate.scanlineOrdering) > ScanlineRank(existing.scanlineOrdering);
    }
    const std::uint64_t candidateNumerator =
        static_cast<std::uint64_t>(candidate.refreshNumerator) * std::max<std::uint32_t>(1, existing.refreshDenominator);
    const std::uint64_t existingNumerator =
        static_cast<std::uint64_t>(existing.refreshNumerator) * std::max<std::uint32_t>(1, candidate.refreshDenominator);
    return candidateNumerator > existingNumerator;
}

bool SameResolutionRefresh(const DisplayMode& left, const DisplayMode& right) noexcept
{
    return left.width == right.width &&
        left.height == right.height &&
        left.refresh_hz() == right.refresh_hz();
}

bool IsHdrColorSpace(DXGI_COLOR_SPACE_TYPE colorSpace) noexcept
{
    return colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
        colorSpace == DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020;
}

} // namespace

void LogError(std::wstring_view message, HRESULT hr) noexcept
{
    try {
        std::wstring line = L"[DEMO_WORKSTATION][DXGI] ";
        line.append(message);
        line.append(L" failed: ");
        line.append(HResultText(hr));
        line.append(L"\n");
        OutputDebugStringW(line.c_str());
    } catch (...) {
        OutputDebugStringW(L"[DEMO_WORKSTATION][DXGI] error logging failed\n");
    }
}

void LogWarning(std::wstring_view message) noexcept
{
    try {
        std::wstring line = L"[DEMO_WORKSTATION][Warning] ";
        line.append(message);
        line.append(L"\n");
        OutputDebugStringW(line.c_str());
    } catch (...) {
        OutputDebugStringW(L"[DEMO_WORKSTATION][Warning] logging failed\n");
    }
}

std::uint32_t DisplayMode::refresh_hz() const noexcept
{
    const std::uint32_t denominator = refreshDenominator == 0 ? 1 : refreshDenominator;
    return (refreshNumerator + denominator / 2u) / denominator;
}

std::vector<DisplayMode> EnumerateSupportedModes(IDXGIOutput* output)
{
    if (!output) {
        LogError(L"EnumerateSupportedModes null IDXGIOutput", E_POINTER);
        return {};
    }

    UINT modeCount = 0;
    HRESULT hr = output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, kModeFlags, &modeCount, nullptr);
    HR_CHECK(hr, L"IDXGIOutput::GetDisplayModeList count");
    if (FAILED(hr) || modeCount == 0) {
        return {};
    }

    std::vector<DXGI_MODE_DESC> modeDescs(modeCount);
    hr = output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, kModeFlags, &modeCount, modeDescs.data());
    HR_CHECK(hr, L"IDXGIOutput::GetDisplayModeList data");
    if (FAILED(hr)) {
        return {};
    }

    std::vector<DisplayMode> modes;
    modes.reserve(modeCount);
    for (UINT i = 0; i < modeCount; ++i) {
        const DXGI_MODE_DESC& desc = modeDescs[i];
        DisplayMode candidate;
        candidate.width = desc.Width;
        candidate.height = desc.Height;
        candidate.refreshNumerator = desc.RefreshRate.Numerator;
        candidate.refreshDenominator = desc.RefreshRate.Denominator == 0 ? 1 : desc.RefreshRate.Denominator;
        candidate.scaling = desc.Scaling;
        candidate.scanlineOrdering = desc.ScanlineOrdering;

        auto duplicate = std::find_if(modes.begin(), modes.end(), [&](const DisplayMode& mode) {
            return SameResolutionRefresh(mode, candidate);
        });
        if (duplicate == modes.end()) {
            modes.push_back(candidate);
        } else if (IsBetterDuplicate(candidate, *duplicate)) {
            *duplicate = candidate;
        }
    }

    std::sort(modes.begin(), modes.end(), [](const DisplayMode& left, const DisplayMode& right) {
        const std::uint64_t leftPixels = static_cast<std::uint64_t>(left.width) * left.height;
        const std::uint64_t rightPixels = static_cast<std::uint64_t>(right.width) * right.height;
        if (leftPixels != rightPixels) {
            return leftPixels > rightPixels;
        }
        const std::uint64_t leftRefresh =
            static_cast<std::uint64_t>(left.refreshNumerator) * std::max<std::uint32_t>(1, right.refreshDenominator);
        const std::uint64_t rightRefresh =
            static_cast<std::uint64_t>(right.refreshNumerator) * std::max<std::uint32_t>(1, left.refreshDenominator);
        return leftRefresh > rightRefresh;
    });

    return modes;
}

DisplayMode FallbackDisplayMode() noexcept
{
    return {};
}

std::wstring FormatDisplayMode(const DisplayMode& mode)
{
    std::wstringstream stream;
    stream << mode.width << L"x" << mode.height << L" @ " << mode.refresh_hz() << L"Hz";
    return stream.str();
}

bool QueryHdr10Capability(IDXGIOutput* output) noexcept
{
    if (!output) {
        LogError(L"QueryHdr10Capability null IDXGIOutput", E_POINTER);
        return false;
    }

    ComPtr<IDXGIOutput6> output6;
    HRESULT hr = output->QueryInterface(IID_PPV_ARGS(&output6));
    if (FAILED(hr)) {
        if (hr != E_NOINTERFACE) {
            HR_CHECK(hr, L"IDXGIOutput::QueryInterface IDXGIOutput6");
        }
        return false;
    }

    DXGI_OUTPUT_DESC1 desc = {};
    hr = output6->GetDesc1(&desc);
    HR_CHECK(hr, L"IDXGIOutput6::GetDesc1");
    if (FAILED(hr)) {
        return false;
    }

    return IsHdrColorSpace(desc.ColorSpace) || desc.BitsPerColor >= 10;
}

bool TryGetOutputDescription(IDXGIOutput* output, DXGI_OUTPUT_DESC& description) noexcept
{
    description = {};
    if (!output) {
        LogError(L"TryGetOutputDescription null IDXGIOutput", E_POINTER);
        return false;
    }
    const HRESULT hr = output->GetDesc(&description);
    HR_CHECK(hr, L"IDXGIOutput::GetDesc");
    return SUCCEEDED(hr);
}

DXGI_COLOR_SPACE_TYPE QueryOutputColorSpace(IDXGIOutput* output) noexcept
{
    if (!output) {
        LogError(L"QueryOutputColorSpace null IDXGIOutput", E_POINTER);
        return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    }

    ComPtr<IDXGIOutput6> output6;
    HRESULT hr = output->QueryInterface(IID_PPV_ARGS(&output6));
    if (FAILED(hr)) {
        if (hr != E_NOINTERFACE) {
            HR_CHECK(hr, L"IDXGIOutput::QueryInterface IDXGIOutput6 for color space");
        }
        return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    }

    DXGI_OUTPUT_DESC1 desc = {};
    hr = output6->GetDesc1(&desc);
    HR_CHECK(hr, L"IDXGIOutput6::GetDesc1 color space");
    if (FAILED(hr)) {
        return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    }
    return desc.ColorSpace;
}

} // namespace talal::dashboard
