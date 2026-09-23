#include "config.hpp"

#include <windows.h>

#include <fstream>

#include "../third_party/nlohmann/json.hpp"

using nlohmann::json;

namespace civ6 {

std::wstring config_path() {
    wchar_t exe[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring path(exe);
    std::size_t slash = path.find_last_of(L"\\/");
    std::wstring dir = (slash == std::wstring::npos) ? L"." : path.substr(0, slash);
    return dir + L"\\civ6proxy_config.json";
}

Config load_config() {
    Config cfg;
    std::ifstream in(config_path(), std::ios::binary);
    if (!in) return cfg;
    try {
        json j;
        in >> j;
        if (j.contains("mode") && j["mode"].is_string())        cfg.mode = j["mode"].get<std::string>();
        if (j.contains("server_vip") && j["server_vip"].is_string()) cfg.server_vip = j["server_vip"].get<std::string>();
        if (j.contains("client_ip") && j["client_ip"].is_string())   cfg.client_ip = j["client_ip"].get<std::string>();
        if (j.contains("peer_vips") && j["peer_vips"].is_string())    cfg.peer_vips = j["peer_vips"].get<std::string>();
        if (j.contains("ports") && j["ports"].is_string())      cfg.ports = j["ports"].get<std::string>();
    } catch (...) {
        // 损坏则退回默认（对齐 Python except (OSError, ValueError): pass）
        return Config{};
    }
    return cfg;
}

void save_config(const Config& cfg) {
    json j;
    j["mode"] = cfg.mode;
    j["server_vip"] = cfg.server_vip;
    j["client_ip"] = cfg.client_ip;
    j["peer_vips"] = cfg.peer_vips;
    j["ports"] = cfg.ports;
    std::ofstream out(config_path(), std::ios::binary);
    if (!out) return;
    out << j.dump(2);
}

}  // namespace civ6
