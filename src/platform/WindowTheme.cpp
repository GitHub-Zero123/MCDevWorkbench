#include "WindowTheme.hpp"

#if defined(_WIN32)

#include <dwmapi.h>
#include <windows.h>

namespace mcdev::platform {
namespace {

constexpr LONG kMinimumClientWidth = 880;
constexpr LONG kMinimumClientHeight = 620;

HWND themedWindow = nullptr;
WNDPROC previousWindowProc = nullptr;

LRESULT CALLBACK workbenchWindowProc(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam) {
    if (message == WM_GETMINMAXINFO) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        const UINT dpi = GetDpiForWindow(window);
        limits->ptMinTrackSize.x = MulDiv(kMinimumClientWidth, static_cast<int>(dpi), 96);
        limits->ptMinTrackSize.y = MulDiv(kMinimumClientHeight, static_cast<int>(dpi), 96);
    }
    return previousWindowProc != nullptr
        ? CallWindowProcW(previousWindowProc, window, message, wParam, lParam)
        : DefWindowProcW(window, message, wParam, lParam);
}

bool belongsToCurrentProcess(const HWND window) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    return processId == GetCurrentProcessId();
}

} // namespace

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
        constexpr DWORD kBorderColor = 34;
        constexpr DWORD kCaptionColor = 35;
        constexpr DWORD kTextColor = 36;
        const BOOL darkMode = TRUE;
        const COLORREF border = RGB(42, 42, 42);
        const COLORREF caption = RGB(14, 14, 14);
        const COLORREF text = RGB(245, 245, 245);
        (void)DwmSetWindowAttribute(
            window,
            static_cast<DWMWINDOWATTRIBUTE>(kUseImmersiveDarkMode),
            &darkMode,
            sizeof(darkMode));
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
        SetWindowPos(
            window,
            nullptr,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

} // namespace mcdev::platform

#else

namespace mcdev::platform {

void applyWindowTheme() {}

} // namespace mcdev::platform

#endif
