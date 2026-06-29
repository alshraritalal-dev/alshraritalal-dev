#pragma once

#include "DashboardController.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <optional>
#include <vector>

namespace talal::dashboard {

struct UIMetrics {
    static constexpr int RowHeight = 28;
    static constexpr int LabelWidth = 180;
    static constexpr int SliderWidth = 260;
    static constexpr int ValueWidth = 80;
    static constexpr int ColumnGap = 32;
    static constexpr int SectionPadding = 16;
    static constexpr int GroupHeaderHeight = 36;
};

class UIRenderer {
public:
    static constexpr int kAdapterComboId = 2001;
    static constexpr int kDisplayModeComboId = 2002;
    static constexpr int kFrameLimiterComboId = 2003;
    static constexpr int kSaveButtonId = 2101;
    static constexpr int kPresetButtonId = 2102;
    static constexpr int kRefreshButtonId = 2103;
    static constexpr int kHdrCheckId = 2104;
    static constexpr int kVsyncCheckId = 2105;
    static constexpr int kSliderIdBase = 3000;

    UIRenderer();
    ~UIRenderer();

    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    bool Create(HWND parent, HINSTANCE instance, UINT dpi, const DashboardViewModel& view);
    void Recreate(UINT dpi, const DashboardViewModel& view);
    void Refresh(const DashboardViewModel& view);
    std::optional<SettingKey> SliderKeyFromHwnd(HWND hwnd) const noexcept;
    LRESULT OnCtlColorStatic(HDC dc, HWND hwnd) const noexcept;

private:
    struct SliderControls {
        SettingKey key = SettingKey::RenderScale;
        HWND label = nullptr;
        HWND trackbar = nullptr;
        HWND value = nullptr;
    };

    int Scale(int value) const noexcept;
    HWND CreateChild(const wchar_t* className, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id);
    HWND CreateLabel(const wchar_t* text, int x, int y, int width, int height, int id = 0);
    HWND CreateHeader(const wchar_t* text, int x, int y, int width);
    HWND CreateButton(const wchar_t* text, int x, int y, int width, int height, int id);
    HWND CreateCheckbox(const wchar_t* text, int x, int y, int width, int height, int id);
    HWND CreateCombo(int x, int y, int width, int height, int id);
    int CreateDisplaySection(int x, int y, int width);
    int CreateSliderSection(const SectionViewModel& section, int x, int y, int width);
    void DestroyControls() noexcept;
    void ApplyFont(HWND hwnd) const noexcept;

    HWND parent_ = nullptr;
    HINSTANCE instance_ = nullptr;
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    HBRUSH headerBrush_ = nullptr;
    HBRUSH headerTextBrush_ = nullptr;
    std::vector<HWND> controls_;
    std::vector<HWND> headerControls_;
    std::vector<SliderControls> sliders_;
    HWND adapterCombo_ = nullptr;
    HWND displayModeCombo_ = nullptr;
    HWND frameLimiterCombo_ = nullptr;
    HWND hdrCheck_ = nullptr;
    HWND vsyncCheck_ = nullptr;
    HWND hardwareInfo_ = nullptr;
    HWND hdrPathInfo_ = nullptr;
    HWND statusInfo_ = nullptr;
    HWND vramProgress_ = nullptr;
    HWND vramText_ = nullptr;
};

} // namespace talal::dashboard
