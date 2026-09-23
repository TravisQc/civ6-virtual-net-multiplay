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

#include <chrono>
#include <fstream>
#include <string>
#include <vector>

namespace {

void save_bmp(const char* filename, int width, int height, const uint8_t* rgba) {
    BITMAPFILEHEADER bfh{};
    bfh.bfType = 0x4D42;  // "BM"
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + width * height * 4;

    BITMAPINFOHEADER bih{};
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = width;
    bih.biHeight = -height;  // top-down
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;

    std::ofstream out(filename, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&bfh), sizeof(bfh));
    out.write(reinterpret_cast<const char*>(&bih), sizeof(bih));
    std::vector<uint8_t> bgra(width * height * 4);
    for (int i = 0; i < width * height; ++i) {
        bgra[i * 4 + 0] = rgba[i * 4 + 2];  // B
        bgra[i * 4 + 1] = rgba[i * 4 + 1];  // G
        bgra[i * 4 + 2] = rgba[i * 4 + 0];  // R
        bgra[i * 4 + 3] = rgba[i * 4 + 3];  // A
    }
    out.write(reinterpret_cast<const char*>(bgra.data()), bgra.size());
}

}  // namespace

int main(int argc, char** argv) {
    // 高 DPI 清晰（best-effort，需在创建窗口前；清单里也声明了 permonitorv2）。
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

    bool snapshot_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--snapshot") snapshot_mode = true;
    }
    if (wcsstr(GetCommandLineW(), L"--snapshot") != nullptr) {
        snapshot_mode = true;
    }

    auto window = AppWindow::create();
    civ6::AppController controller(window);
    controller.setup();  // 先填好配置再显示，避免闪一下默认值

    window->show();
    // 附加无边框窗口的 Win32 胶水（圆角/拖动/缩放）。若原生窗口延迟创建，控制器的定时器会重试。
    civ6::chrome::attach_main_window();

    std::shared_ptr<slint::Timer> snap_timer;
    if (snapshot_mode) {
        window->set_dark_mode(false);
        window->set_running(false);
        snap_timer = std::make_shared<slint::Timer>();
        auto step = std::make_shared<int>(0);
        snap_timer->start(slint::TimerMode::Repeated, std::chrono::milliseconds(300), [window, snap_timer, step]() {
            if (*step == 0) {
                auto snap = window->window().take_snapshot();
                if (snap) {
                    save_bmp("snapshot_light.bmp", snap->width(), snap->height(),
                             reinterpret_cast<const uint8_t*>(snap->begin()));
                }
                window->set_running(true);
                *step = 1;
            } else if (*step == 1) {
                auto snap = window->window().take_snapshot();
                if (snap) {
                    save_bmp("snapshot_running.bmp", snap->width(), snap->height(),
                             reinterpret_cast<const uint8_t*>(snap->begin()));
                }
                window->set_running(false);
                window->set_dark_mode(true);
                civ6::chrome::set_dark_mode(true);
                *step = 2;
            } else if (*step == 2) {
                auto snap = window->window().take_snapshot();
                if (snap) {
                    save_bmp("snapshot_dark.bmp", snap->width(), snap->height(),
                             reinterpret_cast<const uint8_t*>(snap->begin()));
                }
                window->set_current_page(1);
                *step = 3;
            } else if (*step == 3) {
                auto snap = window->window().take_snapshot();
                if (snap) {
                    save_bmp("snapshot_logs.bmp", snap->width(), snap->height(),
                             reinterpret_cast<const uint8_t*>(snap->begin()));
                }
                snap_timer->stop();
                slint::quit_event_loop();
            }
        });
    }

    slint::run_event_loop();

    // 事件循环退出后兜底清理（关闭按钮路径里已处理，双重保险）。
    controller.shutdown();
    WSACleanup();
    return 0;
}
