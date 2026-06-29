#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace talal::renderer {

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t kFramesInFlight = 3;

class Dx12Exception final : public std::runtime_error {
public:
    Dx12Exception(std::string message, HRESULT hr);
    HRESULT result() const noexcept { return hr_; }

private:
    HRESULT hr_ = E_FAIL;
};

std::wstring HResultToString(HRESULT hr);
std::string Narrow(std::wstring_view text);
std::wstring Widen(std::string_view text);
void LogError(std::wstring_view message, HRESULT hr) noexcept;
void LogInfo(std::wstring_view message) noexcept;
void CheckHResult(HRESULT hr, std::wstring_view message);

} // namespace talal::renderer

#ifndef HR_CHECK
#define HR_CHECK(hr, msg) ::talal::renderer::CheckHResult((hr), (msg))
#endif
