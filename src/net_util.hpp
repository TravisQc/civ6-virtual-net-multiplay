// 网络辅助：端口解析、本机 IP、UDP 进程枚举、IP 校验、绑定错误文案。
// 对应 proxy.py 顶部的辅助函数（用 Win32 IP Helper 替代 psutil）。
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace civ6 {

// 占用 UDP 端口的进程信息。
struct UdpProcess {
    std::string name;              // 进程名（拿不到则 "PID <pid>"）
    unsigned long pid = 0;
    std::vector<int> ports;        // 该进程占用的 UDP 端口（升序）
    std::wstring exe;              // exe 完整路径（用于提取图标；可能为空）
};

// 把 "62900, 62056"（支持中文逗号）解析为端口列表。
// 非法端口抛 std::invalid_argument / std::out_of_range，与 Python 抛 ValueError 对齐。
std::vector<int> parse_ports(const std::string& text);

// 校验 IPv4 字符串；无效抛 std::invalid_argument（label 用于组织错误文案）。
void validate_ip(const std::string& ip, const std::string& label);

// 将逗号或中文逗号分隔的 IP 字符串切分为去重且非空的 IP 列表。
std::vector<std::string> split_peer_vips(const std::string& text);

// 校验单条对端虚拟 IP。若有效返回空字符串并在 out_normalized_ip 输出规范化 IP；若无效返回中文错误文案。
std::string validate_peer_ip(const std::string& ip, std::string* out_normalized_ip = nullptr);

// 返回本机可能的局域网 IPv4（过滤 127.*，升序去重）。
std::vector<std::string> list_local_ips();

// 枚举当前占用 UDP 端口的进程，按进程聚合端口，按名称排序。
// 失败（IP Helper 调用出错）抛 std::runtime_error。
std::vector<UdpProcess> list_udp_processes();

// 端口绑定失败时的可读中文提示（对齐 _bind_error_message）。
// err 传 WSAGetLastError() 的值。
std::string bind_error_message(int port, int err);

}  // namespace civ6
