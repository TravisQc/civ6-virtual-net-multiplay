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
# 只需 winit 后端 + Fluent 风格。
set(SLINT_FEATURE_BACKEND_WINIT ON CACHE BOOL "" FORCE)
set(SLINT_FEATURE_RENDERER_FEMTOVG ON CACHE BOOL "" FORCE)
set(SLINT_FEATURE_RENDERER_SOFTWARE ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Slint)

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
        LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /MANIFESTUAC:\"${_civ6_uac}\"")
    target_compile_options(civ6proxy PRIVATE /utf-8 /W3)
endif()

# ---------------------------------------------------------------- 资产拷贝
# 1) Slint 运行时 DLL（若为动态库）
add_custom_command(TARGET civ6proxy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:civ6proxy> $<TARGET_FILE_DIR:civ6proxy>
    COMMAND_EXPAND_LISTS)

# 2) WinDivert 驱动（运行时需与 exe 同目录，程序会再复制到稳定英文目录）
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
