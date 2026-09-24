#include "config.hpp"

#include <windows.h>
#include <shlobj.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "../third_party/nlohmann/json.hpp"

using nlohmann::json;

namespace civ6 {

namespace {

// 可变配置目录：用户可写的 %APPDATA%\civ6proxy
// （安装目录位于 Program Files 下，对普通权限只读，故配置迁出程序目录）。
std::wstring config_dir() {
    std::wstring dir;
    wchar_t buf[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf)))
        dir = buf;
    if (dir.empty()) {
        // 兜底：环境变量 APPDATA。
        wchar_t* env = nullptr;
        std::size_t len = 0;
        if (_wdupenv_s(&env, &len, L"APPDATA") == 0 && env) {
            dir = env;
            free(env);
        }
    }
    if (dir.empty()) dir = L".";
    return dir + L"\\civ6proxy";
}

}  // namespace

std::wstring config_path() {
    return config_dir() + L"\\civ6proxy_config.json";
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
    std::error_code ec;
    std::filesystem::create_directories(config_dir(), ec);  // 确保 %APPDATA%\civ6proxy 存在
    std::ofstream out(config_path(), std::ios::binary);
    if (!out) return;
    out << j.dump(2);
}

}  // namespace civ6
