#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 一帧 RGBA 截图（每像素 4 字节：R,G,B,A）
class Image {
public:
    Image() = default;
    Image(int w, int h) : w_(w), h_(h), px_(static_cast<size_t>(w) * h * 4, 0) {}
    Image(int w, int h, std::vector<uint8_t> rgba)
        : w_(w), h_(h), px_(std::move(rgba)) {
        if (px_.size() != static_cast<size_t>(w) * h * 4) {
            w_ = h_ = 0;
            px_.clear();
        }
    }

    // 解析 `adb exec-out screencap` 的原始输出：
    // 4B 宽 + 4B 高 + 4B 格式(1=RGBA_8888) + 宽*高*4 像素
    static Image from_screencap(const std::vector<uint8_t>& raw, std::string* err = nullptr);

    int width() const { return w_; }
    int height() const { return h_; }
    bool empty() const { return px_.empty(); }

    const uint8_t* at(int x, int y) const {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return nullptr;
        return &px_[(static_cast<size_t>(y) * w_ + x) * 4];
    }
    uint8_t* at(int x, int y) {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return nullptr;
        return &px_[(static_cast<size_t>(y) * w_ + x) * 4];
    }

    // 写入 PNG（无压缩 deflate，方便调试时把画面/ROI 存盘）
    bool save_png(const std::string& path) const;

private:
    int w_ = 0;
    int h_ = 0;
    std::vector<uint8_t> px_;
};
