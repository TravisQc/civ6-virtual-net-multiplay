// 无边框窗口的 Win32 外观/交互胶水：Win11 圆角、系统级标题栏拖动与边缘缩放（WM_NCHITTEST）、
// 双击标题栏最大化、最小化/最大化按钮。
//
// 之所以不用 Slint 的 TouchArea 发 WM_NCLBUTTONDOWN：那会在 Slint 的按下回调里同步跑系统拖动循环，
// 松开事件被系统吃掉，Slint 会一直认为鼠标按在标题栏上（指针抓取不释放），之后所有点击都变成拖动。
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace civ6::chrome {

// 找到本线程的主窗口并附加（圆角 + 子类化 WndProc）。幂等；窗口尚未创建时返回 false，可稍后重试。
// 可传入已获取的 HWND；若为 nullptr 则自动查找主窗口。
// title_height / caption_buttons_width 为逻辑像素，需与 .slint 中的标题栏尺寸一致。
bool attach_main_window(HWND hwnd = nullptr, int title_height = 48, int caption_buttons_width = 3 * 46);
bool attached();

// 对本线程当前所有可见顶层窗口应用 Win11 圆角（幂等；旧系统忽略）。
// 覆盖主窗口之外按需创建的次级窗口（如进程选择窗口），使其也获得圆角。
void round_thread_windows();

void minimize();
void toggle_maximize();
void set_dark_mode(bool dark);

// 取主窗口的“正常”矩形（还原状态、物理屏幕像素）。窗口处于正常状态时返回其实时矩形，
// 处于最大化/最小化时返回最近一次正常状态缓存的矩形。无可用值（未附加且无缓存）返回 false。
// 用于退出/保存时持久化窗口位置尺寸，天然不受最大化/最小化影响。
bool get_normal_rect(RECT& out);

// 判断矩形是否与任一显示器相交（可见）。用于恢复前校验保存的位置是否仍落在可用显示器上。
bool is_rect_visible(const RECT& rc);

}  // namespace civ6::chrome
