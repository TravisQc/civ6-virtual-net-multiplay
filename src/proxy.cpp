#include "proxy.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include "net_util.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace civ6 {

namespace {
constexpr int kBufferSize = 1 << 12;  // 4096，与 Python BUFFER_SIZE 一致
}

UdpProxy::UdpProxy(int listen_port, std::string server_vip, std::string client_ip,
                   LogFn log, std::shared_ptr<std::atomic_bool> verbose)
    : listen_port_(listen_port),
      server_vip_(std::move(server_vip)),
      client_ip_(std::move(client_ip)),
      log_(std::move(log)),
      verbose_(std::move(verbose)) {}

UdpProxy::~UdpProxy() { stop(); }

void UdpProxy::start() {
    stop_.store(false);
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET)
        throw std::runtime_error(bind_error_message(listen_port_, WSAGetLastError()));

    BOOL yes = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&yes), sizeof(yes));
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<unsigned short>(listen_port_));
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        closesocket(sock);
        throw std::runtime_error(bind_error_message(listen_port_, err));
    }

    // 500ms 接收超时，以便定期检查停止标志（对齐 settimeout(0.5)）。
    DWORD timeout = 500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));

    sock_ = static_cast<unsigned long long>(sock);
    thread_ = std::thread([this] { loop(); });

    log_(LogLevel::Info, "端口 " + std::to_string(listen_port_) + " 代理已启动 (客户端 " +
                             client_ip_ + " <-> 服务端 " + server_vip_ + ")");
}

void UdpProxy::loop() {
    SOCKET sock = static_cast<SOCKET>(sock_);
    std::vector<char> buf(kBufferSize);
    while (!stop_.load()) {
        sockaddr_in from{};
        int fromlen = sizeof(from);
        int n = recvfrom(sock, buf.data(), kBufferSize, 0,
                         reinterpret_cast<sockaddr*>(&from), &fromlen);
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) continue;
            break;  // 套接字被关闭或其它错误
        }
        char ipbuf[INET_ADDRSTRLEN] = {0};
        InetNtopA(AF_INET, &from.sin_addr, ipbuf, sizeof(ipbuf));
        std::string ip(ipbuf);
        unsigned short port = ntohs(from.sin_port);

        if (ip == client_ip_) {
            client_port_ = port;
            ++to_server_count_;
            if (verbose_->load())
                log_(LogLevel::Debug, "[" + std::to_string(listen_port_) + "] 客户端 -> 服务端 (" +
                                          std::to_string(n) + "B)");
            safe_send(buf.data(), n, server_vip_, static_cast<unsigned short>(listen_port_));
            maybe_warn_no_server();
        } else if (ip == server_vip_) {
            if (!server_seen_) {
                server_seen_ = true;
                log_(LogLevel::Info, "[" + std::to_string(listen_port_) + "] 已收到服务端 " +
                                         server_vip_ + " 的响应，双向通道就绪");
            }
            if (client_port_) {
                if (verbose_->load())
                    log_(LogLevel::Debug, "[" + std::to_string(listen_port_) + "] 服务端 -> 客户端 (" +
                                              std::to_string(n) + "B)");
                safe_send(buf.data(), n, client_ip_, client_port_);
            } else {
                log_(LogLevel::Warn, "[" + std::to_string(listen_port_) +
                                         "] 收到服务端数据但尚未收到客户端数据包，暂无法回传");
            }
        } else {
            log_(LogLevel::Warn, "[" + std::to_string(listen_port_) + "] 未知来源 " + ip + ":" +
                                     std::to_string(port) + "，已忽略");
        }
    }
}

void UdpProxy::maybe_warn_no_server() {
    if (server_seen_ || warned_no_server_) return;
    if (to_server_count_ >= 20) {
        warned_no_server_ = true;
        log_(LogLevel::Warn,
             "[" + std::to_string(listen_port_) + "] 已向服务端 " + server_vip_ + " 转发 " +
                 std::to_string(to_server_count_) +
                 " 个数据包但一直没有回应，请检查：服务端是否已加入虚拟局域网并开启游戏、"
                 "虚拟 IP 是否填写正确、防火墙是否放行该端口。");
    }
}

void UdpProxy::safe_send(const char* data, int len, const std::string& ip, unsigned short port) {
    SOCKET sock = static_cast<SOCKET>(sock_);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    InetPtonA(AF_INET, ip.c_str(), &to.sin_addr);
    if (sendto(sock, data, len, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == SOCKET_ERROR) {
        log_(LogLevel::Error, "[" + std::to_string(listen_port_) + "] 转发到 " + ip + ":" +
                                  std::to_string(port) + " 失败 (WinError " +
                                  std::to_string(WSAGetLastError()) + ")");
    }
}

void UdpProxy::stop() {
    stop_.store(true);
    if (sock_ != ~0ull) {
        closesocket(static_cast<SOCKET>(sock_));
        sock_ = ~0ull;
    }
    if (thread_.joinable()) thread_.join();
}

bool UdpProxy::running() const { return thread_.joinable(); }

// --------------------------------------------------------------------------

ProxyManager::ProxyManager(LogFn log) : log_(std::move(log)) {}
ProxyManager::~ProxyManager() { stop(); }

void ProxyManager::set_verbose(bool enabled) { verbose_->store(enabled); }

void ProxyManager::start(const std::string& server_vip, const std::string& client_ip,
                         const std::vector<int>& ports) {
    if (running()) throw std::runtime_error("代理已在运行中");
    validate_ip(server_vip, "服务端虚拟 IP");
    validate_ip(client_ip, "客户端真实 IP");
    if (ports.empty()) throw std::invalid_argument("至少需要一个端口");

    std::vector<std::unique_ptr<UdpProxy>> started;
    for (int port : ports) {
        auto proxy = std::make_unique<UdpProxy>(port, server_vip, client_ip, log_, verbose_);
        try {
            proxy->start();
        } catch (...) {
            for (auto& p : started) p->stop();  // 回滚已启动的部分
            throw;
        }
        started.push_back(std::move(proxy));
    }
    proxies_ = std::move(started);
}

void ProxyManager::stop() {
    for (auto& p : proxies_) p->stop();
    proxies_.clear();
}

bool ProxyManager::running() const {
    for (const auto& p : proxies_)
        if (p->running()) return true;
    return false;
}

}  // namespace civ6
