# =============================================================================
#  aino Android 打包工程（NativeActivity，无 Java 代码、无 Gradle）
#
#  产物：libmain.so（ARM64）
#    - examples/mobile_gallery.cpp  唯一的 AINO_IMPLEMENTATION 编译单元
#    - native_app_glue              NDK 自带，驱动 android_main 的 ALooper
#    - FreeType                     构建时下载（见下），字形光栅化
#
#  FreeType 不入仓库：默认用 FetchContent 在首次配置时下载官方源码包
#  （下载后缓存在 build 目录，之后的离线构建不再联网）。两种替代方式：
#    -AINO_FREETYPE_URL=<url>   换下载源（默认 Savannah，慢时可用国内镜像）
#    -DAINO_FREETYPE_DIR=<path> 指向已解压的源码目录，完全离线构建
#
#  由 android/build_apk.py 驱动；一般不手工调用 cmake。
# =============================================================================
cmake_minimum_required(VERSION 3.22)
project(aino_android C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(AINO_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# 本仓库相对 NDK 的 native_app_glue 路径：优先用命令行 GLUE_DIR，
# 其次读环境变量 ANDROID_NDK_HOME / ANDROID_NDK_ROOT。
if(NOT GLUE_DIR)
    if(DEFINED ENV{ANDROID_NDK_HOME})
        set(GLUE_DIR "$ENV{ANDROID_NDK_HOME}/sources/android/native_app_glue")
    elseif(DEFINED ENV{ANDROID_NDK_ROOT})
        set(GLUE_DIR "$ENV{ANDROID_NDK_ROOT}/sources/android/native_app_glue")
    else()
        message(FATAL_ERROR "需要 GLUE_DIR=<NDK>/sources/android/native_app_glue（build_apk.py 会自动传）")
    endif()
endif()

# ---- 获取 FreeType 源码（2.13.3，FTL 许可）----
set(AINO_FREETYPE_URL "https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.gz"
    CACHE STRING "FreeType 源码包地址")
if(DEFINED AINO_FREETYPE_DIR AND EXISTS "${AINO_FREETYPE_DIR}/include/ft2build.h")
    set(FT_SOURCE_DIR "${AINO_FREETYPE_DIR}")
    message(STATUS "FreeType: 使用本地源码 ${FT_SOURCE_DIR}")
else()
    include(FetchContent)
    FetchContent_Declare(freetype
        URL ${AINO_FREETYPE_URL}
        URL_HASH SHA256=5c3a8e78f7b24c20b25b54ee575d6daa40007a5f4eea2845861c3409b3021747)
    FetchContent_GetProperties(freetype)
    if(NOT freetype_POPULATED)
        FetchContent_Populate(freetype)
    endif()
    set(FT_SOURCE_DIR "${freetype_SOURCE_DIR}")
    message(STATUS "FreeType: 下载自 ${AINO_FREETYPE_URL} → ${FT_SOURCE_DIR}")
endif()

add_library(main SHARED
    ${AINO_ROOT}/examples/mobile_gallery.cpp
    ${GLUE_DIR}/android_native_app_glue.c
    # FreeType 最小闭包：base 拼合层 + TT/CFF/SFNT/灰度光栅/PS 名称
    # （CFF 必需：AOSP 的 NotoSansCJK.ttc 是 CFF 轮廓，缺了中文变豆腐）
    ${FT_SOURCE_DIR}/src/base/ftbase.c
    ${FT_SOURCE_DIR}/src/base/ftsystem.c
    ${FT_SOURCE_DIR}/src/base/ftinit.c
    ${FT_SOURCE_DIR}/src/base/ftdebug.c
    ${FT_SOURCE_DIR}/src/base/ftbitmap.c
    ${FT_SOURCE_DIR}/src/base/ftmm.c
    ${FT_SOURCE_DIR}/src/sfnt/sfnt.c
    ${FT_SOURCE_DIR}/src/truetype/truetype.c
    ${FT_SOURCE_DIR}/src/cff/cff.c
    ${FT_SOURCE_DIR}/src/psaux/psaux.c
    ${FT_SOURCE_DIR}/src/pshinter/pshinter.c
    ${FT_SOURCE_DIR}/src/smooth/smooth.c
    ${FT_SOURCE_DIR}/src/psnames/psnames.c
)

target_include_directories(main PRIVATE
    ${GLUE_DIR}
    ${FT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/app    # ftmodule_android.h / ftoptions_android.h
)

target_compile_definitions(main PRIVATE
    # FreeType：注册精简模块集（truetype/cff/sfnt/psnames/pshinter/psaux/smooth），
    # 关掉外部压缩库。CFF 必须保留：AOSP 的 NotoSansCJK.ttc 是 CFF 轮廓
    FT2_BUILD_LIBRARY
    FT_CONFIG_MODULES_H="ftmodule_android.h"
    FT_CONFIG_OPTIONS_H="ftoptions_android.h"
)

# aino 需要：libandroid（ANativeWindow / AFontMatcher / AConfiguration）、liblog
find_library(ANDROID_LIB android)
find_library(LOG_LIB log)
target_link_libraries(main PRIVATE ${ANDROID_LIB} ${LOG_LIB})

# Release 下去符号减体积
target_link_options(main PRIVATE -Os -s)
