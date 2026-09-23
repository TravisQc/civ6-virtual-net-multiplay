#include "icons.hpp"

#include <windows.h>

namespace civ6 {

IconRgba extract_icon_rgba(const std::wstring& exe_path, int size) {
    IconRgba out;
    if (exe_path.empty() || size <= 0) return out;

    HICON hLarge = nullptr, hSmall = nullptr;
    UINT count = ExtractIconExW(exe_path.c_str(), 0, &hLarge, &hSmall, 1);
    if (count == 0) return out;
    HICON hicon = hLarge ? hLarge : hSmall;  // 优先大图标做源，DrawIconEx 缩放到目标尺寸
    if (!hicon) {
        if (hLarge) DestroyIcon(hLarge);
        if (hSmall) DestroyIcon(hSmall);
        return out;
    }

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;  // 负值 = top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib && bits) {
        HGDIOBJ old = SelectObject(mem, dib);
        // 透明底 + 正常绘制（32 位带 alpha 的图标会正确混合）。
        DrawIconEx(mem, 0, 0, hicon, size, size, 0, nullptr, DI_NORMAL);
        GdiFlush();

        const int n = size * size;
        const auto* src = static_cast<const std::uint8_t*>(bits);  // BGRA
        out.pixels.resize(static_cast<std::size_t>(n) * 4);

        bool any_alpha = false;
        for (int i = 0; i < n; ++i)
            if (src[i * 4 + 3] != 0) { any_alpha = true; break; }

        for (int i = 0; i < n; ++i) {
            std::uint8_t b = src[i * 4 + 0];
            std::uint8_t g = src[i * 4 + 1];
            std::uint8_t r = src[i * 4 + 2];
            std::uint8_t a = any_alpha ? src[i * 4 + 3] : 255;  // 无 alpha 的旧图标视为不透明
            out.pixels[i * 4 + 0] = r;
            out.pixels[i * 4 + 1] = g;
            out.pixels[i * 4 + 2] = b;
            out.pixels[i * 4 + 3] = a;
        }
        out.width = size;
        out.height = size;

        SelectObject(mem, old);
    }

    if (dib) DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (hLarge) DestroyIcon(hLarge);
    if (hSmall) DestroyIcon(hSmall);
    return out;
}

}  // namespace civ6
