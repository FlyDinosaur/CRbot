cmake_minimum_required(VERSION 3.20)

# 版本号单一来源：VERSION 文件
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CRBOT_VERSION LIMIT_COUNT 1)
string(STRIP "${CRBOT_VERSION}" CRBOT_VERSION)

project(CRbot VERSION ${CRBOT_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options("/utf-8")
endif()

# ---- 平台相关部分 ----
# Windows: Win32 进程/管道 + WIC 解码 + Win32 窗口 + 内嵌 adb.exe
# macOS:   POSIX 进程/管道 + ImageIO 解码 + 终端面板 + 系统 adb
if(WIN32)
    set(CRBOT_PLATFORM_SRC
            src/platform_win.cpp
            src/proc_win.cpp
            src/png_io_wic.cpp)
    set(CRBOT_APP_SRC
            src/main.cpp
            src/gui_dashboard.cpp
            src/adb_resolve_win.cpp
            src/adb_resource.cpp)
    set(CRBOT_LINK_LIBS comctl32 windowscodecs ole32)
    add_compile_definitions(UNICODE _UNICODE)
elseif(APPLE)
    set(CRBOT_PLATFORM_SRC
            src/platform_posix.cpp
            src/proc_posix.cpp
            src/png_io_imageio.cpp)
    set(CRBOT_APP_SRC
            src/main.cpp
            src/tui_dashboard.cpp)
    # ImageIO 解 PNG，CoreGraphics 转 RGBA，CoreFoundation 传文件 / 内存数据
    set(CRBOT_LINK_LIBS
            "-framework ImageIO"
            "-framework CoreGraphics"
            "-framework CoreFoundation")
else()
    message(FATAL_ERROR "CRBot 目前只支持 Windows 和 macOS")
endif()

# 与平台无关的核心逻辑
set(CRBOT_CORE_SRC
        src/adb_client.cpp
        src/device.cpp
        src/image.cpp
        src/template_match.cpp
        src/ui_buttons.cpp
        src/elixir.cpp
        src/hand.cpp
        src/log_tee.cpp
        src/battler.cpp
        src/cr_game.cpp
        src/bot_runner.cpp
        src/navigator.cpp)

# 探针工具也要解 PNG，但不需要 adb / GUI
if(WIN32)
    set(CRBOT_PROBE_SRC src/png_io_wic.cpp src/platform_win.cpp)
    set(CRBOT_PROBE_LIBS windowscodecs ole32 shell32)
else()
    set(CRBOT_PROBE_SRC src/png_io_imageio.cpp src/platform_posix.cpp)
    set(CRBOT_PROBE_LIBS ${CRBOT_LINK_LIBS})
endif()

if(WIN32)
    add_executable(CRbot WIN32 ${CRBOT_APP_SRC} ${CRBOT_CORE_SRC} ${CRBOT_PLATFORM_SRC})
else()
    add_executable(CRbot ${CRBOT_APP_SRC} ${CRBOT_CORE_SRC} ${CRBOT_PLATFORM_SRC})
endif()

target_include_directories(CRbot PRIVATE include)
target_link_libraries(CRbot PRIVATE ${CRBOT_LINK_LIBS})
target_compile_definitions(CRbot PRIVATE
        CRBOT_VERSION="${CRBOT_VERSION}"
        CRBOT_VERSION_W=L"${CRBOT_VERSION}"
        # 开发时找不到 exe 旁边的 assets 就退回源码树
        CRBOT_SOURCE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/templates")

# 离线回归工具：对录像帧跑一遍圣水/手牌识别，用来验证算法
add_executable(elixir_probe
        src/probe_main.cpp
        src/image.cpp
        ${CRBOT_PROBE_SRC}
        src/elixir.cpp
        src/hand.cpp)
target_include_directories(elixir_probe PRIVATE include)
target_link_libraries(elixir_probe PRIVATE ${CRBOT_PROBE_LIBS})

# 离线验证按钮模板：ui_probe <screen.png> <template.png> [x y w h]
add_executable(ui_probe
        src/ui_probe.cpp
        src/image.cpp
        ${CRBOT_PROBE_SRC}
        src/template_match.cpp)
target_include_directories(ui_probe PRIVATE include)
target_link_libraries(ui_probe PRIVATE ${CRBOT_PROBE_LIBS})

if(APPLE)
    target_compile_options(CRbot PRIVATE -Wall -Wextra)
    target_compile_options(elixir_probe PRIVATE -Wall -Wextra)
    target_compile_options(ui_probe PRIVATE -Wall -Wextra)
endif()

# 模板随构建产物一起放到 exe 旁边，运行时按 exe 目录找
add_custom_command(TARGET CRbot POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates
                $<TARGET_FILE_DIR:CRbot>/assets/templates
        COMMENT "copying button templates")
