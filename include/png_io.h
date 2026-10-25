#pragma once
#include "image.h"

#include <filesystem>
#include <string>
#include <vector>

// 读 PNG 成 RGBA。
//   Windows: Windows Imaging Component (WIC)
//   macOS:   ImageIO + CoreGraphics
// 路径统一用 std::filesystem::path（Windows 上仍然支持中文路径）。
bool load_png(const std::filesystem::path& path, Image& out, std::string* err = nullptr);

// 从内存里的 PNG 解码（主程序截图用：PNG 比原始帧小 30~40 倍）
bool load_png_memory(const std::vector<uint8_t>& png, Image& out,
                     std::string* err = nullptr);
