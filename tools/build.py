"""构建 gallery（唯一示例）。MSVC + CMake 可用时走 CMake，否则用 g++ 直编单文件。

2026-10-17 改动：
- 原来把构建目录硬编码成 `C:/Users/admin/...`，换机器就废（当前机器是 Administrator）。
  改成从脚本位置往上推，不再写死用户名。
- 加了 g++ 回退：这台机器 vcvars64.bat 导不出 MSVC 环境，msvc_env() 会抛 SystemExit，
  回归的"编译 aino_gallery"一步永远失败。但代码本身是能编的，所以让回退接住。
"""

import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD_DIR = os.path.join(ROOT, "build")


def try_cmake():
    try:
        from msvc_build import msvc_env
        env = msvc_env()
    except SystemExit:
        return None
    except Exception:
        return None
    # 没有 CMakeCache 说明还没 configure 过，--build 会报错，交给 g++ 更省事
    if not os.path.isfile(os.path.join(BUILD_DIR, "CMakeCache.txt")):
        return None
    return env


def kill_stale():
    """上一个回归如果没把 gallery 关干净，exe 被占用 → 链接必失败。

    踩过：第一次跑到 hover 探针偶发 FAIL 就退了，gallery 留在那里，
    下一次构建直接 "Permission denied" —— 看起来像代码坏了，其实是文件锁。
    """
    try:
        subprocess.run(["taskkill", "/F", "/IM", "aino_gallery.exe"],
                       capture_output=True, timeout=15)
    except Exception:
        pass


env = try_cmake()
if env is not None:
    r = subprocess.run(["cmake", "--build", BUILD_DIR], env=env,
                       capture_output=True, text=True, errors="replace")
else:
    gxx = shutil.which("g++")
    if not gxx:
        raise SystemExit("既没有可用的 MSVC + CMake，也找不到 g++ —— 无法构建")
    os.makedirs(BUILD_DIR, exist_ok=True)
    kill_stale()
    src = os.path.join(ROOT, "examples", "gallery.cpp")
    out = os.path.join(BUILD_DIR, "aino_gallery.exe")
    print("[g++]", gxx)
    r = subprocess.run([gxx, "-std=c++17", "-O2", "-mwindows", "-o", out, src,
                        "-lgdi32", "-luser32"],
                       capture_output=True, text=True, errors="replace")

print(r.stdout[-4000:])
print(r.stderr[-3000:], file=sys.stderr)
print("rc =", r.returncode)
sys.exit(r.returncode)
