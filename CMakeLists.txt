cmake_minimum_required(VERSION 3.21)
project(civ6proxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "本项目仅支持 Windows（依赖 Winsock / WinDivert / Win32 图标）。")
endif()

# ---------------------------------------------------------------- Slint
# 从源码构建 Slint C++（需 Rust 工具链，MSVC target）。
# 也可改用官方预编译包 + find_package(Slint)（更快），见 README-cpp.md。
include(FetchContent)
FetchContent_Declare(
    Slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    GIT_TAG        v1.18.1
    SOURCE_SUBDIR  api/cpp
)
# 后端/渲染器保持 Slint v1.18.1 默认（winit 后端 + FemtoVG + Software 渲染器；
# Skia/Qt/Vello 默认不编入），无需显式开启。
# 关闭本项目未使用的子系统，避免把死代码编进 slint_cpp.dll（见 openspec/changes/harden-release-binaries）。
set(SLINT_FEATURE_INTERPRETER   OFF CACHE BOOL "" FORCE)  # 用 AOT 编译（slint_target_sources），运行时不需要解释器
set(SLINT_FEATURE_TESTING       OFF CACHE BOOL "" FORCE)  # 未使用实验测试 API
set(SLINT_FEATURE_SYSTEM_TRAY   OFF CACHE BOOL "" FORCE)  # UI 无托盘图标
set(SLINT_FEATURE_ACCESSIBILITY OFF CACHE BOOL "" FORCE)  # 目标用户无需读屏支持

# 发布加固（防逆向 + 瘦身）：对 slint_cpp 的 Rust release profile 施加
# strip 符号 + fat LTO + 单 codegen 单元（panic=abort 由 Slint 自带 release profile 提供）。
# 经独立 cargo 配置文件用 --config 注入，避免内联引号在 MSBuild 下被吞（见 design.md D2）。
file(WRITE "${CMAKE_BINARY_DIR}/cargo-hardening.toml"
"# 由 CMake 生成，请勿手改。见 openspec/changes/harden-release-binaries。\n[profile.release]\nstrip = \"symbols\"\nlto = \"fat\"\ncodegen-units = 1\n")
set(SLINT_LIBRARY_CARGO_FLAGS "--config;${CMAKE_BINARY_DIR}/cargo-hardening.toml" CACHE STRING "" FORCE)

FetchContent_MakeAvailable(Slint)

# 把开发者主目录与项目路径从所有 crate 的嵌入路径（panic 字符串里的 .cargo\registry、
# 源文件路径）中抹掉 —— 去掉用户名/绝对路径泄露。作用于 slint_cpp 及其全部 Rust 依赖（见 design.md D3）。
file(TO_NATIVE_PATH "$ENV{USERPROFILE}" _civ6_home_native)
file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}" _civ6_root_native)
string(REPLACE "\\" "/" _civ6_home_unix "${_civ6_home_native}")
string(REPLACE "\\" "/" _civ6_root_unix "${_civ6_root_native}")
corrosion_add_target_rustflags(slint_cpp
    "--remap-path-prefix=${_civ6_home_native}=~"
    "--remap-path-prefix=${_civ6_home_unix}=~"
    "--remap-path-prefix=${_civ6_root_native}=."
    "--remap-path-prefix=${_civ6_root_unix}=.")

