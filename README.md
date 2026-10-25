# CRBot

**版本 0.0.1（pre-release）** · [更新日志](CHANGELOG.md) · [圣水识别方案](docs/圣水识别方案.md)

《皇室战争》国服（`com.tencent.tmgp.supercell.clashroyale`）自动化机器人。
Windows 桌面程序，通过 ADB 驱动 MuMu / 雷电等 **720×1280** 模拟器，自动循环打 1v1 对战。
图形界面（Win32）显示每台模拟器的状态，需要时可人工介入。

## 现在能做什么

```
扫描模拟器端口 → 按设备身份去重 → 每台一个线程
   → 启动游戏 → 等主界面（"对战"按钮出现）
   → 选「经典 1 对 1」
   → 无限循环：开匹配 → [读圣水 + 读手牌 + 自动出牌] → 结算/领奖 → 回主界面
```

* ✅ **多开**：扫描 5555~5599（步长 2）+ 16384/16416/7555/21503/62001/62025，
  按 `android_id + serialno` 去重（同一个模拟器会同时监听好几个端口）
* ✅ **圣水识别**：像素级读底部圣水条，0~10 整数格，单帧 <1ms
* ✅ **手牌可打性**：读"卡牌被压暗"判断圣水够不够，**不需要知道卡组和费用**
* ✅ **对局循环**：读圣水 → 选牌 → 点卡 → 落点 → 判结束 → 结算 → 下一局
* ✅ **界面判定全像素化**：主界面 / 结算页 / 模式菜单靠按钮模板匹配，**不用 OCR**
* ✅ **多开稳定**：常驻 adb shell、PNG 截图、日志落盘、卡住可 Stop/Retry

出牌策略（`BattleConfig`，见 `include/battler.h`）：

| 阶段 | 圣水阈值 | 选牌 |
|---|---|---|
| 前 117 秒 | **9 费** | 只下亮着的（`Ready`） |
| 117 秒后（双倍圣水） | **8 费** | 优先亮着的；没有就"提前下"灰卡，避免圣水溢出 |

落点固定 **(360, 695)**（河边中央），卡槽 **1 → 2 → 3 → 4** 轮转。
没有敌方部队/塔血识别，所以还谈不上"会防守"。

## 编译

需要 Visual Studio（MSVC，C++23）和 CMake ≥ 3.20。
`adb.exe` 已经以二进制数组形式编进程序（`src/adb_resource.cpp`，所以**首次全量编译会慢几分钟**）。

```powershell
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug
```

| 目标 | 说明 |
|---|---|
| `CRbot.exe` | 主程序（Win32 GUI） |
| `elixir_probe.exe` | 离线回归：对一个目录的截图跑圣水/手牌识别 |
| `ui_probe.exe` | 离线工具：验证按钮模板在一个画面里的匹配分和位置 |

> Debug 版会弹控制台；Release 版不弹，日志只写 `crbot.log`（见下文）。

## 打包预发布版

```powershell
# Windows PowerShell 或 pwsh 都行（脚本带 BOM，5.1 也能读）
powershell -File tools\package.ps1

# 已经用 CLion 构建过，只打包
powershell -File tools\package.ps1 -SkipBuild -BuildDir .\build-release
```

脚本会自己找 `vcvars64.bat` 和 CLion 自带的 cmake+ninja，构建 `RelWithDebInfo`，输出：

```
dist\CRBot-0.0.1-win64\
    CRbot.exe
    assets\templates\btn_*.png      ← 4 个模板
    assets\README.md
    docs\圣水识别方案.md
    README.md  CHANGELOG.md  VERSION
dist\CRBot-0.0.1-win64.zip
```

版本号只写在 `VERSION` 文件里，CMake 和打包脚本都从它读；发新版只改那一个文件。

## 离线回归

```powershell
# 对录像拆出来的帧逐帧识别，输出 CSV：文件名,条可见,圣水格数,填充比例,手牌
.\cmake-build-debug\elixir_probe.exe "D:\...\VideoRecords\frames" > out.csv

# 只看每 10 帧
.\cmake-build-debug\elixir_probe.exe "D:\...\VideoRecords\frames" 10
```

