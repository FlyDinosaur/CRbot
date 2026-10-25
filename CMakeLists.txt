cmake_minimum_required(VERSION 3.20)

# 版本号单一来源：VERSION 文件
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CRBOT_VERSION LIMIT_COUNT 1)
string(STRIP "${CRBOT_VERSION}" CRBOT_VERSION)

project(CRbot VERSION ${CRBOT_VERSION} LANGUAGES CXX)

if(MSVC)
    add_compile_options("/utf-8")
endif()

add_compile_definitions(UNICODE _UNICODE)

set(CMAKE_CXX_STANDARD 23)

add_executable(CRbot WIN32
        src/main.cpp
        src/adb_client.cpp
        src/adb_resource.cpp
        src/device.cpp
        src/image.cpp
        src/png_io.cpp
        src/template_match.cpp
        src/ui_buttons.cpp
        src/elixir.cpp
        src/hand.cpp
        src/battler.cpp
        src/cr_game.cpp
        src/bot_runner.cpp
        src/gui_dashboard.cpp
        src/navigator.cpp
        src/log_tee.cpp
)

target_include_directories(CRbot PRIVATE include)
target_link_libraries(CRbot comctl32 windowscodecs ole32)
target_compile_definitions(CRbot PRIVATE
        CRBOT_VERSION="${CRBOT_VERSION}"
        CRBOT_VERSION_W=L"${CRBOT_VERSION}")

# 离线回归工具：对录像帧跑一遍圣水/手牌识别，用来验证算法
add_executable(elixir_probe
        src/probe_main.cpp
        src/png_io.cpp
        src/image.cpp
        src/elixir.cpp
        src/hand.cpp
)
target_include_directories(elixir_probe PRIVATE include)
target_link_libraries(elixir_probe windowscodecs ole32 shell32)

# 离线验证按钮模板：ui_probe <screen.png> <template.png> [x y w h]
add_executable(ui_probe
        src/ui_probe.cpp
        src/png_io.cpp
        src/image.cpp
        src/template_match.cpp
)
target_include_directories(ui_probe PRIVATE include)
target_link_libraries(ui_probe windowscodecs ole32 shell32)

# 模板随构建产物一起放到 exe 旁边，运行时按 exe 目录找
add_custom_command(TARGET CRbot POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates
                $<TARGET_FILE_DIR:CRbot>/assets/templates
        COMMENT "copying button templates")
