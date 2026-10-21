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

# 默认编 gallery；加 --src / --out 可以编别的单文件示例（回归要顺带编 shadcn_demo）。
SRC = os.path.join(ROOT, "examples", "gallery.cpp")
OUT = os.path.join(BUILD_DIR, "aino_gallery.exe")
_argv = sys.argv[1:]
for _i, _a in enumerate(_argv):
    if _a == "--src" and _i + 1 < len(_argv):
        SRC = _argv[_i + 1]
        if not os.path.isabs(SRC):
            SRC = os.path.join(ROOT, SRC)
    elif _a == "--out" and _i + 1 < len(_argv):
        OUT = os.path.join(BUILD_DIR, _argv[_i + 1])


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
    """上一个回归如果没把目标关干净，exe 被占用 → 链接必失败。

    踩过：第一次跑到 hover 探针偶发 FAIL 就退了，gallery 留在那里，
    下一次构建直接 "Permission denied" —— 看起来像代码坏了，其实是文件锁。
    """
    for name in ("aino_gallery.exe", os.path.basename(OUT)):
        try:
            subprocess.run(["taskkill", "/F", "/IM", name],
                           capture_output=True, timeout=15)
        except Exception:
            pass


def ship_runtime_dll(exe, gxx):
    """MinGW 静态链后仍导入 libwinpthread-1.dll（thread_local 需要），补到 exe 旁边。"""
    try:
        if b"libwinpthread-1.dll" not in open(exe, "rb").read():
            return
        src_dll = os.path.join(os.path.dirname(gxx), "libwinpthread-1.dll")
        dst = os.path.join(os.path.dirname(exe), "libwinpthread-1.dll")
        if os.path.isfile(src_dll) and (
                not os.path.exists(dst) or os.path.getmtime(src_dll) > os.path.getmtime(dst)):
            shutil.copy2(src_dll, dst)
        print("[dll] libwinpthread-1.dll 已随附（整包拷走才不会缺）")
    except Exception as e:
        print("[dll] 跳过:", e)


env = try_cmake()
if env is not None:
    r = subprocess.run(["cmake", "--build", BUILD_DIR], env=env,
                       capture_output=True, text=True, errors="replace")
else:
    # 候选按优先级排：AINO_GXX → 便携工具链安装位置 → PATH 里的 g++。
    # 不再只信 shutil.which()，否则永远被 CLion 那套残废 MinGW 抢先。
    from toolchain import gxx_candidates, toolchain_tag
    cands = gxx_candidates()
    if not cands:
        raise SystemExit("既没有可用的 MSVC + CMake，也找不到 g++ —— 无法构建")
    os.makedirs(BUILD_DIR, exist_ok=True)
    kill_stale()
    src = SRC
    out = OUT

    # -s：静态链 libstdc++ 会带进整份符号表，strip 掉体积能砍一半。
    common = ["-std=c++17", "-O2", "-mwindows", "-s",
              "-o", out, src, "-lgdi32", "-luser32"]

    # 首选真·全静态：产物一个 DLL 都不带。工具链静态运行时残缺时
    # （CLion 自带的 MinGW 15.2.0 就是，libwinpthread.a 缺 __intrinsic_setjmpex），
    # -static 会链接失败 —— 换下一个候选，别死磕。
    gxx, r = None, None
    for cand in cands:
        r = subprocess.run([cand] + common + ["-static"], capture_output=True,
                           text=True, errors="replace")
        if r.returncode == 0:
            gxx = cand
            break
        print("[g++] %s 不支持 -static，换下一个候选" % toolchain_tag(cand))

    if gxx is None:
        # 全都不行 → 半静态 + 随附 libwinpthread-1.dll（只剩一个 DLL 要带）。
        gxx = cands[0]
        print("[g++] 没有支持 -static 的工具链，退回 -static-libgcc/-static-libstdc++")
        r = subprocess.run([gxx] + common + ["-static-libgcc", "-static-libstdc++"],
                           capture_output=True, text=True, errors="replace")
        if r.returncode == 0:
            ship_runtime_dll(out, gxx)

    print("[g++]", gxx)

print(r.stdout[-4000:])
print(r.stderr[-3000:], file=sys.stderr)
print("rc =", r.returncode)
sys.exit(r.returncode)
