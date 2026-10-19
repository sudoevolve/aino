"""挑选可用的 g++ 工具链（不进系统 PATH，只在脚本里定位）。

为什么不直接 `shutil.which("g++")`：
  在开发机上它命中的是 CLion 自带的 MinGW 15.2.0，那套的静态运行时是残的
  （libwinpthread.a 缺 __intrinsic_setjmpex、libmsvcrt.a 缺 __ms_vsnprintf），
  `-static` 必定链接失败 —— 于是产物永远甩不掉 libwinpthread-1.dll。
  只要机器上还有一套健康的工具链（w64devkit / MSYS2 / winlibs），就该优先用它。

为什么不改系统 PATH：
  改 PATH 是全局副作用，换机器/换 shell 就失效，也会影响别的项目。
  这里按固定顺序探测，命中即用；优先级由高到低：
    1. 环境变量 AINO_GXX（显式指定，最优先）
    2. 常见便携安装位置（w64devkit / MSYS2 / winlibs）
    3. PATH 里的 g++（最后兜底）
"""

import os
import shutil


def gxx_candidates():
    """按优先级返回候选 g++.exe 绝对路径（已去重，且逐个确认文件存在）。"""
    found = []

    explicit = os.environ.get("AINO_GXX")
    if explicit and os.path.isfile(explicit):
        found.append(explicit)

    home = os.path.expanduser("~")
    for d in (
        os.path.join(home, "Downloads", "w64devkit", "w64devkit", "bin"),
        os.path.join(home, "w64devkit", "bin"),
        r"C:\w64devkit\bin",
        r"C:\msys64\mingw64\bin",
        r"C:\msys2\mingw64\bin",
        r"C:\Program Files\mingw64\bin",
    ):
        p = os.path.join(d, "g++.exe")
        if os.path.isfile(p):
            found.append(p)

    which = shutil.which("g++")
    if which:
        found.append(which)

    seen, uniq = set(), []
    for p in found:
        key = os.path.normcase(os.path.abspath(p))
        if key not in seen:
            seen.add(key)
            uniq.append(p)
    return uniq


def toolchain_tag(gxx):
    """给日志用的短名，例如 `w64devkit` / `mingw`。"""
    parts = os.path.normpath(gxx).split(os.sep)
    for p in reversed(parts[:-1]):
        if p.lower() not in ("bin", "x86_64-w64-mingw32"):
            return p
    return os.path.dirname(gxx)