# ---------------------------------------------------------------- 程序图标
# 由 ui/assets/app_icon.png 生成的 res/app_icon.ico 通过 .rc 嵌入 exe，
# 使可执行文件 / 快捷方式 / 卸载项显示应用图标（Slint 的窗口图标是运行时另行设置）。
set(CIV6_APP_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/res/app_icon.ico")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/res/app.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)

# ---------------------------------------------------------------- 目标
add_executable(civ6proxy
    src/main.cpp
    src/app_controller.cpp
    src/proxy.cpp
    src/divert.cpp
    src/windivert_api.cpp
    src/net_util.cpp
    src/icons.cpp
    src/config.cpp
    src/win_chrome.cpp
    app.manifest        # MSVC 会自动嵌入清单（DPI/兼容性；UAC 级别见下方 /MANIFESTUAC）
    ${CMAKE_CURRENT_BINARY_DIR}/app.rc  # 程序图标资源（见上方 configure_file 生成）
)

target_include_directories(civ6proxy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# 编译 .slint -> 生成 app.h（含 AppWindow / ProcessPickerWindow / LogLine / ProcRow）。
# 默认使用 Fluent 风格。
slint_target_sources(civ6proxy ui/app.slint)
target_compile_definitions(civ6proxy PRIVATE UNICODE _UNICODE)

target_link_libraries(civ6proxy PRIVATE
    Slint::Slint
    ws2_32 iphlpapi shell32 user32 gdi32 advapi32 dwmapi comctl32
)

# 同机 WinDivert 模式需要管理员；开发/自动化 UI 测试时可 -DCIV6_REQUIRE_ADMIN=OFF 以普通权限运行。
option(CIV6_REQUIRE_ADMIN "exe 清单要求管理员权限 (requireAdministrator)" ON)

# GUI 子系统（无控制台），但保留 int main() 入口。
if(MSVC)
    # UAC 级别交给链接器（避免与自定义 manifest 的 level 冲突）；DPI/兼容性仍由 app.manifest 提供。
    if(CIV6_REQUIRE_ADMIN)
        set(_civ6_uac "level='requireAdministrator' uiAccess='false'")
    else()
        set(_civ6_uac "level='asInvoker' uiAccess='false'")
    endif()
    set_target_properties(civ6proxy PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /MANIFESTUAC:\"${_civ6_uac}\" /PDBALTPATH:%_PDB%")
    target_compile_options(civ6proxy PRIVATE /utf-8 /W3)
endif()

# ---------------------------------------------------------------- 资产拷贝
# 1) Slint 运行时 DLL（若为动态库）
add_custom_command(TARGET civ6proxy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:civ6proxy> $<TARGET_FILE_DIR:civ6proxy>
    COMMAND_EXPAND_LISTS)

# 2) WinDivert 驱动（运行时需与 exe 同目录，直接从安装目录加载）
add_custom_command(TARGET civ6proxy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/windivert/WinDivert64.dll
        $<TARGET_FILE_DIR:civ6proxy>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/windivert/WinDivert64.sys
        $<TARGET_FILE_DIR:civ6proxy>)

# ---------------------------------------------------------------- 单元测试（可选）
option(CIV6_BUILD_TESTS "构建核心逻辑单测" ON)
if(CIV6_BUILD_TESTS)
    enable_testing()
    add_executable(civ6_tests
        tests/test_core.cpp
        src/net_util.cpp
        src/proxy.cpp
    )
    target_include_directories(civ6_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(civ6_tests PRIVATE ws2_32 iphlpapi)
    if(MSVC)
        target_compile_options(civ6_tests PRIVATE /utf-8)
    endif()
    add_test(NAME civ6_tests COMMAND civ6_tests)
endif()

# ---------------------------------------------------------------- 安装包（Inno Setup）
# 一键从构建产物生成安装包：cmake --build build --config Release --target installer
# 需安装 Inno Setup 6（提供命令行编译器 ISCC.exe）；未安装时不生成 installer 目标。
find_program(CIV6_ISCC
    NAMES ISCC
    HINTS
        "C:/Program Files/Inno Setup 7"
        "C:/Program Files (x86)/Inno Setup 7"
        "$ENV{ProgramFiles}/Inno Setup 7"
        "$ENV{LOCALAPPDATA}/Programs/Inno Setup 7"
        "C:/Program Files (x86)/Inno Setup 6"
        "C:/Program Files/Inno Setup 6"
        "$ENV{ProgramFiles}/Inno Setup 6"
        "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6"
    DOC "Inno Setup 命令行编译器 ISCC.exe")

if(CIV6_ISCC)
    add_custom_target(installer
        COMMAND "${CIV6_ISCC}"
            "/DAppVersion=1.2"
            "/DAppSourceDir=$<TARGET_FILE_DIR:civ6proxy>"
            "/DWinDivertDir=${CMAKE_CURRENT_SOURCE_DIR}/third_party/windivert"
            "${CMAKE_CURRENT_SOURCE_DIR}/installer/civ6proxy.iss"
        DEPENDS civ6proxy
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "使用 Inno Setup 生成安装包（输出到 dist/）..."
        VERBATIM
        USES_TERMINAL)
else()
    message(STATUS "未找到 Inno Setup (ISCC.exe)：不提供 installer 目标。安装 Inno Setup 6 后重新配置 CMake 即可启用。")
endif()
