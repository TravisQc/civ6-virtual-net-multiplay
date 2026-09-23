// 入口：初始化 Winsock、DPI 感知，创建 Slint 窗口并装配控制器，进入事件循环。
#include "app.h"  // Slint 生成
#include "app_controller.hpp"
#include "win_chrome.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

int main(int, char**) {
    // 高 DPI 清晰（best-effort，需在创建窗口前；清单里也声明了 permonitorv2）。
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

    auto window = AppWindow::create();
    civ6::AppController controller(window);
    controller.setup();  // 先填好配置再显示，避免闪一下默认值

    window->show();
    // 附加无边框窗口的 Win32 胶水（圆角/拖动/缩放）。若原生窗口延迟创建，控制器的定时器会重试。
    civ6::chrome::attach_main_window();

    slint::run_event_loop();

    // 事件循环退出后兜底清理（关闭按钮路径里已处理，双重保险）。
    controller.shutdown();
    WSACleanup();
    return 0;
}
