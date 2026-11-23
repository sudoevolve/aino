cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)
project(aino_demo VERSION 0.5.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AINO_STATIC_EXE "静态链接 Windows 示例及测试的编译器运行时" OFF)
option(AINO_WAYLAND "使用原生 Wayland 后端（不链接 X11）" OFF)
if(AINO_WAYLAND AND (WIN32 OR APPLE))
    message(FATAL_ERROR "AINO_WAYLAND requires Linux / Unix")
endif()
if(MSVC AND AINO_STATIC_EXE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)
add_library(aino INTERFACE)
add_library(aino::aino ALIAS aino)
target_compile_features(aino INTERFACE cxx_std_17)
if(MSVC)
    target_compile_options(aino INTERFACE /utf-8)
endif()
target_include_directories(aino INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>" "$<INSTALL_INTERFACE:include>")
target_link_libraries(aino INTERFACE Threads::Threads)
if(WIN32)
    target_link_libraries(aino INTERFACE user32 gdi32 ole32 windowscodecs)
elseif(APPLE)
    target_link_libraries(aino INTERFACE "-framework Cocoa" "-framework CoreText"
        "-framework CoreGraphics" "-framework ImageIO" "-framework ApplicationServices")
else()
    if(AINO_WAYLAND)
        target_compile_definitions(aino INTERFACE AINO_WAYLAND=1)
        target_link_libraries(aino INTERFACE ${CMAKE_DL_LIBS})
    else()
        target_link_libraries(aino INTERFACE X11 ${CMAKE_DL_LIBS})
    endif()
endif()

include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/ainoConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/ainoConfig.cmake"
    INSTALL_DESTINATION lib/cmake/aino)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/ainoConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(TARGETS aino EXPORT ainoTargets)
install(FILES aino.hpp DESTINATION include)
install(EXPORT ainoTargets NAMESPACE aino:: DESTINATION lib/cmake/aino)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ainoConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ainoConfigVersion.cmake" DESTINATION lib/cmake/aino)

function(aino_add_demo name)
    cmake_parse_arguments(DEMO "NATIVE_ENTRY" "" "" ${ARGN})
    # Windows 必须用 WIN32 子系统，否则双击 exe 会先弹一个控制台窗口。
    if(WIN32)
        add_executable(${name} WIN32 ${DEMO_UNPARSED_ARGUMENTS} "${CMAKE_CURRENT_SOURCE_DIR}/assets/aino_icon.rc")
    else()
        add_executable(${name} ${DEMO_UNPARSED_ARGUMENTS})
    endif()
    aino_configure_target(${name})
    if(WIN32)
        set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/aino_icon.rc"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/aino_icon.ico"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/aino_icon.png")
    else()
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/aino_icon.png"
                "$<TARGET_FILE_DIR:${name}>/aino_icon.png"
            VERBATIM)
    endif()
    if(WIN32 AND MSVC AND NOT DEMO_NATIVE_ENTRY)
        target_link_options(${name} PRIVATE /ENTRY:mainCRTStartup)
    endif()
endfunction()

function(aino_configure_target name)
    target_link_libraries(${name} PRIVATE aino::aino)

    # 源码与字符串统一按 UTF-8 编译。
    if(MSVC)
        target_compile_options(${name} PRIVATE /W3 /utf-8)
        target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${name} PRIVATE -Wall)
        if(MINGW)
            # 静态请求不能静默回退成依赖 pthread DLL 的产物。
            if(AINO_STATIC_EXE)
                include(CheckCXXSourceCompiles)
                include(CMakePushCheckState)
                cmake_push_check_state(RESET)
                set(CMAKE_REQUIRED_LINK_OPTIONS -static)
                check_cxx_source_compiles(
                    "#include <string>\n#include <thread>\n#include <stdexcept>\nthread_local std::string value=\"x\";\nint main(){std::thread t([]{try{throw std::runtime_error(value);}catch(const std::exception&){}});t.join();}"
                    AINO_STATIC_RUNTIME_LINK_OK)
                cmake_pop_check_state()
                if(NOT AINO_STATIC_RUNTIME_LINK_OK)
                    message(FATAL_ERROR
                        "AINO_STATIC_EXE=ON requires a complete MinGW static runtime. "
                        "Install the static runtime or explicitly configure -DAINO_STATIC_EXE=OFF to package DLLs.")
                else()
                    target_link_options(${name} PRIVATE -static)
                endif()
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

