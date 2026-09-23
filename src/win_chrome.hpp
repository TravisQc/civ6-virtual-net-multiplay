// 无边框窗口的 Win32 外观/交互胶水：Win11 圆角、系统级标题栏拖动与边缘缩放（WM_NCHITTEST）、
// 双击标题栏最大化、最小化/最大化按钮。
//
// 之所以不用 Slint 的 TouchArea 发 WM_NCLBUTTONDOWN：那会在 Slint 的按下回调里同步跑系统拖动循环，
// 松开事件被系统吃掉，Slint 会一直认为鼠标按在标题栏上（指针抓取不释放），之后所有点击都变成拖动。
#pragma once

namespace civ6::chrome {

// 找到本线程的主窗口并附加（圆角 + 子类化 WndProc）。幂等；窗口尚未创建时返回 false，可稍后重试。
// title_height / caption_buttons_width 为逻辑像素，需与 .slint 中的标题栏尺寸一致。
bool attach_main_window(int title_height = 48, int caption_buttons_width = 3 * 46);
bool attached();

void minimize();
void toggle_maximize();

}  // namespace civ6::chrome
