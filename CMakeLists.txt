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

# 三个目标：画廊（功能最全，也是回归靶子）+ 两个最小 demo（新手照着读）。
# 多个窗口好用：同一套 AppState 跨页共享，浮层也天然盖在所有页之上。
aino_add_demo(aino_gallery examples/gallery.cpp)
aino_add_demo(aino_sidebar_demo examples/sidebar_demo.cpp)
aino_add_demo(aino_zoom_demo examples/zoom_demo.cpp)
