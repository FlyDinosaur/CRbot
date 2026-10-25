# 更新日志

## 未发布 — macOS 移植

* **macOS 可原生编译运行**（Apple Silicon / Intel，clang + CMake），不再需要 MSVC。
* 新增平台层：`platform_*` 负责可执行文件目录与 adb 查找，`proc_*` 负责子进程与管道
  （Windows 用 `CreateProcess`，macOS 用 `posix_spawn` + `poll`）。两边都保留
  "不等管道 EOF、只等直接子进程退出"的逻辑，避免 adb 守护进程占着写端导致卡死。
* macOS 不内嵌 `adb.exe`，按 `CRBOT_ADB` > `PATH` > Android SDK / Homebrew 的顺序找系统 adb。
* PNG 解码在 macOS 上改用系统的 ImageIO（Windows 仍用 WIC）。
* 控制界面换成终端面板：设备状态 + 实时日志滚动区，按键 `s/r/w/d` 对应
  Stop / Retry / Wait / Disconnect，`q` 退出；没有终端时自动退化成行模式。
* 设备发现增加 `adb devices`，USB 真机和 Android Studio 的 `emulator-5554` 也能识别；
  端口扫描（5555~5599 等）保留给本地模拟器。
* 退出/Stop 现在会一起打断正在执行的 adb 命令，也会打断"等游戏启动"和"等主界面"的睡眠，
  不用再等满 60 秒。
* 修复 macOS 日志转发的两个问题：后台线程不能 detach 后随静态析构 terminate；
  控制台写入必须走 dup 出来的原始 stdout，否则日志会写回自己的管道形成死循环。
* 修复子进程管道 EOF 后 `poll` 一直返回 `POLLHUP` 导致的空转（29 个端口探测一度烧掉 7 秒 CPU）。
* 离线工具 `elixir_probe` / `ui_probe` 在两个平台都能编译；路径参数改用
  `std::filesystem::path`，以便同时支持 Windows 宽字符路径和 macOS 的 UTF-8 路径。

## 0.0.1 (pre-release) — 2026-10-25

首个可运行的预发布版：多开模拟器、自动打完一整局并循环。

**许可证：[GPL-3.0](LICENSE)**（强 copyleft：分发二进制时须同样开放源码）。
内嵌的 adb 按 Apache-2.0 分发，见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

### 对局内

* **圣水识别**：直接读底部圣水条（720x1280 实测 10 格 × 50px），
  单帧 <1ms；对比 2842 帧录像验证，读数与游戏大号数字逐帧一致。
* **手牌可打性**：读"卡牌被压暗"判断圣水够不够，**不需要知道卡组和费用**。
* **出牌策略**：前 117 秒 9 费出手；117 秒后（双倍圣水）降到 8 费，
  并允许"提前下"灰卡；卡槽 1→4 轮转，落点固定 (360,695)。
* 出牌后等「tap 命令返回 + 游戏处理完落点」才拍下一帧，避免连下两张。

### 对局外

* 与 OCR 完全解耦：主界面 / 结算页 / 模式菜单全部靠**按钮模板匹配**，
  点击坐标一律取自匹配结果。
* 一个按钮支持多个外观模板（例如「对战」按钮带不带"每日额外奖励"小字）。
* 结算后的每日奖励 / 开箱 / 赛季拼图按动画处理：只点空白处推进，绝不误触其它按钮。
* 多设备按 `android_id + serialno` 去重，避免多个线程抢同一块屏幕。

### 基础设施

* 常驻 `adb shell` 会话：`input tap` 从 ~150ms 降到 ~10ms。
* 截图走 PNG（~80KB）而非原始帧（3.7MB），多开时显著降低 adb 压力。
* 日志双写：控制台 + `crbot.log`（UTF-8，逐块刷新）。
* 卡住时自动存现场帧（`crbot_debug.png` / `crbot_battle_<serial>.png`）。
* GUI 每台设备一行：`Wait` / `Disconnect` / `Stop`（立即打断，连 adb 命令一起杀）/ `Retry`（整台重跑）。

### 已知限制

* 出牌没有防守逻辑（不识别敌方部队与塔血），只是节奏型压制。
* 分辨率固定 720x1280；其它分辨率需要改模板与几何。
* 模式菜单的开合按钮图标随模式变化，只能按固定位置点击。
