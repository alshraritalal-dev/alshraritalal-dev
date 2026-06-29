#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "DashboardController.h"
#include "UIRenderer.h"
#include "renderer/renderer_mvp.h"

#include <windows.h>
#include <commctrl.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

using talal::dashboard::DashboardController;
using talal::dashboard::UIRenderer;
using talal::renderer::RendererMvp;

std::filesystem::path ModulePath()
{
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (written == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    buffer.resize(written);
    return std::filesystem::path(buffer);
}

std::filesystem::path LocateRepoRoot()
{
#ifdef TALAL_SOURCE_DIR
    const std::filesystem::path compiledSourceRoot = TALAL_SOURCE_DIR;
    if (std::filesystem::exists(compiledSourceRoot / "config" / "scalability.json")) {
        return compiledSourceRoot;
    }
#endif

    std::vector<std::filesystem::path> candidates;
    candidates.push_back(std::filesystem::current_path());
    candidates.push_back(ModulePath().parent_path());

    for (const auto& start : candidates) {
        std::filesystem::path probe = start;
        for (int i = 0; i < 8; ++i) {
            if (std::filesystem::exists(probe / "config" / "scalability.json")) {
                return probe;
            }
            if (!probe.has_parent_path()) {
                break;
            }
            probe = probe.parent_path();
        }
    }

    return std::filesystem::current_path();
}

class DashboardApp {
public:
    explicit DashboardApp(HINSTANCE instance)
        : instance_(instance)
    {
    }

    bool Create(int showCommand)
    {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        INITCOMMONCONTROLSEX commonControls = {};
        commonControls.dwSize = sizeof(commonControls);
        commonControls.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_WIN95_CLASSES;
        InitCommonControlsEx(&commonControls);

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &DashboardApp::StaticWndProc;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"TalalGameEngineDashboard";

        if (RegisterClassExW(&wc) == 0) {
            return false;
        }

        hwnd_ = CreateWindowExW(
            WS_EX_CONTROLPARENT,
            wc.lpszClassName,
            L"GameEngine - DEMO_WORKSTATION",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            ScaleForSystem(1520),
            ScaleForSystem(1010),
            nullptr,
            nullptr,
            instance_,
            this);

        if (!hwnd_) {
            return false;
        }

        ShowWindow(hwnd_, showCommand);
        UpdateWindow(hwnd_);
        return true;
    }

    int Run()
    {
        MSG msg = {};
        bool running = true;
        while (running) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != FALSE) {
                if (msg.message == WM_QUIT) {
                    running = false;
                    break;
                }
                if (!hwnd_ || !IsDialogMessageW(hwnd_, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            if (running && renderer3d_.Ready()) {
                const auto view = controller_.BuildViewModel();
                renderer3d_.SetPresentation(view.vsync, controller_.CurrentFrameLimit());
                try {
                    renderer3d_.Render(hwnd_);
                } catch (const std::exception& exception) {
                    std::filesystem::create_directories(repoRoot_ / "project_core_state");
                    std::ofstream errorFile(repoRoot_ / "project_core_state" / "renderer_phase3_error.txt", std::ios::trunc);
                    errorFile << exception.what() << "\n";
                    renderer3d_.Shutdown();
                }
            }
        }
        renderer3d_.Shutdown();
        controller_.Shutdown();
        return static_cast<int>(msg.wParam);
    }

private:
    static int ScaleForSystem(int value)
    {
        const UINT dpi = GetDpiForSystem();
        return MulDiv(value, static_cast<int>(dpi), 96);
    }

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* app = reinterpret_cast<DashboardApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = reinterpret_cast<DashboardApp*>(create->lpCreateParams);
            app->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (app) {
            return app->WndProc(message, wParam, lParam);
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT WndProc(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message) {
        case WM_CREATE:
            return OnCreate();
        case WM_COMMAND:
            OnCommand(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
            return 0;
        case WM_HSCROLL:
            OnSliderChanged(reinterpret_cast<HWND>(lParam));
            return 0;
        case WM_SIZE:
            OnSize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_CTLCOLORSTATIC:
            if (const LRESULT brush = renderer_.OnCtlColorStatic(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam))) {
                return brush;
            }
            break;
        case WM_DPICHANGED:
            OnDpiChanged(HIWORD(wParam), reinterpret_cast<RECT*>(lParam));
            return 0;
        case WM_DESTROY:
            renderer3d_.Shutdown();
            controller_.Shutdown();
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    LRESULT OnCreate()
    {
        dpi_ = GetDpiForWindow(hwnd_);
        repoRoot_ = LocateRepoRoot();
        if (!controller_.Initialize(repoRoot_)) {
            MessageBoxW(hwnd_, L"Failed to initialize dashboard controller.", L"GameEngine", MB_ICONERROR | MB_OK);
            return -1;
        }
        RECT client {};
        GetClientRect(hwnd_, &client);
        const auto view = controller_.BuildViewModel();
        if (!renderer3d_.Initialize(
                hwnd_,
                repoRoot_,
                static_cast<std::uint32_t>(std::max<LONG>(1, client.right - client.left)),
                static_cast<std::uint32_t>(std::max<LONG>(1, client.bottom - client.top)),
                view.hdrEnabled && view.hdrAllowed,
                view.vsync,
                controller_.CurrentFrameLimit())) {
            MessageBoxW(hwnd_, L"Failed to initialize DirectX 12 renderer MVP.", L"GameEngine", MB_ICONERROR | MB_OK);
            return -1;
        }
        RefreshView();
        return 0;
    }

    void OnCommand(int id, int notify, HWND control)
    {
        if (id == UIRenderer::kAdapterComboId && notify == CBN_SELCHANGE) {
            const LRESULT selection = SendMessageW(control, CB_GETCURSEL, 0, 0);
            if (selection >= 0) {
                controller_.SelectAdapter(static_cast<std::size_t>(selection));
                RefreshView();
            }
            return;
        }

        if (id == UIRenderer::kDisplayModeComboId && notify == CBN_SELCHANGE) {
            const LRESULT selection = SendMessageW(control, CB_GETCURSEL, 0, 0);
            if (selection >= 0) {
                controller_.SelectDisplayMode(static_cast<std::size_t>(selection));
                RefreshView();
            }
            return;
        }

        if (id == UIRenderer::kFrameLimiterComboId && notify == CBN_SELCHANGE) {
            const LRESULT selection = SendMessageW(control, CB_GETCURSEL, 0, 0);
            if (selection >= 0) {
                controller_.SetFrameLimitByIndex(static_cast<std::size_t>(selection));
                RefreshView();
            }
            return;
        }

        if (id == UIRenderer::kHdrCheckId) {
            controller_.SetHdrEnabled(SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED);
            RefreshView();
            return;
        }

        if (id == UIRenderer::kVsyncCheckId) {
            controller_.SetVsync(SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED);
            RefreshView();
            return;
        }

        if (id == UIRenderer::kPresetButtonId) {
            controller_.ApplyHardwarePreset();
            RefreshView();
            return;
        }

        if (id == UIRenderer::kRefreshButtonId) {
            controller_.RefreshHardware();
            RefreshView();
            return;
        }

        if (id == UIRenderer::kSaveButtonId) {
            controller_.QueueSave();
            RefreshView();
            return;
        }
    }

    void OnSliderChanged(HWND trackbar)
    {
        if (!trackbar) {
            return;
        }
        const auto key = renderer_.SliderKeyFromHwnd(trackbar);
        if (!key.has_value()) {
            return;
        }
        const int value = static_cast<int>(SendMessageW(trackbar, TBM_GETPOS, 0, 0));
        controller_.SetSliderValue(*key, value);
        RefreshView();
    }

    void OnDpiChanged(UINT dpi, const RECT* suggested)
    {
        dpi_ = dpi == 0 ? 96 : dpi;
        if (suggested) {
            SetWindowPos(
                hwnd_,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        renderer_.Recreate(dpi_, controller_.BuildViewModel());
    }

    void OnSize(UINT width, UINT height)
    {
        if (renderer3d_.Ready() && width > 0 && height > 0) {
            renderer3d_.Resize(width, height);
        }
    }

    void RefreshView()
    {
        const auto view = controller_.BuildViewModel();
        if (!rendererCreated_) {
            renderer_.Create(hwnd_, instance_, dpi_, view);
            rendererCreated_ = true;
        } else {
            renderer_.Refresh(view);
        }
        SetWindowTextW(hwnd_, view.titleText.c_str());
    }

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    bool rendererCreated_ = false;
    std::filesystem::path repoRoot_;
    DashboardController controller_;
    UIRenderer renderer_;
    RendererMvp renderer3d_;
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previousInstance, PWSTR commandLine, int showCommand)
{
    UNREFERENCED_PARAMETER(previousInstance);
    UNREFERENCED_PARAMETER(commandLine);

    DashboardApp app(instance);
    if (!app.Create(showCommand)) {
        MessageBoxW(nullptr, L"Failed to initialize GameEngine dashboard.", L"GameEngine", MB_ICONERROR | MB_OK);
        return -1;
    }
    return app.Run();
}
