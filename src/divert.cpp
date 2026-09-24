#include "divert.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>

#include "net_util.hpp"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace civ6 {

namespace {
constexpr int kDiscoveryPort = 62900;

std::string ipv4_to_string(UINT32 addr_net_order) {
    in_addr a{};
    a.s_addr = addr_net_order;
    char buf[INET_ADDRSTRLEN] = {0};
    InetNtopA(AF_INET, &a, buf, sizeof(buf));
    return buf;
}
}  // namespace

std::vector<std::string> broadcast_addresses() {
    std::set<std::string> addrs{"255.255.255.255"};

    ULONG size = 0;
    GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &size);
    std::vector<char> buffer(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapters, &size) != NO_ERROR)
        return {addrs.begin(), addrs.end()};

    for (auto* ad = adapters; ad; ad = ad->Next) {
        for (auto* ua = ad->FirstUnicastAddress; ua; ua = ua->Next) {
            if (ua->Address.lpSockaddr->sa_family != AF_INET) continue;
            auto* sa = reinterpret_cast<sockaddr_in*>(ua->Address.lpSockaddr);
            UINT32 ip = sa->sin_addr.s_addr;  // 网络序
            UINT8 prefix = ua->OnLinkPrefixLength;
            if (prefix > 32) continue;
            // 主机序算广播，再转回网络序展示。
            UINT32 ip_host = ntohl(ip);
            UINT32 mask_host = (prefix == 0) ? 0 : (0xFFFFFFFFu << (32 - prefix));
            UINT32 bcast_host = ip_host | (~mask_host);
            addrs.insert(ipv4_to_string(htonl(bcast_host)));
        }
    }
    return {addrs.begin(), addrs.end()};
}

std::string route_source(const std::string& peer) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) throw std::runtime_error("创建探测套接字失败");
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(kDiscoveryPort);
    InetPtonA(AF_INET, peer.c_str(), &to.sin_addr);
    std::string result;
    if (connect(s, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0) {
        sockaddr_in local{};
        int len = sizeof(local);
        if (getsockname(s, reinterpret_cast<sockaddr*>(&local), &len) == 0)
            result = ipv4_to_string(local.sin_addr.s_addr);
    }
    closesocket(s);
    if (result.empty()) throw std::runtime_error("无法确定到 " + peer + " 的源地址");
    return result;
}

std::vector<std::string> zerotier_ips() {
    std::set<std::string> ips;
    ULONG size = 0;
    GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &size);
    std::vector<char> buffer(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapters, &size) != NO_ERROR)
        return {};
    for (auto* ad = adapters; ad; ad = ad->Next) {
        std::wstring wname = ad->FriendlyName ? ad->FriendlyName : L"";
        std::wstring wdesc = ad->Description ? ad->Description : L"";
        std::wstring both = wname + L"\x01" + wdesc;
        std::wstring lower;
        for (wchar_t c : both) lower.push_back(static_cast<wchar_t>(towlower(c)));
        if (lower.find(L"zerotier") == std::wstring::npos) continue;
        for (auto* ua = ad->FirstUnicastAddress; ua; ua = ua->Next) {
            if (ua->Address.lpSockaddr->sa_family != AF_INET) continue;
            auto* sa = reinterpret_cast<sockaddr_in*>(ua->Address.lpSockaddr);
            ips.insert(ipv4_to_string(sa->sin_addr.s_addr));
        }
    }
    return {ips.begin(), ips.end()};
}

std::string discovery_filter(const std::vector<int>& ports,
                             const std::vector<std::string>& broadcasts) {
    std::set<int> uports(ports.begin(), ports.end());
    std::set<std::string> ubcasts(broadcasts.begin(), broadcasts.end());

    std::ostringstream port_expr, addr_expr;
    bool first = true;
    for (int p : uports) {
        if (!first) port_expr << " or ";
        port_expr << "udp.DstPort == " << p;
        first = false;
    }
    first = true;
    for (const auto& ip : ubcasts) {
        if (!first) addr_expr << " or ";
        addr_expr << "ip.DstAddr == " << ip;
        first = false;
    }
    return "outbound and ip and udp and (" + port_expr.str() + ") and (" + addr_expr.str() + ")";
}

// --------------------------------------------------------------------------

DivertForwarder::DivertForwarder(std::vector<int> ports, std::vector<std::string> peer_vips,
                                 LogFn log)
    : ports_(std::move(ports)), log_(std::move(log)) {
    // 去重，保序（对齐 dict.fromkeys）。
    std::set<std::string> seen;
    for (auto& v : peer_vips) {
        std::string t = v;
        std::size_t b = t.find_first_not_of(" \t\r\n");
        std::size_t e = t.find_last_not_of(" \t\r\n");
        t = (b == std::string::npos) ? std::string{} : t.substr(b, e - b + 1);
        if (t.empty() || seen.count(t)) continue;
        seen.insert(t);
        peer_vips_.push_back(t);
    }
}

DivertForwarder::~DivertForwarder() { stop(); }

