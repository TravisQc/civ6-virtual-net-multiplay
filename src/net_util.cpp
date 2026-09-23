#include "net_util.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <map>
#include <set>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace civ6 {

namespace {

// 把字符串里的中文逗号「，」替换为半角逗号，并按逗号切分、去空白。
std::vector<std::string> split_commas(const std::string& text) {
    // UTF-8 的「，」是 3 字节 E3 80 81? 实际为 EF BC 8C。逐字节替换成 ',' 前先归一化。
    std::string s;
    s.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == 0xEF && i + 2 < text.size() &&
            static_cast<unsigned char>(text[i + 1]) == 0xBC &&
            static_cast<unsigned char>(text[i + 2]) == 0x8C) {
            s.push_back(',');
            i += 3;
        } else {
            s.push_back(text[i]);
            ++i;
        }
    }
    std::vector<std::string> tokens;
    std::string cur;
    for (char ch : s) {
        if (ch == ',') {
            tokens.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    tokens.push_back(cur);
    for (auto& t : tokens) {
        std::size_t b = t.find_first_not_of(" \t\r\n");
        std::size_t e = t.find_last_not_of(" \t\r\n");
        t = (b == std::string::npos) ? std::string{} : t.substr(b, e - b + 1);
    }
    return tokens;
}

std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

}  // namespace

std::vector<int> parse_ports(const std::string& text) {
    std::vector<int> ports;
    for (const auto& tok : split_commas(text)) {
        if (tok.empty()) continue;
        // 仅接受纯数字（避免 std::stoi 吃掉 "62900abc"）。
        for (char ch : tok) {
            if (!std::isdigit(static_cast<unsigned char>(ch)))
                throw std::invalid_argument("端口不是数字: " + tok);
        }
        long port = 0;
        try {
            port = std::stol(tok);
        } catch (...) {
            throw std::invalid_argument("端口不是数字: " + tok);
        }
        if (!(port > 0 && port < 65536))
            throw std::out_of_range("端口超出范围: " + tok);
        ports.push_back(static_cast<int>(port));
    }
    return ports;
}

void validate_ip(const std::string& ip_in, const std::string& label) {
    std::string ip = ip_in;
    std::size_t b = ip.find_first_not_of(" \t\r\n");
    std::size_t e = ip.find_last_not_of(" \t\r\n");
    ip = (b == std::string::npos) ? std::string{} : ip.substr(b, e - b + 1);
    if (ip.empty())
        throw std::invalid_argument(label + " 不能为空");
    in_addr addr{};
    if (InetPtonA(AF_INET, ip.c_str(), &addr) != 1)
        throw std::invalid_argument(label + " 格式无效: " + ip);
}

std::vector<std::string> list_local_ips() {
    std::set<std::string> ips;

    char hostname[256] = {0};
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        addrinfo* res = nullptr;
        if (getaddrinfo(hostname, nullptr, &hints, &res) == 0) {
            for (addrinfo* p = res; p; p = p->ai_next) {
                auto* sa = reinterpret_cast<sockaddr_in*>(p->ai_addr);
                char buf[INET_ADDRSTRLEN] = {0};
                InetNtopA(AF_INET, &sa->sin_addr, buf, sizeof(buf));
                ips.insert(buf);
            }
            freeaddrinfo(res);
        }
    }

    // connect-probe 取默认出口 IP（不实际发包）。
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s != INVALID_SOCKET) {
        sockaddr_in probe{};
        probe.sin_family = AF_INET;
        probe.sin_port = htons(80);
        InetPtonA(AF_INET, "8.8.8.8", &probe.sin_addr);
        if (connect(s, reinterpret_cast<sockaddr*>(&probe), sizeof(probe)) == 0) {
            sockaddr_in local{};
            int len = sizeof(local);
            if (getsockname(s, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
                char buf[INET_ADDRSTRLEN] = {0};
                InetNtopA(AF_INET, &local.sin_addr, buf, sizeof(buf));
                ips.insert(buf);
            }
        }
        closesocket(s);
    }

    std::vector<std::string> out;
    for (const auto& ip : ips) {
        if (ip.rfind("127.", 0) == 0) continue;
        out.push_back(ip);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<UdpProcess> list_udp_processes() {
    // pid -> 端口集合
    std::map<DWORD, std::set<int>> aggregated;

    ULONG size = 0;
    DWORD ret = GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET,
                                    UDP_TABLE_OWNER_PID, 0);
    if (ret != ERROR_INSUFFICIENT_BUFFER && ret != NO_ERROR)
        throw std::runtime_error("枚举 UDP 端口失败 (GetExtendedUdpTable)");

    std::vector<char> buffer(size);
    ret = GetExtendedUdpTable(buffer.data(), &size, FALSE, AF_INET,
                              UDP_TABLE_OWNER_PID, 0);
    if (ret != NO_ERROR)
        throw std::runtime_error("枚举 UDP 端口失败 (GetExtendedUdpTable)");

    auto* table = reinterpret_cast<MIB_UDPTABLE_OWNER_PID*>(buffer.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& row = table->table[i];
        int port = static_cast<int>(ntohs(static_cast<u_short>(row.dwLocalPort & 0xFFFF)));
        aggregated[row.dwOwningPid].insert(port);
    }

    std::vector<UdpProcess> result;
    for (const auto& [pid, ports] : aggregated) {
        UdpProcess proc;
        proc.pid = pid;
        proc.name = "PID " + std::to_string(pid);
        for (int p : ports) proc.ports.push_back(p);

        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h) {
            wchar_t path[MAX_PATH] = {0};
            DWORD len = MAX_PATH;
            if (QueryFullProcessImageNameW(h, 0, path, &len)) {
                proc.exe = path;
                std::wstring wpath(path);
                std::size_t slash = wpath.find_last_of(L"\\/");
                std::wstring wname = (slash == std::wstring::npos) ? wpath : wpath.substr(slash + 1);
                proc.name = wide_to_utf8(wname);
            }
            CloseHandle(h);
        }
        result.push_back(std::move(proc));
    }

    std::sort(result.begin(), result.end(), [](const UdpProcess& a, const UdpProcess& b) {
        std::string la = a.name, lb = b.name;
        std::transform(la.begin(), la.end(), la.begin(), [](unsigned char c) { return std::tolower(c); });
        std::transform(lb.begin(), lb.end(), lb.begin(), [](unsigned char c) { return std::tolower(c); });
        return la < lb;
    });
    return result;
}

std::string bind_error_message(int port, int err) {
    const std::string p = std::to_string(port);
    if (err == 10013) {  // WSAEACCES
        return "无法绑定端口 " + p + "（WinError 10013 拒绝访问）。\n\n"
               "这个端口通常已被本机其它程序占用。如果它是用「从程序选择」选来的，\n"
               "说明那个程序此刻正在使用该端口，代理无法再绑定同一个端口。\n\n"
               "正确用法：在【不运行游戏】的中转设备上运行本代理，端口用文明6默认的 62900,62056；\n"
               "如果一定要在本机运行，请先关闭占用该端口的程序。";
    }
    if (err == 10048) {  // WSAEADDRINUSE
        return "端口 " + p + " 已被占用（WinError 10048），请关闭占用它的程序或更换端口。";
    }
    return "端口 " + p + " 绑定失败，可能已被占用或需要权限 (WinError " + std::to_string(err) + ")";
}

}  // namespace civ6
