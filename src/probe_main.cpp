// 离线回归工具：对一整个目录的对局截图跑一遍圣水/手牌识别。
// 用法：
//   elixir_probe <图片或目录> [步长]
// 输出 CSV：file,visible,segments,fill,hand
#include "elixir.h"
#include "hand.h"
#include "platform.h"
#include "png_io.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

const char* card_char(CardState s) {
    switch (s) {
        case CardState::Ready: return "R";
        case CardState::Unaffordable: return "u";
        case CardState::Empty: return "-";
        default: return "?";
    }
}

// 没给参数时的默认帧目录：环境变量 CRBOT_FRAMES > 从 exe 目录往上找 frames 子目录
// > MuMu 默认共享目录（仅 Windows）
fs::path default_root() {
#if defined(_WIN32)
    if (const wchar_t* env = _wgetenv(L"CRBOT_FRAMES"); env && *env) return fs::path(env);
#else
    if (const char* env = std::getenv("CRBOT_FRAMES"); env && *env) return fs::path(env);
#endif
    std::error_code ec;

    fs::path base = platform::exe_dir();
    while (!base.empty()) {
        const fs::path cand = base / "frames";
        if (fs::is_directory(cand, ec)) return cand;
        fs::path up = base.parent_path();
        if (up == base) break;
        base = up;
    }

#if defined(_WIN32)
    if (const wchar_t* profile = _wgetenv(L"USERPROFILE"); profile && *profile) {
        const fs::path cand = fs::path(profile) / L"Documents" / L"MuMu共享文件夹" /
                              L"VideoRecords" / L"frames";
        if (fs::is_directory(cand, ec)) return cand;
    }
#endif
    return {};
}

int run(const std::vector<fs::path>& args) {
    // 参数优先级：命令行 > 环境变量 CRBOT_FRAMES > exe 所在目录往上找 frames 子目录
    // 目的是让 IDE 里直接点 Run 也能跑起来，而不是只打印一句用法就退出。
    fs::path root;
    int step = 1;
    if (args.size() >= 2) root = args[1];
    if (args.size() >= 3) step = std::max(1, std::stoi(args[2].string()));
    if (root.empty()) root = default_root();

    if (root.empty()) {
        std::fprintf(stdout,
                     "elixir_probe - 离线回归：对一目录的截图跑一遍圣水/手牌识别\n"
                     "\n"
                     "用法: elixir_probe <图片目录或单个png> [步长]\n"
                     "\n"
                     "没给参数时，会依次尝试：\n"
                     "  1. 环境变量 CRBOT_FRAMES\n"
                     "  2. exe 所在目录及其各级上级目录下的 frames/\n"
                     "  3. %%USERPROFILE%%\\Documents\\MuMu共享文件夹\\VideoRecords\\frames"
                     "（仅 Windows）\n");
        return 0;  // 用法提示不算失败
    }

    std::vector<fs::path> files;
    std::error_code ec;
    if (fs::is_directory(root, ec)) {
        for (auto& e : fs::directory_iterator(root, ec)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".png") files.push_back(e.path());
        }
        std::sort(files.begin(), files.end());
    } else {
        files.push_back(root);
    }
    if (files.empty()) {
        std::fprintf(stderr, "no png found in %s\n", root.string().c_str());
        return 0;
    }
    std::fprintf(stderr, "reading %zu png from %s\n", files.size(), root.string().c_str());

#if defined(_WIN32)
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif

    long long total = 0, visible = 0;
    long long hist[11] = {0};
    long long ready_frames = 0;
    long long failed_reads = 0;

    for (size_t i = 0; i < files.size(); i += static_cast<size_t>(step)) {
        Image img;
        std::string err;
        if (!load_png(files[i], img, &err)) {
            std::fprintf(stderr, "load failed: %s (%s)\n", files[i].string().c_str(),
                         err.c_str());
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

#if defined(_WIN32)
    CoUninitialize();
#endif
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
