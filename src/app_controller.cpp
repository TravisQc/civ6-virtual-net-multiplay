#include "app_controller.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <stdexcept>

#include "divert.hpp"
#include "icons.hpp"
#include "proxy.hpp"
#include "win_chrome.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace civ6 {

namespace {

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

void show_error(const std::string& title, const std::string& message) {
    MessageBoxW(nullptr, utf8_to_wide(message).c_str(), utf8_to_wide(title).c_str(),
                MB_OK | MB_ICONERROR);
}

void show_info(const std::string& title, const std::string& message) {
    MessageBoxW(nullptr, utf8_to_wide(message).c_str(), utf8_to_wide(title).c_str(),
                MB_OK | MB_ICONINFORMATION);
}

slint::Image to_slint_image(const IconRgba& icon) {
    slint::SharedPixelBuffer<slint::Rgba8Pixel> buf(
        static_cast<uint32_t>(icon.width), static_cast<uint32_t>(icon.height));
    std::memcpy(buf.begin(), icon.pixels.data(), icon.pixels.size());
    return slint::Image(buf);
}

std::string now_hms() {
    auto t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

std::string join_ports(const std::vector<int>& ports) {
    std::string s;
    for (std::size_t i = 0; i < ports.size(); ++i) {
        if (i) s += ",";
        s += std::to_string(ports[i]);
    }
    return s;
}

}  // namespace

AppController::AppController(slint::ComponentHandle<AppWindow> window)
    : window_(std::move(window)) {}

void AppController::setup() {
    cfg_ = load_config();
    log_model_ = std::make_shared<slint::VectorModel<LogLine>>();

    // 载入配置到 UI
    window_->set_mode(slint::SharedString(cfg_.mode));
    window_->set_server_vip(slint::SharedString(cfg_.server_vip));
    window_->set_client_ip(slint::SharedString(cfg_.client_ip));
    window_->set_peer_vips(slint::SharedString(cfg_.peer_vips));
    window_->set_ports(slint::SharedString(cfg_.ports));
    window_->set_verbose(false);
    window_->set_running(false);
    window_->set_version(slint::SharedString("1.2 修正版"));
    window_->set_log_lines(log_model_);

    // 本机 IP 提示
    auto ips = list_local_ips();
    std::string hint = "本机检测到的局域网 IP: ";
    if (ips.empty()) {
        hint += "未检测到";
    } else {
        for (std::size_t i = 0; i < ips.size(); ++i) {
            if (i) hint += ", ";
            hint += ips[i];
        }
    }
    window_->set_local_ips_hint(slint::SharedString(hint));

    // 行为回调
    window_->on_start_proxy([this] { on_start(); });
    window_->on_stop_proxy([this] { on_stop(); });
    window_->on_pick_process([this] { on_pick_process(); });
    window_->on_verbose_changed([this](bool v) {
        if (engine_) engine_->set_verbose(v);
    });
    window_->on_open_settings([] {
        show_info("设置", "配置会在启动代理或关闭窗口时自动保存到程序目录的 civ6proxy_config.json。");
    });
    window_->on_open_help([] {
        show_info("帮助",
                  "同机模式：本机就是游戏机，需以管理员运行，并在“对端虚拟 IP”里填入其他玩家的虚拟 IP。\n\n"
                  "中转模式：在不运行游戏的独立设备上运行，填服务端虚拟 IP 与客户端真实局域网 IP。\n\n"
                  "端口默认 62900；可用“从程序选择…”读取占用 UDP 端口的程序。");
    });

    // 窗口控制（拖动/缩放由 win_chrome 的 WM_NCHITTEST 交给系统处理）
    window_->on_minimize([] { chrome::minimize(); });
    window_->on_maximize([] { chrome::toggle_maximize(); });
    window_->on_close_window([this] {
        shutdown();
        slint::quit_event_loop();
    });

    // 日志泵：100ms 抽干队列（对齐 Python 的 after(100)）。
    log_timer_.start(slint::TimerMode::Repeated, std::chrono::milliseconds(100), [this] {
        if (!chrome::attached()) chrome::attach_main_window();  // 原生窗口可能延迟创建
        chrome::round_thread_windows();  // 让次级窗口（如进程选择窗口）也获得 Win11 圆角
        pump_logs();
    });
}

void AppController::on_start() {
    std::string mode = std::string(window_->get_mode());
    try {
        auto ports = parse_ports(std::string(window_->get_ports()));
        if (ports.empty()) throw std::invalid_argument("请至少填写一个端口");

        std::unique_ptr<Engine> engine;
        if (mode == "windivert") {
            std::string peers_raw = std::string(window_->get_peer_vips());
            // 逗号（含中文逗号）分隔
            std::vector<std::string> peers;
            std::string norm;
            for (std::size_t i = 0; i < peers_raw.size();) {
                unsigned char c = (unsigned char)peers_raw[i];
                if (c == 0xEF && i + 2 < peers_raw.size() &&
                    (unsigned char)peers_raw[i + 1] == 0xBC &&
                    (unsigned char)peers_raw[i + 2] == 0x8C) { norm += ','; i += 3; }
                else { norm += peers_raw[i]; ++i; }
            }
            std::string cur;
            for (char ch : norm) {
                if (ch == ',') { peers.push_back(cur); cur.clear(); }
                else cur += ch;
            }
            peers.push_back(cur);

            auto div = std::make_unique<DivertForwarder>(ports, peers, log_bus_.as_fn());
            div->start();
            engine = std::move(div);
        } else {
            auto mgr = std::make_unique<ProxyManager>(log_bus_.as_fn());
            mgr->start(std::string(window_->get_server_vip()),
                       std::string(window_->get_client_ip()), ports);
            engine = std::move(mgr);
        }
        engine_ = std::move(engine);
    } catch (const std::exception& e) {
        show_error("启动失败", e.what());
        return;
    }

    engine_->set_verbose(window_->get_verbose());
    save_current_config();
    set_running(true);
}

void AppController::on_stop() {
    if (engine_) {
        engine_->stop();
        engine_.reset();
    }
    set_running(false);
}

void AppController::on_pick_process() {
    std::vector<UdpProcess> procs;
    try {
        for (auto& p : list_udp_processes())
            if (!p.ports.empty()) procs.push_back(std::move(p));
    } catch (const std::exception& e) {
        show_error("无法枚举进程", e.what());
        return;
    }
    if (procs.empty()) {
        show_info("提示",
                  "未发现占用 UDP 端口的程序。\n请确认目标程序（如文明6）正在运行，"
                  "且本工具与它在同一台机器上。");
        return;
    }
    picker_procs_ = std::move(procs);

    auto rows = std::make_shared<slint::VectorModel<ProcRow>>();
    for (const auto& p : picker_procs_) {
        ProcRow row;
        row.name = slint::SharedString(p.name);
        row.pid = slint::SharedString(std::to_string(p.pid));
        row.ports = slint::SharedString(join_ports(p.ports));
        IconRgba icon = extract_icon_rgba(p.exe, 20);
        if (!icon.empty()) {
            row.icon = to_slint_image(icon);
            row.has_icon = true;
        } else {
            row.has_icon = false;
        }
        rows->push_back(row);
    }

    picker_ = std::make_shared<slint::ComponentHandle<ProcessPickerWindow>>(
        ProcessPickerWindow::create());
    auto pk = *picker_;
    pk->set_procs(rows);
    pk->on_cancel([this] {
        if (picker_) (*picker_)->hide();
        picker_.reset();
    });
    pk->on_confirm([this](int idx) {
        if (idx >= 0 && idx < (int)picker_procs_.size()) {
            window_->set_ports(slint::SharedString(join_ports(picker_procs_[idx].ports)));
        }
        if (picker_) (*picker_)->hide();
        picker_.reset();
    });
    pk->show();
}

void AppController::set_running(bool running) { window_->set_running(running); }

void AppController::save_current_config() {
    cfg_.mode = std::string(window_->get_mode());
    cfg_.server_vip = std::string(window_->get_server_vip());
    cfg_.client_ip = std::string(window_->get_client_ip());
    cfg_.peer_vips = std::string(window_->get_peer_vips());
    cfg_.ports = std::string(window_->get_ports());
    save_config(cfg_);
}

void AppController::pump_logs() {
    auto entries = log_bus_.drain();
    for (const auto& e : entries) append_log(e.level, e.message);
}

void AppController::append_log(LogLevel level, const std::string& msg) {
    LogLine line;
    line.level = slint::SharedString(to_string(level));
    line.time = slint::SharedString(now_hms());
    line.text = slint::SharedString(msg);
    log_model_->push_back(line);
    // 限制行数（对齐 Python 的裁剪逻辑）。
    while (log_model_->row_count() > 1000) log_model_->erase(0);
}

void AppController::shutdown() {
    if (engine_ && engine_->running()) engine_->stop();
    engine_.reset();
    save_current_config();
}

}  // namespace civ6
