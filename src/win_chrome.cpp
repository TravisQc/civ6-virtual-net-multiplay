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
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmUseImmersiveDarkModeOld = 19;
bool g_dark_mode = false;

// 最近一次窗口处于正常状态（非最大化/最小化）时的矩形（物理屏幕像素）。
// 在 WM_EXITSIZEMOVE 时更新；供 get_normal_rect 在窗口当前为最大化/最小化时回退使用。
RECT g_normal_rect{};
bool g_have_normal_rect = false;

float scale_of(HWND h) { return GetDpiForWindow(h) / 96.0f; }

// 对单个窗口应用 Win11 圆角（DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND）。
// 幂等；Windows 10 及更早忽略该属性，返回值无需处理。
void set_corners_rounded(HWND h) {
    DwmSetWindowAttribute(h, kDwmCornerPreference, &kDwmCornerRound, sizeof(kDwmCornerRound));
}

void apply_dark_mode(HWND h, bool dark) {
    BOOL val = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(h, kDwmUseImmersiveDarkMode, &val, sizeof(val));
    DwmSetWindowAttribute(h, kDwmUseImmersiveDarkModeOld, &val, sizeof(val));
}

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
        case WM_NCLBUTTONDOWN:
            if (wp == HTCAPTION) {
                if (IsZoomed(h)) {
                    RECT wr{};
                    GetWindowRect(h, &wr);
                    int cur_width = wr.right - wr.left;

                    WINDOWPLACEMENT wpi{sizeof(WINDOWPLACEMENT)};
                    GetWindowPlacement(h, &wpi);
                    int normal_width = wpi.rcNormalPosition.right - wpi.rcNormalPosition.left;
                    int normal_height = wpi.rcNormalPosition.bottom - wpi.rcNormalPosition.top;
                    if (normal_width <= 0) normal_width = 1024;
                    if (normal_height <= 0) normal_height = 600;

                    POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                    float ratio = cur_width > 0 ? static_cast<float>(pt.x - wr.left) / static_cast<float>(cur_width) : 0.5f;
                    int new_x = pt.x - static_cast<int>(normal_width * ratio);
                    int new_y = pt.y - 10;

                    ShowWindow(h, SW_RESTORE);
                    SetWindowPos(h, nullptr, new_x, new_y, normal_width, normal_height,
                                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
                }
                ReleaseCapture();
                SendMessage(h, WM_SYSCOMMAND, 0xF012 /* SC_MOVE | HTCAPTION */, 0);
                return 0;
            }
            break;
        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == SC_MOVE) {
                return DefWindowProc(h, msg, wp, lp);
            }
            break;
        case WM_NCLBUTTONDBLCLK:
            if (wp == HTCAPTION) {  // 双击标题栏：最大化/还原
                toggle_maximize();
                return 0;
            }
            break;
        case WM_EXITSIZEMOVE:
            // 拖动/缩放结束：仅在正常状态记录矩形，最大化/最小化期间不覆盖，缓存自然只保留正常矩形。
            if (!IsZoomed(h) && !IsIconic(h)) {
                GetWindowRect(h, &g_normal_rect);
                g_have_normal_rect = true;
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

// EnumWindows 回调：在「本进程」内挑选第一个可见、顶层、足够大的窗口。
// win32_hwnd() 尚不可用时的兜底；尺寸下限用于排除 winit 的 24x24 事件目标等辅助窗口。
constexpr int kMinAttachSize = 200;  // 物理像素
BOOL CALLBACK enum_pick_process(HWND h, LPARAM lp) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    if (!IsWindowVisible(h) || GetAncestor(h, GA_ROOT) != h) return TRUE;
    RECT r{};
    GetWindowRect(h, &r);
    if (r.right - r.left < kMinAttachSize || r.bottom - r.top < kMinAttachSize) return TRUE;
    *reinterpret_cast<HWND*>(lp) = h;
    return FALSE;  // 找到即停
}

// EnumThreadWindows 回调：对每个可见、尺寸非零的顶层窗口应用圆角与沉浸式暗色属性。
BOOL CALLBACK round_if_visible(HWND h, LPARAM) {
    if (!IsWindowVisible(h)) return TRUE;
    RECT r{};
    GetWindowRect(h, &r);
    if (r.right - r.left <= 0 || r.bottom - r.top <= 0) return TRUE;
    set_corners_rounded(h);
    apply_dark_mode(h, g_dark_mode);
    return TRUE;
}

}  // namespace

bool attach_main_window(HWND hwnd, int title_height, int caption_buttons_width) {
    // 目标顶层窗口：优先用 Slint 提供的 HWND（上溯到根窗口）；否则在本进程内查找合适的顶层窗口。
    // winit 早期会先建出一个可见的 24x24「事件目标」窗口，故不能简单取“第一个可见窗口”。
    HWND target = (hwnd && IsWindow(hwnd)) ? GetAncestor(hwnd, GA_ROOT) : nullptr;
    if (!target) {
        EnumWindows(enum_pick_process, reinterpret_cast<LPARAM>(&target));
    }

    if (!target || !IsWindow(target)) return false;  // 尚不可用，下个定时器 tick 再试
    if (g_hwnd == target) return true;               // 已附加到正确窗口，幂等

    // 自愈：若此前误附加到别的窗口，先摘除旧子类再附加到正确的顶层窗口。
    if (g_hwnd && IsWindow(g_hwnd)) RemoveWindowSubclass(g_hwnd, subclass_proc, kSubclassId);

    g_hwnd = target;
    g_title_height = title_height;
    g_caption_buttons_width = caption_buttons_width;

    set_corners_rounded(g_hwnd);
    apply_dark_mode(g_hwnd, g_dark_mode);
    SetWindowSubclass(g_hwnd, subclass_proc, kSubclassId, 0);
    // 让系统重新评估非客户区，使圆角与命中测试立即生效。
    SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return true;
}

bool attached() { return g_hwnd != nullptr && IsWindow(g_hwnd); }

void round_thread_windows() {
    EnumThreadWindows(GetCurrentThreadId(), round_if_visible, 0);
}

void minimize() {
    if (attached()) ShowWindow(g_hwnd, SW_MINIMIZE);
}

void toggle_maximize() {
    if (!attached()) return;
    ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
}

void set_dark_mode(bool dark) {
    g_dark_mode = dark;
    if (attached()) {
        apply_dark_mode(g_hwnd, dark);
        SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

bool get_normal_rect(RECT& out) {
    // 正常状态：实时矩形即正常矩形（也涵盖“恢复后未再拖动”的情形）。
    if (attached() && !IsZoomed(g_hwnd) && !IsIconic(g_hwnd)) {
        GetWindowRect(g_hwnd, &out);
        return true;
    }
    // 最大化/最小化或尚未附加：回退到最近一次缓存的正常矩形。
    if (g_have_normal_rect) {
        out = g_normal_rect;
        return true;
    }
    return false;
}

bool is_rect_visible(const RECT& rc) {
    // MONITOR_DEFAULTTONULL：矩形与任何显示器都不相交时返回 nullptr（如外接显示器已断开）。
    return MonitorFromRect(&rc, MONITOR_DEFAULTTONULL) != nullptr;
}

}  // namespace civ6::chrome
