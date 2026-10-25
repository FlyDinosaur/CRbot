#pragma once
#include "image.h"
#include <string>
#include <vector>

// 用 Windows Imaging Component 读 PNG
bool load_png(const std::wstring& path, Image& out, std::string* err = nullptr);

// 从内存里的 PNG 解码（主程序截图用：PNG 比原始帧小 30~40 倍）
bool load_png_memory(const std::vector<uint8_t>& png, Image& out,
                     std::string* err = nullptr);
