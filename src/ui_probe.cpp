// 离线验证按钮模板：在整屏截图里找模板，打印相关度和位置。
// 用法：
//   ui_probe <screen.png> <template.png> [x y w h] [min_score]
// 例：
//   ui_probe assets/screens/result.png assets/templates/btn_ok.png
//   ui_probe assets/screens/menu.png assets/templates/btn_battle.png 0 900 720 380
#include "png_io.h"
#include "template_match.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>
#include <vector>

int main() {
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv || argc < 3) {
        std::fprintf(stdout,
                     "usage: ui_probe <screen.png> <template.png> [x y w h] [min_score]\n"
                     "  roi 省略 = 全屏搜索\n");
        if (wargv) LocalFree(wargv);
        return 0;
    }

    std::string err;
    Image screen;
    if (!load_png(wargv[1], screen, &err)) {
        std::fprintf(stderr, "cannot load screen: %s\n", err.c_str());
        return 0;
    }
    Template tpl = Template::load(wargv[2], &err);
    if (!tpl.valid()) {
        std::fprintf(stderr, "cannot load template: %s\n", err.c_str());
        return 0;
    }

    Rect roi{};
    double min_score = 0.80;
    if (argc >= 7) {
        roi = Rect{std::stoi(wargv[3]), std::stoi(wargv[4]),
                   std::stoi(wargv[5]), std::stoi(wargv[6])};
    }
    if (argc >= 8) min_score = std::stod(wargv[7]);

    MatchResult m = match_template(screen, tpl, roi, min_score);

    std::printf("screen %dx%d  template %dx%d  roi %d,%d %dx%d\n", screen.width(),
                screen.height(), tpl.width(), tpl.height(), roi.x, roi.y, roi.w, roi.h);
    std::printf("found=%d score=%.4f center=(%d,%d) rect=(%d,%d %dx%d)\n",
                m.found ? 1 : 0, m.score, m.cx(), m.cy(), m.rect.x, m.rect.y, m.rect.w,
                m.rect.h);

    LocalFree(wargv);
    return 0;
}
