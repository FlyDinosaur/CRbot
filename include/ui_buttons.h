#pragma once
#include "template_match.h"
#include <string>
#include <vector>

// 需要点击的按钮。
enum class Btn { Battle, Mode1v1, Ok };

// 点击位置一律来自模板匹配的结果（match.cx()/cy()）。
// 同一个按钮可以挂多个模板：游戏里按钮外观会随账号状态/活动/版本变化，
// 例如主界面「对战」在"每日额外奖励"可领时会多一行小字（btn_battle2.png）。
class UiButtons {
public:
    // 全进程一份，第一次用到时懒加载。find() 是 const + 无状态，多设备线程可并发调用。
    static UiButtons& shared();

    bool load(std::string* err = nullptr);
    bool ready() const { return loaded_ > 0; }

    // 在该按钮的搜索范围内，对所有模板取最高分
    MatchResult find(const Image& img, Btn b, double min_score = 0.72) const;

private:
    struct Entry {
        std::vector<Template> tpls;  // 同一按钮的多个外观
        Rect roi;                    // 搜索范围（固定区域，与模板尺寸无关）
    };

    Entry entries_[3];
    int loaded_ = 0;
};
