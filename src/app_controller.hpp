// UI <-> 核心 的桥接：装配回调、启停引擎、日志泵、进程选择、窗口控制、配置存取。
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app.h"  // Slint 生成（来自 ui/app.slint）
#include "config.hpp"
#include "engine.hpp"
#include "log_bus.hpp"
#include "net_util.hpp"

namespace civ6 {

class AppController {
public:
    explicit AppController(slint::ComponentHandle<AppWindow> window);

    // 载入配置到 UI、装配所有回调、启动日志泵。
    void setup();

    // 关闭前：若运行中先停，保存当前配置。
    void shutdown();

private:
    void on_start();
    void on_stop();
    void on_pick_process();
    void set_running(bool running);
    void save_current_config();
    void restore_window_placement();
    void pump_logs();
    void append_log(LogLevel level, const std::string& msg);

    slint::ComponentHandle<AppWindow> window_;
    Config cfg_;
    LogBus log_bus_;
    std::unique_ptr<Engine> engine_;

    std::shared_ptr<slint::VectorModel<LogLine>> log_model_;
    slint::Timer log_timer_;

    // 进程选择器（保活）
    std::shared_ptr<slint::ComponentHandle<ProcessPickerWindow>> picker_;
    std::vector<UdpProcess> picker_procs_;
};

}  // namespace civ6
