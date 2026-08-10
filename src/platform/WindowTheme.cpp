#include "WindowTheme.hpp"

#if defined(_WIN32)

#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>

namespace mcdev::platform {
namespace {

constexpr LONG kMinimumClientWidth = 860;
constexpr LONG kMinimumClientHeight = 520;

HWND themedWindow = nullptr;
WNDPROC previousWindowProc = nullptr;

LRESULT cornerResizeHitTest(const HWND window, const LPARAM lParam, const LRESULT fallback) {
    if (IsZoomed(window) || (GetWindowLongPtrW(window, GWL_STYLE) & WS_THICKFRAME) == 0) {
        return fallback;
    }

    RECT bounds{};
    if (!GetWindowRect(window, &bounds)) {
        return fallback;
    }

    const UINT dpi = GetDpiForWindow(window);
    const int borderX = std::max(
        GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi)
            + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi),
        MulDiv(6, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI));
    const int borderY = std::max(
        GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi)
            + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi),
        MulDiv(6, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI));
    const int cornerX = std::max(borderX * 2, MulDiv(16, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI));
    const int cornerY = std::max(borderY * 2, MulDiv(16, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI));
    const int x = GET_X_LPARAM(lParam);
    const int y = GET_Y_LPARAM(lParam);

    const bool onLeftBorder = x < bounds.left + borderX;
    const bool onRightBorder = x >= bounds.right - borderX;
    const bool onTopBorder = y < bounds.top + borderY;
    const bool onBottomBorder = y >= bounds.bottom - borderY;
    const bool nearLeft = x < bounds.left + cornerX;
    const bool nearRight = x >= bounds.right - cornerX;
    const bool nearTop = y < bounds.top + cornerY;
    const bool nearBottom = y >= bounds.bottom - cornerY;

    if (nearLeft && nearTop && (onLeftBorder || onTopBorder)) {
        return HTTOPLEFT;
    }
    if (nearRight && nearTop && (onRightBorder || onTopBorder)) {
        return HTTOPRIGHT;
    }
    if (nearLeft && nearBottom && (onLeftBorder || onBottomBorder)) {
        return HTBOTTOMLEFT;
    }
    if (nearRight && nearBottom && (onRightBorder || onBottomBorder)) {
        return HTBOTTOMRIGHT;
    }
    return fallback;
}

LRESULT CALLBACK workbenchWindowProc(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam) {
    const LRESULT result = previousWindowProc != nullptr
        ? CallWindowProcW(previousWindowProc, window, message, wParam, lParam)
        : DefWindowProcW(window, message, wParam, lParam);
    if (message == WM_NCHITTEST) {
        return cornerResizeHitTest(window, lParam, result);
    }
    if (message == WM_GETMINMAXINFO) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        const UINT dpi = GetDpiForWindow(window);
        RECT frame{
            0,
            0,
            MulDiv(kMinimumClientWidth, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI),
            MulDiv(kMinimumClientHeight, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI),
        };
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
        const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
        if (AdjustWindowRectExForDpi(&frame, style, FALSE, extendedStyle, dpi)) {
            limits->ptMinTrackSize.x = frame.right - frame.left;
            limits->ptMinTrackSize.y = frame.bottom - frame.top;
        }
    }
    return result;
}

bool belongsToCurrentProcess(const HWND window) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    return processId == GetCurrentProcessId();
}

} // namespace

WindowSize initialWindowSize(const int logicalWidth, const int logicalHeight) {
    const UINT dpi = GetDpiForSystem();
    int width = MulDiv(
        std::max(1, logicalWidth),
        static_cast<int>(dpi),
        USER_DEFAULT_SCREEN_DPI);
    int height = MulDiv(
        std::max(1, logicalHeight),
        static_cast<int>(dpi),
        USER_DEFAULT_SCREEN_DPI);

    RECT workArea{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
        const int margin = MulDiv(32, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
        const int availableWidth =
            static_cast<int>(workArea.right - workArea.left) - margin * 2;
        const int availableHeight =
            static_cast<int>(workArea.bottom - workArea.top) - margin * 2;
        width = std::min(width, std::max(1, availableWidth));
        height = std::min(height, std::max(1, availableHeight));
    }
    return {width, height};
}

std::string preferredUiFont() {
    constexpr const char* path = "C:/Windows/Fonts/segoeui.ttf";
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ? path : "";
}

void applyWindowTheme() {
    HWND window = GetActiveWindow();
    if (window == nullptr || !belongsToCurrentProcess(window)) {
        return;
    }
    window = GetAncestor(window, GA_ROOT);
    if (window == nullptr) {
        return;
    }

    if (themedWindow != window) {
        themedWindow = window;
        previousWindowProc = reinterpret_cast<WNDPROC>(
            GetWindowLongPtrW(window, GWLP_WNDPROC));
        if (previousWindowProc != nullptr) {
            SetLastError(ERROR_SUCCESS);
            const LONG_PTR previous = SetWindowLongPtrW(
                window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(workbenchWindowProc));
            if (previous == 0 && GetLastError() != ERROR_SUCCESS) {
                previousWindowProc = nullptr;
            }
        }

        constexpr DWORD kUseImmersiveDarkMode = 20;
        constexpr DWORD kWindowCornerPreference = 33;
        constexpr DWORD kBorderColor = 34;
        constexpr DWORD kCaptionColor = 35;
        constexpr DWORD kTextColor = 36;
        const BOOL darkMode = TRUE;
        const DWORD roundCorners = 2;
        const COLORREF border = RGB(61, 61, 61);
        const COLORREF caption = RGB(33, 33, 33);
        const COLORREF text = RGB(236, 236, 236);
        (void)DwmSetWindowAttribute(
            window,
            static_cast<DWMWINDOWATTRIBUTE>(kUseImmersiveDarkMode),
            &darkMode,
            sizeof(darkMode));
        (void)DwmSetWindowAttribute(
            window,
            static_cast<DWMWINDOWATTRIBUTE>(kWindowCornerPreference),
            &roundCorners,
            sizeof(roundCorners));
        (void)DwmSetWindowAttribute(
            window,
            static_cast<DWMWINDOWATTRIBUTE>(kBorderColor),
            &border,
            sizeof(border));
        (void)DwmSetWindowAttribute(
            window,
            static_cast<DWMWINDOWATTRIBUTE>(kCaptionColor),
            &caption,
            sizeof(caption));
        (void)DwmSetWindowAttribute(
            window,
            static_cast<DWMWINDOWATTRIBUTE>(kTextColor),
            &text,
            sizeof(text));
    }
}

} // namespace mcdev::platform

#else

namespace mcdev::platform {

WindowSize initialWindowSize(const int logicalWidth, const int logicalHeight) {
    return {logicalWidth, logicalHeight};
}

std::string preferredUiFont() {
    return {};
}

void applyWindowTheme() {}

} // namespace mcdev::platform

#endif
