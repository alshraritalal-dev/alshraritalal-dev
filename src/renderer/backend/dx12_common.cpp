#include "renderer/backend/dx12_common.h"

#include <sstream>

namespace talal::renderer {

Dx12Exception::Dx12Exception(std::string message, HRESULT hr)
    : std::runtime_error(std::move(message))
    , hr_(hr)
{
}

std::wstring HResultToString(HRESULT hr)
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

    std::wstring message;
    if (length > 0 && buffer) {
        message.assign(buffer, length);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
            message.pop_back();
        }
    } else {
        std::wstringstream stream;
        stream << L"HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        message = stream.str();
    }

    if (buffer) {
        LocalFree(buffer);
    }
    return message;
}

std::string Narrow(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count, nullptr, nullptr);
    return result;
}

std::wstring Widen(std::string_view text)
{
    if (text.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}

void LogError(std::wstring_view message, HRESULT hr) noexcept
{
    try {
        std::wstring line = L"[DEMO_WORKSTATION][DX12] ";
        line.append(message);
        line.append(L": ");
        line.append(HResultToString(hr));
        line.append(L"\n");
        OutputDebugStringW(line.c_str());
    } catch (...) {
        OutputDebugStringW(L"[DEMO_WORKSTATION][DX12] error logging failed\n");
    }
}

void LogInfo(std::wstring_view message) noexcept
{
    try {
        std::wstring line = L"[DEMO_WORKSTATION][DX12] ";
        line.append(message);
        line.append(L"\n");
        OutputDebugStringW(line.c_str());
    } catch (...) {
        OutputDebugStringW(L"[DEMO_WORKSTATION][DX12] info logging failed\n");
    }
}

void CheckHResult(HRESULT hr, std::wstring_view message)
{
    if (FAILED(hr)) {
        LogError(message, hr);
        std::wstring wide = std::wstring(message) + L": " + HResultToString(hr);
        throw Dx12Exception(Narrow(wide), hr);
    }
}

} // namespace talal::renderer
