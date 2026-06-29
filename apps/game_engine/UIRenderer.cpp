#include "UIRenderer.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>

namespace talal::dashboard {
namespace {

bool IsNamedSection(const SectionViewModel& section, const wchar_t* name) noexcept
{
    return section.name == name;
}

COLORREF BarColor(BudgetSeverity severity) noexcept
{
    switch (severity) {
    case BudgetSeverity::Green:
        return RGB(34, 139, 69);
    case BudgetSeverity::Yellow:
        return RGB(212, 164, 36);
    case BudgetSeverity::Red:
        return RGB(190, 45, 45);
    default:
        return RGB(34, 139, 69);
    }
}

} // namespace

UIRenderer::UIRenderer()
{
    headerBrush_ = CreateSolidBrush(RGB(38, 74, 105));
    headerTextBrush_ = CreateSolidBrush(RGB(38, 74, 105));
}

UIRenderer::~UIRenderer()
{
    DestroyControls();
    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    if (headerBrush_) {
        DeleteObject(headerBrush_);
        headerBrush_ = nullptr;
    }
    if (headerTextBrush_) {
        DeleteObject(headerTextBrush_);
        headerTextBrush_ = nullptr;
    }
}

bool UIRenderer::Create(HWND parent, HINSTANCE instance, UINT dpi, const DashboardViewModel& view)
{
    parent_ = parent;
    instance_ = instance;
    dpi_ = dpi == 0 ? 96 : dpi;

    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    font_ = CreateFontW(
        -MulDiv(10, static_cast<int>(dpi_), 72),
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");

    constexpr int margin = 16;
    constexpr int columnWidth = UIMetrics::LabelWidth + UIMetrics::SliderWidth + UIMetrics::ValueWidth + UIMetrics::SectionPadding * 2;
    const int leftX = margin;
    const int rightX = margin + columnWidth + UIMetrics::ColumnGap;
    const int infoX = rightX + columnWidth + UIMetrics::ColumnGap;
    int leftY = margin;
    int rightY = margin;

    leftY = CreateDisplaySection(leftX, leftY, columnWidth) + UIMetrics::SectionPadding;

    for (const SectionViewModel& section : view.sections) {
        if (IsNamedSection(section, L"RENDERING") ||
            IsNamedSection(section, L"SHADOWS") ||
            IsNamedSection(section, L"GEOMETRY")) {
            leftY = CreateSliderSection(section, leftX, leftY, columnWidth) + UIMetrics::SectionPadding;
        } else {
            rightY = CreateSliderSection(section, rightX, rightY, columnWidth) + UIMetrics::SectionPadding;
        }
    }

    CreateHeader(L"SYSTEM", infoX, margin, 340);
    hardwareInfo_ = CreateChild(
        L"EDIT",
        L"",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_BORDER,
        infoX,
        margin + UIMetrics::GroupHeaderHeight,
        340,
        580,
        0);
    hdrPathInfo_ = CreateChild(
        L"STATIC",
        L"",
        SS_LEFT | SS_NOPREFIX,
        infoX,
        margin + UIMetrics::GroupHeaderHeight + 592,
        340,
        78,
        0);
    statusInfo_ = CreateChild(
        L"STATIC",
        L"",
        SS_LEFT | SS_NOPREFIX,
        margin,
        884,
        1080,
        24,
        0);
    vramProgress_ = CreateChild(
        PROGRESS_CLASSW,
        L"",
        PBS_SMOOTH,
        margin,
        914,
        1460,
        22,
        0);
    SendMessageW(vramProgress_, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    vramText_ = CreateChild(
        L"STATIC",
        L"",
        SS_CENTER | SS_NOPREFIX,
        margin,
        940,
        1460,
        24,
        0);

    Refresh(view);
    return true;
}

void UIRenderer::Recreate(UINT dpi, const DashboardViewModel& view)
{
    DestroyControls();
    Create(parent_, instance_, dpi, view);
}

void UIRenderer::Refresh(const DashboardViewModel& view)
{
    SendMessageW(adapterCombo_, CB_RESETCONTENT, 0, 0);
    for (const std::wstring& item : view.adapterItems) {
        SendMessageW(adapterCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
    if (!view.adapterItems.empty()) {
        SendMessageW(adapterCombo_, CB_SETCURSEL, view.selectedAdapter, 0);
    }

    SendMessageW(displayModeCombo_, CB_RESETCONTENT, 0, 0);
    for (const std::wstring& item : view.displayModeItems) {
        SendMessageW(displayModeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
    if (!view.displayModeItems.empty()) {
        SendMessageW(displayModeCombo_, CB_SETCURSEL, view.selectedDisplayMode, 0);
    }

    SendMessageW(frameLimiterCombo_, CB_RESETCONTENT, 0, 0);
    for (const std::wstring& item : view.frameLimitItems) {
        SendMessageW(frameLimiterCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
    if (!view.frameLimitItems.empty()) {
        SendMessageW(frameLimiterCombo_, CB_SETCURSEL, view.selectedFrameLimit, 0);
    }

    Button_SetCheck(hdrCheck_, view.hdrEnabled ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(hdrCheck_, view.hdrAllowed ? TRUE : FALSE);
    Button_SetCheck(vsyncCheck_, view.vsync ? BST_CHECKED : BST_UNCHECKED);

    for (const SectionViewModel& section : view.sections) {
        for (const SliderViewModel& slider : section.sliders) {
            auto it = std::find_if(sliders_.begin(), sliders_.end(), [&](const SliderControls& controls) {
                return controls.key == slider.key;
            });
            if (it == sliders_.end()) {
                continue;
            }
            SendMessageW(it->trackbar, TBM_SETRANGE, TRUE, MAKELPARAM(slider.minValue, slider.maxValue));
            SendMessageW(it->trackbar, TBM_SETPOS, TRUE, slider.value);
            SetWindowTextW(it->label, slider.label.c_str());
            SetWindowTextW(it->value, slider.formattedValue.c_str());
        }
    }

    SetWindowTextW(hardwareInfo_, view.hardwareText.c_str());
    SetWindowTextW(hdrPathInfo_, view.hdrPathText.c_str());
    SetWindowTextW(statusInfo_, view.statusText.c_str());
    SendMessageW(vramProgress_, PBM_SETPOS, view.vramProgressPercent, 0);
    SendMessageW(vramProgress_, PBM_SETBARCOLOR, 0, BarColor(view.vramSeverity));
    SetWindowTextW(vramText_, view.vramText.c_str());
}

std::optional<SettingKey> UIRenderer::SliderKeyFromHwnd(HWND hwnd) const noexcept
{
    for (const SliderControls& slider : sliders_) {
        if (slider.trackbar == hwnd) {
            return slider.key;
        }
    }
    return std::nullopt;
}

LRESULT UIRenderer::OnCtlColorStatic(HDC dc, HWND hwnd) const noexcept
{
    const auto it = std::find(headerControls_.begin(), headerControls_.end(), hwnd);
    if (it == headerControls_.end()) {
        return 0;
    }
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkColor(dc, RGB(38, 74, 105));
    return reinterpret_cast<LRESULT>(headerBrush_);
}

int UIRenderer::Scale(int value) const noexcept
{
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

HWND UIRenderer::CreateChild(const wchar_t* className, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id)
{
    HWND child = CreateWindowExW(
        0,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        Scale(x),
        Scale(y),
        Scale(width),
        Scale(height),
        parent_,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
        instance_,
        nullptr);
    if (child) {
        controls_.push_back(child);
        ApplyFont(child);
    }
    return child;
}

HWND UIRenderer::CreateLabel(const wchar_t* text, int x, int y, int width, int height, int id)
{
    return CreateChild(L"STATIC", text, SS_LEFT | SS_NOPREFIX, x, y, width, height, id);
}

HWND UIRenderer::CreateHeader(const wchar_t* text, int x, int y, int width)
{
    HWND header = CreateChild(L"STATIC", text, SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX, x, y, width, UIMetrics::GroupHeaderHeight, 0);
    if (header) {
        headerControls_.push_back(header);
    }
    return header;
}

HWND UIRenderer::CreateButton(const wchar_t* text, int x, int y, int width, int height, int id)
{
    return CreateChild(L"BUTTON", text, BS_PUSHBUTTON | WS_TABSTOP, x, y, width, height, id);
}

HWND UIRenderer::CreateCheckbox(const wchar_t* text, int x, int y, int width, int height, int id)
{
    return CreateChild(L"BUTTON", text, BS_AUTOCHECKBOX | WS_TABSTOP, x, y, width, height, id);
}

HWND UIRenderer::CreateCombo(int x, int y, int width, int height, int id)
{
    return CreateChild(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x, y, width, height, id);
}

int UIRenderer::CreateDisplaySection(int x, int y, int width)
{
    CreateHeader(L"DISPLAY", x, y, width);
    int rowY = y + UIMetrics::GroupHeaderHeight + UIMetrics::SectionPadding;
    const int labelX = x + UIMetrics::SectionPadding;
    const int controlX = labelX + UIMetrics::LabelWidth;
    const int controlW = width - UIMetrics::LabelWidth - UIMetrics::SectionPadding * 2;

    CreateLabel(L"Adapter", labelX, rowY + 4, UIMetrics::LabelWidth, 20);
    adapterCombo_ = CreateCombo(controlX, rowY, controlW, 240, kAdapterComboId);
    rowY += UIMetrics::RowHeight;

    CreateLabel(L"Resolution / Refresh", labelX, rowY + 4, UIMetrics::LabelWidth, 20);
    displayModeCombo_ = CreateCombo(controlX, rowY, controlW, 320, kDisplayModeComboId);
    rowY += UIMetrics::RowHeight;

    CreateLabel(L"Frame Limit", labelX, rowY + 4, UIMetrics::LabelWidth, 20);
    frameLimiterCombo_ = CreateCombo(controlX, rowY, 160, 220, kFrameLimiterComboId);
    hdrCheck_ = CreateCheckbox(L"HDR", controlX + 178, rowY, 70, 24, kHdrCheckId);
    vsyncCheck_ = CreateCheckbox(L"VSync", controlX + 252, rowY, 92, 24, kVsyncCheckId);
    rowY += UIMetrics::RowHeight + 4;

    CreateButton(L"Apply RTX 4070 Preset", controlX, rowY, 174, 26, kPresetButtonId);
    CreateButton(L"Refresh", controlX + 184, rowY, 76, 26, kRefreshButtonId);
    CreateButton(L"Save", controlX + 270, rowY, 74, 26, kSaveButtonId);
    rowY += UIMetrics::RowHeight;
    return rowY + UIMetrics::SectionPadding;
}

int UIRenderer::CreateSliderSection(const SectionViewModel& section, int x, int y, int width)
{
    CreateHeader(section.name.c_str(), x, y, width);
    int rowY = y + UIMetrics::GroupHeaderHeight + UIMetrics::SectionPadding;
    const int labelX = x + UIMetrics::SectionPadding;
    const int sliderX = labelX + UIMetrics::LabelWidth;
    const int valueX = sliderX + UIMetrics::SliderWidth + 8;

    for (const SliderViewModel& slider : section.sliders) {
        HWND label = CreateLabel(slider.label.c_str(), labelX, rowY + 4, UIMetrics::LabelWidth, 20);
        HWND trackbar = CreateChild(
            TRACKBAR_CLASSW,
            L"",
            TBS_AUTOTICKS | TBS_HORZ | WS_TABSTOP,
            sliderX,
            rowY,
            UIMetrics::SliderWidth,
            UIMetrics::RowHeight,
            kSliderIdBase + static_cast<int>(slider.key));
        SendMessageW(trackbar, TBM_SETRANGE, TRUE, MAKELPARAM(slider.minValue, slider.maxValue));
        SendMessageW(trackbar, TBM_SETPAGESIZE, 0, 5);
        SendMessageW(trackbar, TBM_SETPOS, TRUE, slider.value);
        HWND value = CreateLabel(slider.formattedValue.c_str(), valueX, rowY + 4, UIMetrics::ValueWidth, 20);
        sliders_.push_back(SliderControls { slider.key, label, trackbar, value });
        rowY += UIMetrics::RowHeight;
    }
    return rowY + UIMetrics::SectionPadding;
}

void UIRenderer::DestroyControls() noexcept
{
    for (HWND hwnd : controls_) {
        if (hwnd && IsWindow(hwnd)) {
            DestroyWindow(hwnd);
        }
    }
    controls_.clear();
    headerControls_.clear();
    sliders_.clear();
    adapterCombo_ = nullptr;
    displayModeCombo_ = nullptr;
    frameLimiterCombo_ = nullptr;
    hdrCheck_ = nullptr;
    vsyncCheck_ = nullptr;
    hardwareInfo_ = nullptr;
    hdrPathInfo_ = nullptr;
    statusInfo_ = nullptr;
    vramProgress_ = nullptr;
    vramText_ = nullptr;
}

void UIRenderer::ApplyFont(HWND hwnd) const noexcept
{
    if (hwnd && font_) {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
}

} // namespace talal::dashboard
