#include "ui_buttons.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

namespace {

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n,
                        nullptr, nullptr);
    return s;
}

std::wstring exe_assets_dir() {
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    fs::path exe_dir = fs::path(buf).parent_path();
    for (const wchar_t* rel : {L"\\assets\\templates", L"\\..\\assets\\templates"}) {
        fs::path cand = exe_dir.wstring() + rel;
        std::error_code ec;
        if (fs::is_directory(cand, ec)) return cand.wstring();
    }
    return {};
}

// 每个按钮：一组外观模板 + 搜索范围（720x1280 实测，与模板尺寸无关）
struct Spec {
    Btn btn;
    const wchar_t* files[4];  // 最多 4 个外观，nullptr 结束
    Rect roi;
};

const Spec kSpecs[] = {
    // 主界面底部黄色「对战」：普通版 / 带"每日额外奖励"版
    {Btn::Battle,
     {L"btn_battle.png", L"btn_battle2.png", nullptr, nullptr},
     Rect{210, 900, 310, 220}},

    // 模式菜单里的「经典1对1」：位置随滚动变化 → 全屏搜
    {Btn::Mode1v1, {L"btn_mode_1v1.png", nullptr, nullptr, nullptr}, Rect{}},

    // 结算页蓝色「确定」
    {Btn::Ok, {L"btn_ok.png", nullptr, nullptr, nullptr}, Rect{255, 1040, 220, 140}},
};

}  // namespace

UiButtons& UiButtons::shared() {
    static UiButtons inst = [] {
        UiButtons b;
        std::string err;
        if (!b.load(&err)) {
            std::fprintf(stderr, "[UI] button templates unavailable: %s\n", err.c_str());
        }
        return b;
    }();
    return inst;
}

bool UiButtons::load(std::string* err) {
    const std::wstring dir = exe_assets_dir();
    if (dir.empty()) {
        if (err) *err = "assets/templates not found next to exe";
        return false;
    }

    for (const Spec& s : kSpecs) {
        Entry& e = entries_[static_cast<int>(s.btn)];
        e.roi = s.roi;

        for (const wchar_t* f : s.files) {
            if (!f) break;
            std::string perr;
            Template t = Template::load(dir + L"\\" + f, &perr);
            if (!t.valid()) {
                std::fprintf(stderr, "[UI] cannot load %s: %s\n", narrow(f).c_str(),
                             perr.c_str());
                continue;
            }
            std::fprintf(stderr, "[UI] loaded %s (%dx%d)\n", narrow(f).c_str(), t.width(),
                         t.height());
            e.tpls.push_back(std::move(t));
            ++loaded_;
        }
    }
    return loaded_ > 0;
}

MatchResult UiButtons::find(const Image& img, Btn b, double min_score) const {
    MatchResult best;
    const Entry& e = entries_[static_cast<int>(b)];
    if (img.empty() || e.tpls.empty()) return best;

    for (const Template& tpl : e.tpls) {
        MatchResult m = match_template(img, tpl, e.roi, 0.0);
        if (m.score > best.score) best = m;
    }
    best.found = best.score >= min_score;
    return best;
}