option(AINO_BUILD_SSH "构建 SSH 客户端示例（需要第三方静态依赖）" OFF)
set(AINO_SSH_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/build/third_party"
    CACHE PATH "libssh2 / mbedTLS 依赖目录")

function(aino_add_ssh source)
    if(NOT AINO_BUILD_SSH)
        return()
    endif()
    if(WIN32 AND MSVC)
        set(_ssh_l2_inc  "${AINO_SSH_DEPS}/libssh2-1.11.1/include")
        # 此目录由仓库脚本生成 /MT 依赖，SSH 目标显式匹配，避免默认 /MD 混链。
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
            message(FATAL_ERROR
                "无法构建 aino_ssh —— 缺第三方静态依赖：\n    ${_ssh_msg}\n"
                "  依赖构建说明见 docs/wiki/01-quickstart.md。")
        else()
            aino_add_demo(aino_ssh "${source}" NATIVE_ENTRY)
            set_property(TARGET aino_ssh PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
            target_include_directories(aino_ssh PRIVATE
                "${_ssh_l2_inc}" "${_ssh_l2_bld}" "${_ssh_mbed_inc}")
            target_compile_definitions(aino_ssh PRIVATE NOMINMAX)
            # 链接顺序：libssh2 依赖 mbedtls，所以 mbedtls 放后面。
            target_link_libraries(aino_ssh PRIVATE
                "${_ssh_l2_bld}/libssh2.lib" "${_ssh_mbed_lib}"
                msimg32
                ws2_32 crypt32 bcrypt
                shell32 ole32 uuid comdlg32)
        endif()
    else()
        find_path(AINO_LIBSSH2_INCLUDE_DIR libssh2.h)
        find_library(AINO_LIBSSH2_LIBRARY NAMES ssh2 libssh2)
        if(NOT AINO_LIBSSH2_INCLUDE_DIR OR NOT AINO_LIBSSH2_LIBRARY)
            message(FATAL_ERROR "AINO_BUILD_SSH=ON requires libssh2 development headers and library")
        endif()
        aino_add_demo(aino_ssh "${source}" NATIVE_ENTRY)
        target_include_directories(aino_ssh PRIVATE "${AINO_LIBSSH2_INCLUDE_DIR}")
        find_package(Threads REQUIRED)
        target_link_libraries(aino_ssh PRIVATE "${AINO_LIBSSH2_LIBRARY}" Threads::Threads)
        if(WIN32)
            target_link_libraries(aino_ssh PRIVATE ws2_32 crypt32 bcrypt shell32 ole32 uuid msimg32 comdlg32)
        endif()
        if(APPLE)
            target_link_libraries(aino_ssh PRIVATE "-framework Security")
        endif()
    endif()
    if(TARGET aino_ssh AND MINGW AND AINO_STATIC_EXE)
        add_custom_command(TARGET aino_ssh POST_BUILD
            COMMAND ${CMAKE_COMMAND} "-DEXE=$<TARGET_FILE:aino_ssh>" "-DOBJDUMP=${CMAKE_OBJDUMP}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckStaticRuntime.cmake"
            VERBATIM)
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

option(AINO_BUILD_TESTS "构建 CTest 回归探针（含原生窗口测试）" OFF)
if(AINO_BUILD_TESTS)
    enable_testing()
    include(cmake/Tests.cmake)
endif()
