// 核心逻辑单测（无需管理员/驱动）：极简 assert 框架。
// 覆盖 parse_ports / validate_ip / bind 错误文案 / ProxyManager 启停与校验。
#include <winsock2.h>
#include <windows.h>

#include <cstdio>
#include <stdexcept>
#include <string>

#include "net_util.hpp"
#include "proxy.hpp"

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);        \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

template <typename Fn>
static bool throws(Fn fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

static void test_parse_ports() {
    std::printf("test_parse_ports\n");
    auto a = civ6::parse_ports("62900, 62056");
    CHECK(a.size() == 2 && a[0] == 62900 && a[1] == 62056);

    // 中文逗号
    auto b = civ6::parse_ports("62900，62056");
    CHECK(b.size() == 2 && b[0] == 62900 && b[1] == 62056);

    // 空 token 跳过
    auto c = civ6::parse_ports("62900,,");
    CHECK(c.size() == 1 && c[0] == 62900);

    CHECK(throws([] { civ6::parse_ports("70000"); }));   // 超范围
    CHECK(throws([] { civ6::parse_ports("0"); }));        // 超范围
    CHECK(throws([] { civ6::parse_ports("abc"); }));      // 非数字
}

static void test_validate_ip() {
    std::printf("test_validate_ip\n");
    civ6::validate_ip("192.168.1.1", "x");  // 不抛
    CHECK(throws([] { civ6::validate_ip("", "x"); }));
    CHECK(throws([] { civ6::validate_ip("999.1.1.1", "x"); }));
    CHECK(throws([] { civ6::validate_ip("not-an-ip", "x"); }));
}

static void test_bind_error_message() {
    std::printf("test_bind_error_message\n");
    CHECK(civ6::bind_error_message(62900, 10013).find("10013") != std::string::npos);
    CHECK(civ6::bind_error_message(62900, 10048).find("10048") != std::string::npos);
    CHECK(!civ6::bind_error_message(62900, 12345).empty());
}

static void test_proxy_lifecycle() {
    std::printf("test_proxy_lifecycle\n");
    civ6::ProxyManager mgr([](civ6::LogLevel, const std::string&) {});
    // 用一个不常用端口，验证绑定+线程+停止的生命周期。
    mgr.start("127.0.0.2", "127.0.0.1", {54329});
    CHECK(mgr.running());
    mgr.stop();
    CHECK(!mgr.running());

    // 校验：非法 IP 应抛。
    civ6::ProxyManager bad([](civ6::LogLevel, const std::string&) {});
    CHECK(throws([&] { bad.start("bad-ip", "127.0.0.1", {54330}); }));
    // 校验：空端口应抛。
    CHECK(throws([&] { bad.start("127.0.0.2", "127.0.0.1", {}); }));
}

int main() {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

    test_parse_ports();
    test_validate_ip();
    test_bind_error_message();
    test_proxy_lifecycle();

    WSACleanup();
    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
