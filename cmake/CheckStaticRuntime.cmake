# A single-file Windows release may import Windows system DLLs only.
cmake_minimum_required(VERSION 3.15)
if(NOT EXISTS "${EXE}" OR NOT EXISTS "${OBJDUMP}")
    message(FATAL_ERROR "Static import audit requires EXE and OBJDUMP")
endif()
execute_process(COMMAND "${OBJDUMP}" -p "${EXE}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _imports ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect ${EXE}: ${_error}")
endif()
string(REGEX MATCHALL "DLL Name: [^\r\n]+" _dlls "${_imports}")
if(NOT _dlls)
    message(FATAL_ERROR "No Windows imports found in ${EXE}")
endif()
set(_system_dlls advapi32.dll bcrypt.dll comctl32.dll comdlg32.dll crypt32.dll
    dwmapi.dll gdi32.dll imm32.dll kernel32.dll msvcrt.dll ntdll.dll ole32.dll
    oleaut32.dll rpcrt4.dll secur32.dll shell32.dll shlwapi.dll user32.dll
    userenv.dll uxtheme.dll version.dll winmm.dll ws2_32.dll wsock32.dll
    ucrtbase.dll msimg32.dll)
foreach(_import IN LISTS _dlls)
    string(REPLACE "DLL Name: " "" _dll "${_import}")
    string(STRIP "${_dll}" _dll)
    string(TOLOWER "${_dll}" _dll)
    if(NOT _dll IN_LIST _system_dlls AND NOT _dll MATCHES "^(api|ext)-ms-.*\\.dll$")
        message(FATAL_ERROR "Single-file release still imports a non-system DLL: ${_dll}")
    endif()
endforeach()
message(STATUS "Static import audit passed: ${EXE}")
