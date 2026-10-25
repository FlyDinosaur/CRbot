# CRBot

**版本 0.0.1（pre-release）** · [更新日志](CHANGELOG.md)

《皇室战争》国服（`com.tencent.tmgp.supercell.clashroyale`）自动化机器人。
Windows 桌面程序，通过 ADB 驱动 MuMu/雷电等 720×1280 模拟器，自动循环打 1v1 对战。
图形界面（Win32）用来显示每台模拟器的状态，并在需要时人工介入。

## 现在能做什么

```
扫描 5555..5599 端口 → 连接所有模拟器 → 每台一个线程
   → 启动游戏 → 等登录完成（主界面出现"对战"按钮）
   → 进入"经典 1 对 1"
   → 无限循环：等待"对战" → 开始匹配 → [圣水识别 + 手牌识别 + 自动出牌] → 结算 → 回主界面
```

* ✅ 多开、端口扫描、设备状态 GUI、登录超时人工介入（已有功能）
* ✅ **圣水识别**：像素级读底部圣水条，0~10 整数格，<1ms/帧（本次新增）
* ✅ **手牌可打性识别**：读"卡牌被压暗"判断当前圣水够不够，不需要知道卡组和费用（本次新增）
* ✅ **对局主循环**：读圣水 → 选牌 → 点卡 → 落点 → 判结束（本次新增）
* ⚠️ 出牌策略是"节奏型"：
  * **前 117 秒**：攒到 **9 费**才下，只下可打的牌；
  * **117 秒后（双倍圣水）**：阈值降到 **8 费**，而且**允许提前下**——牌还是灰的也下，
    因为游戏在费用即将满足时会接受这一手，否则圣水会一直溢出；
  * 落点固定 **(360, 695)**（河边中央），卡槽按 **1 → 2 → 3 → 4** 依次轮转；
  * 出牌后要等「tap 的 adb 命令已返回」+「游戏处理完落点」才拍下一帧
    （`tap_gap_ms / tap_settle_ms`），并重置滤波，避免扣费还没落地就再点一张；

  没有敌方部队/塔血识别，所以还谈不上"会防守"。策略参数集中在 `BattleConfig`（`include/battler.h`）。

设计说明见 **[docs/圣水识别方案.md](docs/圣水识别方案.md)**（含 5 种方案的取舍和实测数据）。

## 编译

需要 Visual Studio（MSVC，C++23）和 CMake ≥ 3.20。
`adb.exe` 已经以二进制数组形式编进程序（`src/adb_resource.cpp`，所以首次全量编译会慢）。

```powershell
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug
```

产物：

| 目标 | 说明 |
|---|---|
| `CRbot.exe` | 主程序（Win32 GUI + 控制台日志） |
| `elixir_probe.exe` | 离线回归工具：对一个目录的截图跑一遍圣水/手牌识别 |
| `ui_probe.exe` | 离线工具：验证按钮模板在一个画面里的匹配分和位置 |

> 首次构建会花几分钟编译 `adb_resource.cpp`（6MB 的 adb.exe 十六进制数组）。
> 之后增量构建是秒级的。

## 打包预发布版

```powershell
pwsh -File tools\package.ps1
```

脚本会自己找 vcvars64 / CLion 自带的 cmake+ninja，构建 `RelWithDebInfo`，然后输出：

```
dist\CRBot-0.0.1-win64\         ← exe + assets/templates + 文档
dist\CRBot-0.0.1-win64.zip      ← 可直接分发
```

版本号只写在 `VERSION` 文件里，CMake 和打包脚本都从它读；改版本只要改那一个文件。

## 离线回归

```powershell
# 对录像拆出来的帧逐帧识别，输出 CSV：文件名,条可见,圣水格数,填充比例,手牌
.\cmake-build-debug\elixir_probe.exe "D:\...\VideoRecords\frames" > out.csv

# 只看每 10 帧
.\cmake-build-debug\elixir_probe.exe "D:\...\VideoRecords\frames" 10
```

