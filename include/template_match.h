#pragma once
#include "image.h"
#include <filesystem>
#include <string>
#include <vector>

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool valid() const { return w > 0 && h > 0; }
    int cx() const { return x + w / 2; }
    int cy() const { return y + h / 2; }
};

// 一块按钮模板（灰度 + 尺寸），只在加载时算一次统计量
class Template {
public:
    static Template load(const std::filesystem::path& png, std::string* err = nullptr);
    static Template from_image(const Image& img);

    int width() const { return w_; }
    int height() const { return h_; }
    bool valid() const { return w_ > 0 && h_ > 0 && !gray_.empty(); }

    const std::vector<uint8_t>& gray() const { return gray_; }

    // 采样步长：模板越大抽得越稀，保证单次匹配在 100ms 量级
    int sample_step() const { return (w_ * h_ > 12000) ? 3 : ((w_ * h_ > 3000) ? 2 : 1); }

private:
    int w_ = 0, h_ = 0;
    std::vector<uint8_t> gray_;
};

struct MatchResult {
    bool found = false;
    Rect rect;           // 命中的屏幕区域（左上角 + 模板尺寸）
    double score = 0.0;  // 归一化相关系数，1.0 = 完全一致

    int cx() const { return rect.cx(); }
    int cy() const { return rect.cy(); }
};

// 在 img 的 roi 区域内找 tpl。roi 传 {} 表示全屏。
// 先用大步长粗扫，再在最优位置附近逐像素精修。
MatchResult match_template(const Image& img, const Template& tpl, Rect roi,
                           double min_score = 0.80);
