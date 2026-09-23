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
        if (j.contains("dark_mode") && j["dark_mode"].is_boolean()) cfg.dark_mode = j["dark_mode"].get<bool>();
        if (j.contains("window_x") && j["window_x"].is_number_integer())      cfg.window_x = j["window_x"].get<int>();
        if (j.contains("window_y") && j["window_y"].is_number_integer())      cfg.window_y = j["window_y"].get<int>();
        if (j.contains("window_width") && j["window_width"].is_number_integer())   cfg.window_width = j["window_width"].get<int>();
        if (j.contains("window_height") && j["window_height"].is_number_integer()) cfg.window_height = j["window_height"].get<int>();
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
    j["dark_mode"] = cfg.dark_mode;
    j["window_x"] = cfg.window_x;
    j["window_y"] = cfg.window_y;
    j["window_width"] = cfg.window_width;
    j["window_height"] = cfg.window_height;
    std::ofstream out(config_path(), std::ios::binary);
    if (!out) return;
    out << j.dump(2);
}

}  // namespace civ6