不带参数直接运行也不会报错退出：它会依次找 `%CRBOT_FRAMES%`、exe 往上各层的 `frames\`、
`%USERPROFILE%\Documents\MuMu共享文件夹\VideoRecords\frames`；都找不到才打印用法（返回 0）。
在 CLion 里点 Run 想直接跑出结果，就把帧目录填进
**Run → Edit Configurations → Program arguments**（只需设置一次）。

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
  cr_game.*        进程/包名相关、等主界面
  bot_runner.*     单台设备的线程主流程 + Stop/Retry
  gui_dashboard.*  Win32 状态面板
  log_tee.*        控制台 + crbot.log 双写
  probe_main.cpp   离线回归工具入口（圣水/手牌）
  ui_probe.cpp     离线工具：验证按钮模板
assets/
  templates/       按钮模板（构建时复制到 exe 旁边）
  screens/         原始按钮截图（留档）
docs/
  圣水识别方案.md
tools/
  package.ps1      构建 + 打包预发布版
  *.py             量取几何、验证读数用的离线脚本
```

## 界面判定：像素模板，不用 OCR

主界面 / 结算页 / 菜单项全部靠模板匹配，**点击坐标一律取自匹配结果**（不是写死的）：

```
assets/templates/btn_battle.png      主界面黄色「对战」   搜索范围 x210..520 y900..1120
assets/templates/btn_battle2.png     同上（带"每日额外奖励"小字的外观）
assets/templates/btn_ok.png          结算页蓝色「确定」   搜索范围 x255..475 y1040..1180
assets/templates/btn_mode_1v1.png    模式菜单「经典1对1」 每次全屏重新找（列表会滚动）
```

* 一个按钮可以挂**多个外观模板**，取最高分；游戏里按钮外观会随账号状态/活动/版本变化；
* 「经典1对1」**不缓存**：模式列表是会滚动的，位置每次都变，所以每次都用全屏搜索
  （粗扫步长 8，约 0.2~0.5s），找不到就滑动列表再找；
* 模式菜单的开合按钮图标随当前模式变化、还会随版本改，没法做模板，只能按固定位置 (570,1000) 点；
* 阈值 `0.72 / 0.70 / 0.80`，见 `src/navigator.cpp` 顶部常量；
* 模板缺失时所有判定都会失败（没有 OCR 兜底），启动日志会打
  `[UI] button templates unavailable`，这是唯一需要留意的前置条件。
* 卡住时程序会存 `crbot_debug.png`（真实 720x1280 帧），遇到点不动的按钮就从中裁模板。

## 日志与调试产物

| 文件 | 内容 |
|---|---|
| `crbot.log` | 控制台的完整副本（UTF-8 BOM，逐块刷新，崩了也不丢） |
| `crbot_debug.png` | `finish_match` 卡住时存的现场帧 |
| `crbot_battle_<serial>.png` | 等对局开始超时时存的现场帧 |

截图走 **PNG**（约 80KB）而不是原始帧（3.7MB）：多开时瓶颈是 adb 通道，PNG 快好几倍；
解码失败会自动退回原始帧并打日志。单帧超过 2 秒会打 `slow screenshot` 警告。



## 真机上第一次跑，重点看这三件事

1. **启动日志里的 `[UI] loaded btn_*.png`**：三个模板都加载成功才说明素材就位。
   没有这几行 → `assets/templates` 没被复制到 exe 旁边，或文件缺失。
2. **`[BATTLE] play slot N at (360,695)`**：卡槽是否按 1→4 轮转、部队是否真的下到地上。
   偏了改 `BattleConfig::deploy_x / deploy_y`（`include/battler.h`）。
3. **`[NAV] tapping 经典1对1 at (x,y)`**：这一行出现才说明模式菜单选对了；
   如果是 `1v1 item not found; keeping current mode`，说明模板没匹配上，需要重新截图。

