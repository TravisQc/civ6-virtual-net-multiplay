// 配置读写：JSON 文件放在 exe 同目录（civ6proxy_config.json）。
// 对应 app.py 的 load_config / save_config / DEFAULT_CONFIG。
#pragma once

#include <string>

namespace civ6 {

struct Config {
    std::string mode = "windivert";       // "windivert"（同机，需管理员）| "relay"（独立中转机）
    std::string server_vip = "172.16.0.223";
    std::string client_ip = "10.0.0.10";
    std::string peer_vips = "";
    std::string ports = "62900";
};

// 返回 exe 同目录下的 civ6proxy_config.json 完整路径。
std::wstring config_path();

// 读取配置；缺失/损坏时返回默认值（对齐 Python 的容错）。
Config load_config();

// 保存配置；失败静默（对齐 Python）。
void save_config(const Config& cfg);

}  // namespace civ6
