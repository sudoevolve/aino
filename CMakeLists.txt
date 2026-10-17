cmake_minimum_required(VERSION 3.15)
project(aino_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# aino 只依赖各平台的系统库 —— 没有第三方依赖，这是刻意的。
# Windows 上只需要 user32 + gdi32：用不到 msimg32，因为圆角/圆/线都是本库自己
# 做的覆盖率光栅化，不走 GDI 的 AlphaBlend / GradientFill。
function(aino_add_demo name)
    add_executable(${name} ${ARGN})

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
    else()
        target_compile_options(${name} PRIVATE -Wall)
    endif()
endfunction()

# 只有一个目标：侧边栏分页画廊。
# 它取代了原来的三个可执行文件（三栏画廊 main.cpp / 布局演示 layout.cpp /
# 组件总览 components.cpp）—— 分页比三个 exe 更好用：同一套 AppState 跨页共享，
# 而且浮层能天然盖在所有页之上，不会出现"三个窗口各画各的"。
aino_add_demo(aino_gallery examples/gallery.cpp)
