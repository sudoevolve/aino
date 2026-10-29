cmake_minimum_required(VERSION 3.15)
project(aino_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 分发用的 exe 不能依赖编译器运行时的 DLL —— 这台机器上它们都在 PATH 里，换台机器就缺：
#   MSVC  默认 /MD  → 缺 vcruntime140.dll / msvcp140.dll（目标机没装 VC++ 运行库）
#   MinGW 默认动态链 → 缺 libgcc_s_seh-1.dll / libstdc++-6.dll / libwinpthread-1.dll
# CMP0091 是让 CMAKE_MSVC_RUNTIME_LIBRARY 生效的前提（CMake 3.15+）。
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# aino 只依赖各平台的系统库 —— 没有第三方依赖，这是刻意的。
# Windows 上只需要 user32 + gdi32：用不到 msimg32，因为圆角/圆/线都是本库自己
# 做的覆盖率光栅化，不走 GDI 的 AlphaBlend / GradientFill。
# 产物只出一个 exe、不依赖编译器运行时 DLL（MSVC 对应 /MT）。
option(AINO_STATIC_EXE "静态链接运行时，产物零 DLL 依赖" ON)

function(aino_add_demo name)
    # Windows 必须用 WIN32 子系统，否则双击 exe 会先弹一个控制台窗口。
    if(WIN32)
        add_executable(${name} WIN32 ${ARGN})
    else()
        add_executable(${name} ${ARGN})
    endif()

    if(WIN32)
        target_link_libraries(${name} PRIVATE user32 gdi32)
    elseif(APPLE)
        target_link_libraries(${name} PRIVATE
            "-framework Cocoa" "-framework CoreText" "-framework CoreGraphics")
    else()
        target_link_libraries(${name} PRIVATE X11)
    endif()

    # /utf-8 在 MSVC 上是**硬要求**，不是优化项：源码里的非 ASCII 字面量会按
    # 系统 ANSI 代码页（本机 GBK）编进窄字符串，而库内部一律按 UTF-8 处理。
    # to_wide 虽然会在"严格 UTF-8 解码失败"时回退到 CP_ACP，但存在解不出来的情况：
    # GBK 的 "状态" = D7 B4 CC AC，本身就是合法 UTF-8（U+05F4 U+032C），
    # 于是回退不会被触发，画出来是乱码 —— 这类歧义字节没有任何启发式能救。
    if(MSVC)
        target_compile_options(${name} PRIVATE /W3 /utf-8)
        target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
        # WIN32 子系统下 MSVC 默认找 WinMain；示例用的是 main()，指定入口为 mainCRTStartup。
        target_link_options(${name} PRIVATE /ENTRY:mainCRTStartup)
    else()
        target_compile_options(${name} PRIVATE -Wall)
        if(MINGW)
            # 目标：产物只出一个 exe、不依赖编译器运行时 DLL。
            # 但 -static 要求工具链静态运行时完整 —— CLion 自带的 MinGW 15.2.0 就不行
            # （libwinpthread.a 缺 __intrinsic_setjmpex）。配置期先真链一次试试：
            # 成功就 -static；失败自动退回半静态，并把 libwinpthread-1.dll 拷到 exe 旁边，
            # 免得在 IDE 里一构建就报一堆链接错误。
            set(_aino_static_ok OFF)
            if(AINO_STATIC_EXE)
                include(CheckCXXSourceCompiles)
                set(CMAKE_REQUIRED_LINK_OPTIONS -static)
                check_cxx_source_compiles(
                    "#include <string>\nint main(){std::string s=\"x\";return s.size()==1?0:1;}"
                    AINO_STATIC_LINK_OK)
                unset(CMAKE_REQUIRED_LINK_OPTIONS)
                set(_aino_static_ok ${AINO_STATIC_LINK_OK})
            endif()

            if(_aino_static_ok)
                target_link_options(${name} PRIVATE -static)
            else()
                target_link_options(${name} PRIVATE -static-libgcc -static-libstdc++)
                get_filename_component(_gxx_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
                if(EXISTS "${_gxx_dir}/libwinpthread-1.dll")
                    add_custom_command(TARGET ${name} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${_gxx_dir}/libwinpthread-1.dll" "$<TARGET_FILE_DIR:${name}>")
                endif()
            endif()
            # 顺带 strip：静态库会带进整份符号表，不 strip 体积翻倍。
            target_link_options(${name} PRIVATE $<$<NOT:$<CONFIG:Debug>>:-s>)
        endif()
    endif()
endfunction()

# 四个目标：画廊（功能最全，也是回归靶子）+ 三个最小 demo（新手照着读）。
# 多个窗口好用：同一套 AppState 跨页共享，浮层也天然盖在所有页之上。
aino_add_demo(aino_gallery examples/gallery.cpp)
aino_add_demo(aino_sidebar_demo examples/sidebar_demo.cpp)
aino_add_demo(aino_zoom_demo examples/zoom_demo.cpp)
aino_add_demo(aino_function_viewer examples/function_viewer.cpp)

# =============================================================================
#  SSH 客户端（可选目标）
#
#  和上面四个 demo 不一样，它不是"只依赖 aino.hpp"：还需要静态 libssh2 + mbedTLS。
#  这两个产物不在源码树里，由 build/third_party/_mk.py 下载源码并编成 .lib，所以
#  这里先探测再决定加不加目标 —— 缺依赖只是跳过它，不能让整个工程配置失败。
#
#  为什么不能复用 aino_add_demo()：
#    · 那个函数给的是 /MT（静态 CRT，产物零 DLL 依赖），而 libssh2.lib 是 /MD 编的，
#      混链会满屏 __imp_* 未解析，所以本目标必须单独用 /MD；
#    · 它还会把入口改成 mainCRTStartup，而 ssh_client.cpp 的入口是 WinMain
#      （WINDOWS 子系统默认就找 WinMain，不用额外指定）。
#
#  依赖就绪后 `cmake --build build --config Release` 就会一起产出 aino_ssh.exe，
#  不再需要单独跑 tools/build_ssh.py（那个脚本仍然保留，用来编各种探针）。
# =============================================================================
option(AINO_BUILD_SSH "构建 SSH 客户端示例（需要第三方静态依赖）" ON)
set(AINO_SSH_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/build/third_party"
    CACHE PATH "libssh2 / mbedTLS 依赖目录")

if(AINO_BUILD_SSH)
    if(WIN32 AND MSVC)
        set(_ssh_l2_inc  "${AINO_SSH_DEPS}/libssh2-1.11.1/include")
        # 用 mbedTLS 后端那份构建产物（l2mbed），不是 WinCNG：
        # WinCNG 版的 libssh2 没开 ECDSA/ED25519，只剩 diffie-hellman-group*，
        # 而 OpenSSH 10.x 已经停用那一族，握手会"没有共同算法"。
        set(_ssh_l2_bld  "${AINO_SSH_DEPS}/l2mbed/src")
        set(_ssh_mbed_inc "${AINO_SSH_DEPS}/mbedtls-3.6.2/include")
        set(_ssh_mbed_lib "${AINO_SSH_DEPS}/mbedtls-3.6.2/mbedtls_all.lib")

        set(_ssh_missing "")
        foreach(_p "${_ssh_l2_inc}/libssh2.h" "${_ssh_l2_bld}/libssh2_config.h"
                   "${_ssh_l2_bld}/libssh2.lib" "${_ssh_mbed_lib}")
            if(NOT EXISTS "${_p}")
                list(APPEND _ssh_missing "${_p}")
            endif()
        endforeach()

        if(_ssh_missing)
            list(JOIN _ssh_missing "\n    " _ssh_msg)
            message(WARNING
                "跳过 aino_ssh —— 缺第三方静态依赖：\n    ${_ssh_msg}\n"
                "  先跑：python build/third_party/_mk.py")
        else()
            add_executable(aino_ssh WIN32 examples/ssh_client.cpp)
            set_target_properties(aino_ssh PROPERTIES
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
            target_include_directories(aino_ssh PRIVATE
                "${_ssh_l2_inc}" "${_ssh_l2_bld}" "${_ssh_mbed_inc}")
            target_compile_options(aino_ssh PRIVATE /W3 /utf-8)
            target_compile_definitions(aino_ssh PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
            # 链接顺序：libssh2 依赖 mbedtls，所以 mbedtls 放后面。
            target_link_libraries(aino_ssh PRIVATE
                "${_ssh_l2_bld}/libssh2.lib" "${_ssh_mbed_lib}"
                user32 gdi32 msimg32        # ime 候选框 / 圆角光栅化 / 渐变
                ws2_32 crypt32 bcrypt       # 套接字 / 主机密钥指纹 / mbedTLS 熵源
                shell32 ole32)              # SHBrowseForFolder 选下载目录
        endif()
    else()
        # Linux/macOS 用发行版自带 libssh2；平台窗口库由 aino_add_demo 负责。
        aino_add_demo(aino_ssh examples/ssh_client.cpp)
        target_link_libraries(aino_ssh PRIVATE ssh2)
        find_package(Threads REQUIRED)
        target_link_libraries(aino_ssh PRIVATE Threads::Threads)
    endif()
endif()
