# CMake 的自动探测可能按 ACP 解读 UTF-8 的 MSVC 诊断，导致 Ninja 静默丢失依赖。
# 用已知头文件确认实际前缀；不修改生成的 Ninja 文件或宿主工具链设置。
set(_aino_dep_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/aino-dependencies")
file(MAKE_DIRECTORY "${_aino_dep_dir}")
file(WRITE "${_aino_dep_dir}/aino_dependency_header.hpp" "#pragma once\n")
file(WRITE "${_aino_dep_dir}/prefix.cpp" "#include \"aino_dependency_header.hpp\"\n")
execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /EP "${_aino_dep_dir}/prefix.cpp"
    WORKING_DIRECTORY "${_aino_dep_dir}"
    OUTPUT_VARIABLE _aino_dep_stdout ERROR_VARIABLE _aino_dep_stderr
    RESULT_VARIABLE _aino_dep_result ENCODING AUTO)
if(NOT _aino_dep_result EQUAL 0)
    message(FATAL_ERROR "Cannot verify MSVC header dependency diagnostics: ${_aino_dep_stderr}")
endif()
string(REPLACE "\r" "" _aino_dep_output "${_aino_dep_stdout}${_aino_dep_stderr}")
string(REPLACE "\n" ";" _aino_dep_lines "${_aino_dep_output}")
set(_aino_dep_prefix "")
foreach(_aino_dep_line IN LISTS _aino_dep_lines)
    if(_aino_dep_line MATCHES "^(.*)[A-Za-z]:[/\\].*aino_dependency_header[.]hpp$")
        set(_aino_dep_prefix "${CMAKE_MATCH_1}")
        break()
    endif()
endforeach()
if(NOT _aino_dep_prefix)
    message(FATAL_ERROR "Cannot identify MSVC include prefix; refusing a Ninja build without header dependencies")
endif()
set(CMAKE_CL_SHOWINCLUDES_PREFIX "${_aino_dep_prefix}")
set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${_aino_dep_prefix}")
