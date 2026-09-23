// 中转模式（Relay）：把客户端(真实局域网)与服务端(虚拟 VIP)间的 UDP 报文互转。
// 对应 proxy.py 的 UdpProxy / ProxyManager。
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "engine.hpp"
#include "log_bus.hpp"

namespace civ6 {

// 单端口双向 UDP 代理，可优雅启停。
class UdpProxy {
public:
    UdpProxy(int listen_port, std::string server_vip, std::string client_ip,
             LogFn log, std::shared_ptr<std::atomic_bool> verbose);
    ~UdpProxy();

    // 绑定并起线程。失败抛 std::runtime_error（含 WinError 码，供上层组织文案）。
    void start();
    void stop();
    bool running() const;

    int listen_port() const { return listen_port_; }

private:
    void loop();
    void safe_send(const char* data, int len, const std::string& ip, unsigned short port);
    void maybe_warn_no_server();

    int listen_port_;
    std::string server_vip_;
    std::string client_ip_;
    LogFn log_;
    std::shared_ptr<std::atomic_bool> verbose_;

    unsigned long long sock_ = ~0ull;  // SOCKET，避免头文件泄漏 winsock
    std::thread thread_;
    std::atomic_bool stop_{false};

    unsigned short client_port_ = 0;
    bool server_seen_ = false;
    int to_server_count_ = 0;
    bool warned_no_server_ = false;
};

// 管理多个端口上的代理实例。
class ProxyManager : public Engine {
public:
    explicit ProxyManager(LogFn log);
    ~ProxyManager() override;

    // 校验 IP/端口后逐端口启动；失败回滚并抛异常。
    void start(const std::string& server_vip, const std::string& client_ip,
               const std::vector<int>& ports);

    void stop() override;
    bool running() const override;
    void set_verbose(bool enabled) override;

private:
    LogFn log_;
    std::vector<std::unique_ptr<UdpProxy>> proxies_;
    std::shared_ptr<std::atomic_bool> verbose_ = std::make_shared<std::atomic_bool>(false);
};

}  // namespace civ6
