// 从 Windows exe 提取图标为 RGBA 像素缓冲（供上层转成 slint::Image）。
// 对应 icons.py，但省去 PNG/zlib：Slint 直接吃像素缓冲。失败返回空(width==0)。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace civ6 {

struct IconRgba {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;  // RGBA8，行优先，top-down
    bool empty() const { return width == 0 || height == 0 || pixels.empty(); }
};

// 提取 exe 图标并缩放到 size×size。任何失败/非 Windows 返回空。
IconRgba extract_icon_rgba(const std::wstring& exe_path, int size = 32);

}  // namespace civ6
