// 同机模式（WinDivert）：在网络栈底层拦截文明6 的 LAN 发现广播，
// 不绑定端口，可与正在运行的游戏共存。把捕获到的广播单播转发给虚拟网内各对端。
// 对应 divert.py 的 DivertForwarder。需管理员权限，仅 Windows。
#pragma once

#include <atomic>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "engine.hpp"
#include "log_bus.hpp"
#include "windivert_api.hpp"

namespace civ6 {

class DivertForwarder : public Engine {
public:
    DivertForwarder(std::vector<int> ports, std::vector<std::string> peer_vips, LogFn log);
    ~DivertForwarder() override;

    // 校验（管理员/对端/ZeroTier 路由）后打开 WinDivert 并起线程。
    // 失败抛异常（PermissionError 等价 -> std::runtime_error，文案可读）。
    void start();

    void stop() override;
    bool running() const override;
    void set_verbose(bool enabled) override;

private:
    void loop();

    std::vector<int> ports_;
    std::vector<std::string> peer_vips_;   // 已去重
    LogFn log_;
    std::atomic_bool verbose_{false};

    std::map<std::string, std::string> sources_;  // peer -> 本机源地址(ZeroTier)
    WinDivertApi api_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::thread thread_;
    std::atomic_bool stop_{false};
};

// 以下辅助导出，便于单测（对齐 divert.py 的自由函数）。

// 本机所有接口的广播地址集合（含 255.255.255.255）。
std::vector<std::string> broadcast_addresses();

// 询问 Windows 到 peer 的源地址（connect UDP 后 getsockname，不发包）。
std::string route_source(const std::string& peer);

// 名字含 "zerotier" 的接口的 IPv4 地址集合。
std::vector<std::string> zerotier_ips();

// 生成 WinDivert 过滤器：outbound and ip and udp and (端口...) and (广播地址...)。
std::string discovery_filter(const std::vector<int>& ports,
                             const std::vector<std::string>& broadcasts);

}  // namespace civ6