void DivertForwarder::set_verbose(bool enabled) { verbose_.store(enabled); }

void DivertForwarder::start() {
    if (running()) throw std::runtime_error("代理已在运行");
    if (!is_admin()) throw std::runtime_error("请以管理员身份运行修正版。");
    if (peer_vips_.empty() || ports_.empty())
        throw std::invalid_argument("请填写其他玩家的虚拟 IP 和发现端口。");

    std::vector<std::string> vips = zerotier_ips();
    std::set<std::string> vip_set(vips.begin(), vips.end());

    for (const auto& peer : peer_vips_) {
        // 排除组播/未指定/环回（等价 ipaddress 校验）。
        in_addr a{};
        if (InetPtonA(AF_INET, peer.c_str(), &a) != 1)
            throw std::invalid_argument("不是可用的玩家地址：" + peer);
        UINT32 h = ntohl(a.s_addr);
        bool multicast = (h >= 0xE0000000u && h <= 0xEFFFFFFFu);
        bool loopback = ((h >> 24) == 127);
        bool unspecified = (h == 0);
        if (multicast || unspecified || loopback)
            throw std::invalid_argument("不是可用的玩家地址：" + peer);

        std::string source = route_source(peer);
        if (source == peer)
            throw std::invalid_argument("对端列表中包含本机地址：" + peer);
        // 源地址不是 ZeroTier 网卡地址时不再阻断启动：ZeroTier 托管路由（Managed Route）等场景下，
        // 到对端的转发可经物理网卡源地址走 ZeroTier 并正常送达。仅记一条警告以便排查误填。
        if (!vip_set.count(source))
            log_(LogLevel::Warn, "到 " + peer + " 的路由源地址为 " + source +
                                     "，不是 ZeroTier 网卡地址；若已配置 ZeroTier 托管路由可忽略，"
                                     "否则请检查对端虚拟 IP 是否填错。");
        sources_[peer] = source;
    }

    api_.load(log_);
    std::string filter = discovery_filter(ports_, broadcast_addresses());
    handle_ = api_.Open(filter.c_str(), WINDIVERT_LAYER_NETWORK, 0, 0);
    if (handle_ == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        throw std::runtime_error(
            "打开 WinDivert 失败 (WinError " + std::to_string(err) +
            ")。\n\n请确认以管理员身份运行，且杀毒软件未拦截 WinDivert 驱动。");
    }

    stop_.store(false);
    thread_ = std::thread([this] { loop(); });

    log_(LogLevel::Info, "v1.2 修正版：保留游戏源端口；发现广播仅发给指定 ZeroTier 玩家。");
    for (const auto& [peer, source] : sources_)
        log_(LogLevel::Info, "发现路径：" + source + " -> " + peer + "；游戏单播保持直连");
}

void DivertForwarder::loop() {
    std::vector<char> buf(65535);
    while (!stop_.load()) {
        WINDIVERT_ADDRESS addr{};
        UINT recv_len = 0;
        if (!api_.Recv(handle_, buf.data(), static_cast<UINT>(buf.size()), &recv_len, &addr)) {
            if (!stop_.load())
                log_(LogLevel::Error, "发现转发已中止，请停止后重启 (WinError " +
                                          std::to_string(GetLastError()) + ")");
            break;
        }
        for (const auto& [peer, source] : sources_) {
            // 就地改写源/目的地址（保留游戏 UDP 源端口），重算校验和后发送。
            WINDIVERT_IPHDR* ip = nullptr;
            UINT8 protocol = 0;
            api_.ParsePacket(buf.data(), recv_len, &ip, nullptr, &protocol, nullptr, nullptr,
                             nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
            if (!ip) continue;

            in_addr sa{}, da{};
            InetPtonA(AF_INET, source.c_str(), &sa);
            InetPtonA(AF_INET, peer.c_str(), &da);
            ip->SrcAddr = sa.s_addr;
            ip->DstAddr = da.s_addr;

            api_.CalcChecksums(buf.data(), recv_len, &addr, 0);
            UINT send_len = 0;
            if (!api_.Send(handle_, buf.data(), recv_len, &send_len, &addr)) {
                log_(LogLevel::Error, "发现转发失败 " + peer + " (WinError " +
                                          std::to_string(GetLastError()) + ")");
            } else if (verbose_.load()) {
                log_(LogLevel::Debug, "保留源端口 " + source + " -> " + peer + " (" +
                                          std::to_string(recv_len) + "B)");
            }
        }
        // 有意不回注物理局域网广播，避免同路由对端优先选到物理地址。
    }
}

void DivertForwarder::stop() {
    stop_.store(true);
    if (handle_ != INVALID_HANDLE_VALUE && api_.Close) {
        api_.Close(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    if (thread_.joinable()) thread_.join();
    if (log_) log_(LogLevel::Info, "修正版代理已停止，恢复正常局域网发现。");
}

bool DivertForwarder::running() const { return thread_.joinable(); }

}  // namespace civ6
