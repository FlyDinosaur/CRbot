# 第三方组件声明

本程序（CRBot）整体按 **GPL-3.0** 发布，见 [LICENSE](LICENSE)。
下面这个第三方组件是按**另一份兼容许可证**分发的，保留其原始许可与版权声明。

---

## adb（Android SDK Platform-Tools）

本程序内嵌并随包分发 Android SDK Platform-Tools 的 `adb`：

* 编译进程序的副本：`src/adb_resource.cpp`（十六进制数组，运行时释放到临时目录）
* 独立副本：`tools/adb.exe`

| 项目 | 内容 |
|---|---|
| 版权 | Copyright (C) 2007-2024 The Android Open Source Project |
| 许可证 | Apache License, Version 2.0 — <https://www.apache.org/licenses/LICENSE-2.0> |
| 源码 | <https://android.googlesource.com/platform/packages/modules/adb/> |

Apache-2.0 与 GPL-3.0 兼容（Apache-2.0 第 4 节、GPL-3.0 第 7 节），本程序把 adb 作为
独立可执行文件调用，不修改它；adb 的许可与版权声明在本文件中保留，不因本项目整体适用
GPL-3.0 而失效。

> macOS 版**不内嵌** adb：运行时使用系统里已安装的 `adb`（例如
> `brew install --cask android-platform-tools`），本仓库不附带也不分发 adb 二进制。
> 上面这些声明是对 Windows 版内嵌副本的说明，仍然有效。
