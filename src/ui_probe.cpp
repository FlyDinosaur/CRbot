// 离线验证按钮模板：在整屏截图里找模板，打印相关度和位置。
// 用法：
//   ui_probe <screen.png> <template.png> [x y w h] [min_score]
// 例：
//   ui_probe assets/screens/result.png assets/templates/btn_ok.png
//   ui_probe assets/screens/menu.png assets/templates/btn_battle.png 0 900 720 380
#include "png_io.h"
#include "template_match.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int run(const std::vector<fs::path>& args) {
    if (args.size() < 3) {
        std::fprintf(stdout,
                     "usage: ui_probe <screen.png> <template.png> [x y w h] [min_score]\n"
                     "  roi 省略 = 全屏搜索\n");
        return 0;
    }

    std::string err;
    Image screen;
    if (!load_png(args[1], screen, &err)) {
        std::fprintf(stderr, "cannot load screen: %s\n", err.c_str());
        return 0;
    }
    Template tpl = Template::load(args[2], &err);
    if (!tpl.valid()) {
        std::fprintf(stderr, "cannot load template: %s\n", err.c_str());
        return 0;
    }

    Rect roi{};
    double min_score = 0.80;
    if (args.size() >= 7) {
        roi = Rect{std::stoi(args[3].string()), std::stoi(args[4].string()),
                   std::stoi(args[5].string()), std::stoi(args[6].string())};
    }
    if (args.size() >= 8) min_score = std::stod(args[7].string());

    MatchResult m = match_template(screen, tpl, roi, min_score);

    std::printf("screen %dx%d  template %dx%d  roi %d,%d %dx%d\n", screen.width(),
                screen.height(), tpl.width(), tpl.height(), roi.x, roi.y, roi.w, roi.h);
    std::printf("found=%d score=%.4f center=(%d,%d) rect=(%d,%d %dx%d)\n",
                m.found ? 1 : 0, m.score, m.cx(), m.cy(), m.rect.x, m.rect.y, m.rect.w,
                m.rect.h);
    return 0;
}

}  // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    std::vector<fs::path> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    return run(args);
}