不带参数也不会报错退出：依次找 `%CRBOT_FRAMES%`、exe 往上各层的 `frames\`、
`%USERPROFILE%\Documents\MuMu共享文件夹\VideoRecords\frames`；都找不到才打印用法（返回 0）。
在 CLion 里想直接跑，把帧目录填进 **Run → Edit Configurations → Program arguments**。

按钮模板也可以用 `ui_probe` 单独验证：

```powershell
.\cmake-build-debug\ui_probe.exe assets\screens\result2.png assets\templates\btn_ok.png
# → found=1 score=1.0000 center=(361,1120) rect=(274,1084 175x73)
```

## 目录

```
include/ src/
  adb_client.*     adb 调用（connect / screencap / tap / swipe / shell）
                   常驻 shell 会话、可被打断的子进程、adb 提取
  adb_resource.*   内嵌的 adb.exe
  device.*         设备连接 + 分辨率校验（必须 720x1280）+ 截图（PNG 优先）
  image.*          RGBA 帧、screencap 解析、PNG 落盘（调试用）
  template_match.* ★ 归一化相关（NCC）模板匹配，粗扫 + 精修
  ui_buttons.*     ★ 按钮模板组、搜索范围、匹配
  elixir.*         ★ 圣水条几何 + 读数（纯函数）
  hand.*           ★ 4 个卡位的可打性（纯函数）
  battler.*        ★ 对局中循环：读圣水、读手牌、出牌（不认识 GUI）
  navigator.*      对局外：主界面导航、start_match、finish_match
  cr_game.*        进程/包名、启动游戏、等主界面
  bot_runner.*     单台设备的线程主流程 + Stop/Retry
  gui_dashboard.*  Win32 状态面板
  log_tee.*        控制台 + crbot.log 双写
  probe_main.cpp   离线回归工具入口（圣水/手牌）
  ui_probe.cpp     离线工具：验证按钮模板
assets/
  templates/       按钮模板（构建时复制到 exe 旁边）
  screens/         原始按钮截图（留档，补模板时用）
docs/
  圣水识别方案.md   设计说明：5 种圣水方案取舍 + 实测数据
tools/
  package.ps1      构建 + 打包预发布版
  *.py             量取几何、验证读数用的离线脚本
```

## 界面判定：像素模板，不用 OCR

主界面 / 结算页 / 菜单项全部靠模板匹配，**点击坐标一律取自匹配结果**：

```
btn_battle.png      主界面黄色「对战」   搜索范围 x210..520 y900..1120
btn_battle2.png     同上，带"每日额外奖励"小字的外观
btn_ok.png          结算页蓝色「确定」   搜索范围 x255..475 y1040..1180
btn_mode_1v1.png    模式菜单「经典1对1」 每次全屏重新找（列表会滚动）
```

* 一个按钮可以挂**多个外观模板**，取最高分——按钮外观会随账号状态/活动/版本变化；
* 「经典1对1」**不缓存**：模式列表会滚动，位置每次都变，所以每次全屏搜索
  （粗扫步长 8，约 0.2~0.5s），找不到就滑动列表再找；
* 模式菜单的开合按钮图标随当前模式变、还会随版本改，没法做模板，只能按固定位置 (570,1000) 点；
* 阈值 `0.72 / 0.70 / 0.80`，见 `src/navigator.cpp` 顶部常量；
* 模板缺失时所有判定都会失败，日志会打 `[UI] button templates unavailable`——
  这是唯一需要留意的前置条件。

## 日志与调试产物

| 文件 | 内容 |
|---|---|
| `crbot.log` | 完整日志（UTF-8 BOM，逐块刷新，崩了也不丢） |
| `crbot_debug.png` | 结算流程卡住时存的现场帧 |
| `crbot_battle_<serial>.png` | 等对局开始超时时存的现场帧 |

* **Debug**：弹控制台 + 写 `crbot.log`；
* **Release**：**不弹控制台**（`NDEBUG` 下跳过 `AllocConsole`），日志只落 `crbot.log`。
  不想要文件日志就删掉 `main.cpp` 里 `start_log_tee("crbot.log")` 那一行。

截图走 **PNG**（约 80KB）而不是原始帧（3.7MB）：多开时瓶颈是 adb 通道，PNG 快好几倍；
解码失败会自动退回原始帧。单帧超过 2 秒会打 `slow screenshot` 警告。

## 卡住时怎么办

GUI 每台设备一行，右侧按钮：

| 按钮 | 作用 |
|---|---|
| `Wait` | 登录超时等人工确认时出现，继续等 |
| `Disconnect` | 同上，断开这台设备 |
| `Stop` | **立即打断**：停线程 + 杀掉正在执行的 adb 命令，不等 60 秒超时 |
| `Retry` | 从"连设备"那一步**整台重跑**（复用启动流程） |

按钮只登记请求，真正的 stop/join/start 由主进程里的监视线程执行——
界面线程从不阻塞，所以机器人卡在 adb 调用里时窗口也不会假死。

## 真机上第一次跑，重点看这几行日志

1. `[CRBOT] version 0.0.1` + 4 行 `[UI] loaded btn_*.png` —— 素材就位；
2. `[NAV] battle button at (365,1000) score 0.9x, tapping` —— 主界面识别到了；
   没生效会自动 `tap did not take effect, retry 1/3`；
3. `[NAV] 经典1对1 at (358,973) score 0.9x` —— 模式菜单选对了；
4. `[BATTLE] play slot N at (360,695)` + `[BATTLE] frame avg ...ms` —— 出牌与帧耗时。

## 已知限制

* 只支持 720×1280；换分辨率要改模板和 `elixir.cpp` / `hand.cpp` 里的几何；
* 出牌是节奏型，不防守、不针对卡组；
* 赛季专属活动页是纯动画，只能靠点空白处推进；
* 详见 [CHANGELOG.md](CHANGELOG.md) 的"已知限制"。
