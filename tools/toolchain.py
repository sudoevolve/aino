"""挑选可用的 g++ 工具链（不进系统 PATH，只在脚本里定位）。

为什么不直接 `shutil.which("g++")`：
  在开发机上它命中的是 CLion 自带的 MinGW 15.2.0，那套的静态运行时是残的
  （libwinpthread.a 缺 __intrinsic_setjmpex、libmsvcrt.a 缺 __ms_vsnprintf），
  `-static` 必定链接失败 —— 于是产物永远甩不掉 libwinpthread-1.dll。
  只要机器上还有一套健康的工具链（w64devkit / MSYS2 / winlibs），就该优先用它。

为什么不改系统 PATH：
  改 PATH 是全局副作用，换机器/换 shell 就失效，也会影响别的项目。

为什么不放在 Downloads：
  那是"清理垃圾"脚本和清理软件的重点扫描区，700+ MB 的工具链很容易被顺手清掉；
  清掉之后产物就从「一个 exe」变回「exe + 一堆 DLL」，报错还很难看懂。
  所以约定装到 `C:\\Tools\\w64devkit`（可用 AINO_TOOLS_DIR 改根目录），
  缺失时自行安装 w64devkit，或用 AINO_GXX 指向现有 g++；本脚本不下载工具链。

优先级由高到低：
  1. 环境变量 AINO_GXX（显式指定编译器，最优先）
  2. C:\\Tools\\w64devkit\\bin（约定的手动安装位置）
  3. 其它常见位置（w64devkit / MSYS2 / winlibs，含早先下载到 Downloads 的旧目录）
  4. PATH 里的 g++（最后兜底）
"""

import os
import shutil


def tools_root():
    """工具链的根目录，默认 C:\\Tools。"""
    return os.environ.get("AINO_TOOLS_DIR") or r"C:\Tools"


def install_dir():
    """w64devkit 的默认安装位置。"""
    return os.path.join(tools_root(), "w64devkit")


def gxx_candidates():
    """按优先级返回候选 g++.exe 绝对路径（已去重，且逐个确认文件存在）。"""
    found = []

    explicit = os.environ.get("AINO_GXX")
    if explicit and os.path.isfile(explicit):
        found.append(explicit)

    root = tools_root()
    home = os.path.expanduser("~")
    for d in (
        # 主位置：约定的手动安装目录
        os.path.join(root, "w64devkit", "bin"),
        os.path.join(root, "w64devkit", "w64devkit", "bin"),   # 解压时多套了一层
        r"C:\w64devkit\bin",
        # 兜底：早先下载/手动解压的位置
        os.path.join(home, "Downloads", "w64devkit", "w64devkit", "bin"),
        os.path.join(home, "w64devkit", "bin"),
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
