#include "win_chrome.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

namespace civ6::chrome {

namespace {

HWND g_hwnd = nullptr;
int g_title_height = 48;            // 逻辑像素
int g_caption_buttons_width = 138;  // 逻辑像素
constexpr int kResizeBorder = 6;    // 逻辑像素：窗口四周可拖动缩放的宽度
constexpr UINT_PTR kSubclassId = 1;
// DWMWA_WINDOW_CORNER_PREFERENCE / DWMWCP_ROUND（Windows 11；旧系统忽略该属性）
constexpr DWORD kDwmCornerPreference = 33;
constexpr DWORD kDwmCornerRound = 2;

float scale_of(HWND h) { return GetDpiForWindow(h) / 96.0f; }

// 命中测试：四周边缘 -> 缩放；标题栏 -> 系统拖动；标题栏右侧按钮与其余区域 -> 交给 Slint。
LRESULT hit_test(HWND h, POINT screen_pt) {
    const float s = scale_of(h);
    const bool maximized = IsZoomed(h) != 0;

    if (!maximized) {
        RECT wr{};
        GetWindowRect(h, &wr);
        const int b = static_cast<int>(kResizeBorder * s);
        const bool left = screen_pt.x < wr.left + b;
        const bool right = screen_pt.x >= wr.right - b;
        const bool top = screen_pt.y < wr.top + b;
        const bool bottom = screen_pt.y >= wr.bottom - b;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }

    // 标题栏用客户区坐标判断（最大化时 winit 把客户区限制在工作区，与窗口矩形不一致）。
    POINT cp = screen_pt;
    ScreenToClient(h, &cp);
    RECT cr{};
    GetClientRect(h, &cr);
    if (cp.y >= 0 && cp.y < static_cast<int>(g_title_height * s)) {
        if (cp.x >= cr.right - static_cast<int>(g_caption_buttons_width * s)) return HTCLIENT;
        return HTCAPTION;
    }
    return HTCLIENT;
}

LRESULT CALLBACK subclass_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (msg) {
        case WM_NCHITTEST:
            return hit_test(h, POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        case WM_NCLBUTTONDBLCLK:
            if (wp == HTCAPTION) {  // 双击标题栏：最大化/还原
                toggle_maximize();
                return 0;
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(h, subclass_proc, kSubclassId);
            if (g_hwnd == h) g_hwnd = nullptr;
            break;
        default:
            break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

BOOL CALLBACK first_visible_window(HWND h, LPARAM out) {
    if (!IsWindowVisible(h)) return TRUE;
    RECT r{};
    GetWindowRect(h, &r);
    if (r.right - r.left <= 0 || r.bottom - r.top <= 0) return TRUE;
    *reinterpret_cast<HWND*>(out) = h;
    return FALSE;
}

}  // namespace

bool attach_main_window(int title_height, int caption_buttons_width) {
    if (attached()) return true;
    HWND found = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), first_visible_window, reinterpret_cast<LPARAM>(&found));
    if (!found) return false;

    g_hwnd = found;
    g_title_height = title_height;
    g_caption_buttons_width = caption_buttons_width;

    DwmSetWindowAttribute(g_hwnd, kDwmCornerPreference, &kDwmCornerRound, sizeof(kDwmCornerRound));
    SetWindowSubclass(g_hwnd, subclass_proc, kSubclassId, 0);
    // 让系统重新评估非客户区，使圆角与命中测试立即生效。
    SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return true;
}

bool attached() { return g_hwnd != nullptr && IsWindow(g_hwnd); }

void minimize() {
    if (attached()) ShowWindow(g_hwnd, SW_MINIMIZE);
}

void toggle_maximize() {
    if (!attached()) return;
    ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
}

}  // namespace civ6::chrome
