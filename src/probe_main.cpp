// 离线回归工具：对一整个目录的对局截图跑一遍圣水/手牌识别。
// 用法：
//   elixir_probe <图片或目录> [步长]
// 输出 CSV：file,visible,segments,fill,hand
#include "elixir.h"
#include "hand.h"
#include "png_io.h"

// windows.h 默认把 min/max 定义成宏，会和 std::max 打架
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static const char* card_char(CardState s) {
    switch (s) {
        case CardState::Ready: return "R";
        case CardState::Unaffordable: return "u";
        case CardState::Empty: return "-";
        default: return "?";
    }
}

// 没给参数时的默认帧目录：环境变量 CRBOT_FRAMES > 从 exe 目录往上找 frames 子目录
// > MuMu 默认共享目录
static fs::path default_root() {
    if (const wchar_t* env = _wgetenv(L"CRBOT_FRAMES"); env && *env) {
        return fs::path(env);
    }
    std::error_code ec;

    wchar_t buf[MAX_PATH] = {0};
    if (GetModuleFileNameW(nullptr, buf, MAX_PATH) != 0) {
        fs::path base = fs::path(buf).parent_path();
        while (!base.empty()) {
            fs::path cand = base / L"frames";
            if (fs::is_directory(cand, ec)) return cand;
            fs::path up = base.parent_path();
            if (up == base) break;
            base = up;
        }
    }

    if (const wchar_t* profile = _wgetenv(L"USERPROFILE"); profile && *profile) {
        fs::path cand = fs::path(profile) / L"Documents" / L"MuMu\u5171\u4eab\u6587\u4ef6\u5939" /
                        L"VideoRecords" / L"frames";
        if (fs::is_directory(cand, ec)) return cand;
    }

    // 本机 MuMu 把共享目录指到了 D:\file，帧就在下面这个位置。
    // 换机器时删掉这一段即可，或者用 CRBOT_FRAMES 环境变量覆盖。
    {
        fs::path cand = L"D:\\file\\Users\\nya\\Documents\\MuMu\u5171\u4eab\u6587\u4ef6\u5939"
                        L"\\VideoRecords\\frames";
        if (fs::is_directory(cand, ec)) return cand;
    }
    return {};
}

int main() {
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);

    // 参数优先级：命令行 > 环境变量 CRBOT_FRAMES > exe 所在目录往上找 frames 子目录
    // 目的是让 CLion 里直接点 Run 也能跑起来，而不是只打印一句用法就退出。
    fs::path root;
    int step = 1;
    if (wargv && argc >= 2) root = wargv[1];
    if (wargv && argc >= 3) step = std::max(1, std::stoi(std::wstring(wargv[2])));
    if (root.empty()) root = default_root();

    if (root.empty()) {
        std::fprintf(stdout,
                     "elixir_probe - 离线回归：对一目录的截图跑一遍圣水/手牌识别\n"
                     "\n"
                     "用法: elixir_probe <图片目录或单个png> [步长]\n"
                     "\n"
                     "没给参数时，会依次尝试：\n"
                     "  1. 环境变量 CRBOT_FRAMES\n"
                     "  2. exe 所在目录及其各级上级目录下的 frames\\\n"
                     "  3. %%USERPROFILE%%\\Documents\\MuMu共享文件夹\\VideoRecords\\frames\n"
                     "都找不到，就在 CLion 里 Run -> Edit Configurations ->\n"
                     "Program arguments 填上帧目录，例如：\n"
                     "  D:\\file\\Users\\nya\\Documents\\MuMu\xe5\x85\xb1\xe4\xba\xab\xe6\x96\x87\xe4\xbb\xb6\xe5\xa4\xb9"
                     "\\VideoRecords\\frames\n");
        if (wargv) LocalFree(wargv);
        return 0;  // 用法提示不算失败
    }

    std::vector<fs::path> files;
    std::error_code ec;
    if (fs::is_directory(root, ec)) {
        for (auto& e : fs::directory_iterator(root, ec)) {
            if (!e.is_regular_file()) continue;
            std::wstring ext = e.path().extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            if (ext == L".png") files.push_back(e.path());
        }
        std::sort(files.begin(), files.end());
    } else {
        files.push_back(root);
    }
    if (files.empty()) {
        std::fprintf(stderr, "no png found in %ls\n", root.c_str());
        if (wargv) LocalFree(wargv);
        return 0;
    }
    std::fprintf(stderr, "reading %zu png from %ls\n", files.size(), root.c_str());
    if (wargv) LocalFree(wargv);

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    long long total = 0, visible = 0;
    long long hist[11] = {0};
    long long ready_frames = 0;
    long long failed_reads = 0;

    for (size_t i = 0; i < files.size(); i += static_cast<size_t>(step)) {
        Image img;
        std::string err;
        if (!load_png(files[i].wstring(), img, &err)) {
            std::fprintf(stderr, "load failed: %ls (%s)\n", files[i].c_str(), err.c_str());
            ++failed_reads;
            continue;
        }

        ElixirBar bar = default_elixir_bar(img.width(), img.height());
        ElixirReading r = read_elixir(img, bar);
        HandGeometry hand = default_hand(img.width(), img.height());

        char hand_txt[5] = {'?', '?', '?', '?', 0};
        bool any_ready = false;
        for (int k = 0; k < 4; ++k) {
            CardState cs = read_card(img, hand.slots[k]);
            hand_txt[k] = card_char(cs)[0];
            if (cs == CardState::Ready) any_ready = true;
        }

        ++total;
        if (r.visible) {
            ++visible;
            hist[std::clamp(r.segments, 0, 10)]++;
            if (any_ready) ++ready_frames;
        }

        std::string name = files[i].filename().string();
        std::printf("%s,%d,%d,%.3f,%s\n", name.c_str(), r.visible ? 1 : 0, r.segments,
                    r.fill, hand_txt);
        if (total % 300 == 0) std::fprintf(stderr, "  ...%lld\n", total);
    }

    std::fprintf(stderr, "\nframes=%lld visible=%lld bad_reads=%lld\n", total, visible,
                 failed_reads);
    std::fprintf(stderr, "segment histogram:");
    for (int i = 0; i <= 10; ++i) std::fprintf(stderr, " %d:%lld", i, hist[i]);
    std::fprintf(stderr, "\nframes with a playable card: %lld / %lld\n", ready_frames, visible);

    CoUninitialize();
    return 0;
}
