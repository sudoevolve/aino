cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)
project(aino_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AINO_STATIC_EXE "静态链接普通示例的编译器运行时" ON)
if(MSVC AND AINO_STATIC_EXE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

function(aino_add_demo name)
    cmake_parse_arguments(DEMO "NATIVE_ENTRY" "" "" ${ARGN})
    # Windows 必须用 WIN32 子系统，否则双击 exe 会先弹一个控制台窗口。
    if(WIN32)
        add_executable(${name} WIN32 ${DEMO_UNPARSED_ARGUMENTS})
    else()
        add_executable(${name} ${DEMO_UNPARSED_ARGUMENTS})
    endif()

    if(WIN32)
        target_link_libraries(${name} PRIVATE user32 gdi32)
    elseif(APPLE)
        target_link_libraries(${name} PRIVATE
            "-framework Cocoa" "-framework CoreText" "-framework CoreGraphics")
    else()
        target_link_libraries(${name} PRIVATE X11 ${CMAKE_DL_LIBS})
    endif()

    # 源码与字符串统一按 UTF-8 编译。
    if(MSVC)
        target_compile_options(${name} PRIVATE /W3 /utf-8)
        target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
        # WIN32 子系统下 MSVC 默认找 WinMain；示例用的是 main()，指定入口为 mainCRTStartup。
        if(NOT DEMO_NATIVE_ENTRY)
            target_link_options(${name} PRIVATE /ENTRY:mainCRTStartup)
        endif()
    else()
        target_compile_options(${name} PRIVATE -Wall)
        if(MINGW)
            # 配置期验证静态运行时是否完整；失败时回退并随附 pthread DLL。
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
            # Release 产物去除调试符号。
            target_link_options(${name} PRIVATE $<$<NOT:$<CONFIG:Debug>>:-s>)
        endif()
    endif()
endfunction()

option(AINO_BUILD_SSH "构建 SSH 客户端示例（需要第三方静态依赖）" ON)
set(AINO_SSH_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/build/third_party"
    CACHE PATH "libssh2 / mbedTLS 依赖目录")

function(aino_add_ssh source)
    if(NOT AINO_BUILD_SSH)
        return()
    endif()
    if(WIN32 AND MSVC)
        set(_ssh_l2_inc  "${AINO_SSH_DEPS}/libssh2-1.11.1/include")
        # 预编译 libssh2 使用 mbedTLS 后端与动态 CRT。
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
                "  依赖构建说明见 docs/01-quickstart.md。")
        else()
            aino_add_demo(aino_ssh "${source}" NATIVE_ENTRY)
            set_target_properties(aino_ssh PROPERTIES
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
            target_include_directories(aino_ssh PRIVATE
                "${_ssh_l2_inc}" "${_ssh_l2_bld}" "${_ssh_mbed_inc}")
            target_compile_definitions(aino_ssh PRIVATE NOMINMAX)
            # 链接顺序：libssh2 依赖 mbedtls，所以 mbedtls 放后面。
            target_link_libraries(aino_ssh PRIVATE
                "${_ssh_l2_bld}/libssh2.lib" "${_ssh_mbed_lib}"
                msimg32
                ws2_32 crypt32 bcrypt
                shell32 ole32)
        endif()
    else()
        find_path(AINO_LIBSSH2_INCLUDE_DIR libssh2.h)
        find_library(AINO_LIBSSH2_LIBRARY NAMES ssh2 libssh2)
        if(NOT AINO_LIBSSH2_INCLUDE_DIR OR NOT AINO_LIBSSH2_LIBRARY)
            message(STATUS "跳过 aino_ssh：未找到 libssh2 开发库")
            return()
        endif()
        aino_add_demo(aino_ssh "${source}" NATIVE_ENTRY)
        target_include_directories(aino_ssh PRIVATE "${AINO_LIBSSH2_INCLUDE_DIR}")
        find_package(Threads REQUIRED)
        target_link_libraries(aino_ssh PRIVATE "${AINO_LIBSSH2_LIBRARY}" Threads::Threads)
        if(WIN32)
            target_link_libraries(aino_ssh PRIVATE ws2_32 crypt32 bcrypt shell32 ole32 msimg32)
        endif()
    endif()
endfunction()

# 每个顶层 .cpp 是一个独立示例；添加或删除文件会触发重新配置。
file(GLOB AINO_EXAMPLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp")
foreach(source IN LISTS AINO_EXAMPLE_SOURCES)
    get_filename_component(stem "${source}" NAME_WE)
    if(stem STREQUAL "ssh_client")
        aino_add_ssh("${source}")
    else()
        aino_add_demo("aino_${stem}" "${source}")
    endif()
endforeach()
