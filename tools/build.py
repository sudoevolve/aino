"""构建单个示例；优先使用已配置的 CMake，Windows 可回退到 MinGW。"""

import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD_DIR = os.path.join(ROOT, "build")

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--src", default="examples/gallery.cpp")
parser.add_argument("--out", help="输出路径，相对路径以 build 为根目录")
args = parser.parse_args()
SRC = os.path.abspath(os.path.join(ROOT, args.src))
stem = os.path.splitext(os.path.basename(SRC))[0]
target = "aino_ssh" if stem == "ssh_client" else "aino_" + stem
OUT = os.path.abspath(os.path.join(BUILD_DIR, args.out or (target + ".exe")))


def try_cmake():
    if os.path.dirname(SRC) != os.path.join(ROOT, "examples"):
        return None
    try:
        from msvc_build import msvc_env
        env = msvc_env()
    except SystemExit:
        return None
    except Exception:
        return None
    if not os.path.isfile(os.path.join(BUILD_DIR, "CMakeCache.txt")):
        return None
    return env


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
    configured = subprocess.run(["cmake", "-S", ROOT, "-B", BUILD_DIR], env=env)
    if configured.returncode:
        raise SystemExit(configured.returncode)
    r = subprocess.run(["cmake", "--build", BUILD_DIR, "--config", "Release", "--target", target],
                       env=env, capture_output=True, text=True, errors="replace")
    if r.returncode == 0:
        for subdir in ("Release", ""):
            artifact = os.path.join(BUILD_DIR, subdir, target + ".exe")
            if os.path.isfile(artifact):
                if os.path.abspath(artifact) != os.path.abspath(OUT):
                    os.makedirs(os.path.dirname(OUT), exist_ok=True)
                    shutil.copy2(artifact, OUT)
                break
        else:
            raise SystemExit("未找到 CMake 产物: " + target)

else:
    from toolchain import gxx_candidates, toolchain_tag
    cands = gxx_candidates()
    if not cands:
        raise SystemExit("既没有可用的 MSVC + CMake，也找不到 g++ —— 无法构建")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    src = SRC
    out = OUT

    common = ["-std=c++17", "-O2", "-mwindows", "-s",
              "-o", out, src, "-lgdi32", "-luser32"]

    gxx, r = None, None
    for cand in cands:
        r = subprocess.run([cand] + common + ["-static"], capture_output=True,
                           text=True, errors="replace")
        if r.returncode == 0:
            gxx = cand
            break
        print("[g++] %s 不支持 -static，换下一个候选" % toolchain_tag(cand))

    if gxx is None:
        gxx = cands[0]
        print("[g++] 没有支持 -static 的工具链，退回 -static-libgcc/-static-libstdc++")
        r = subprocess.run([gxx] + common + ["-static-libgcc", "-static-libstdc++"],
                           capture_output=True, text=True, errors="replace")
        if r.returncode == 0:
            ship_runtime_dll(out, gxx)

    print("[g++]", gxx)

print(r.stdout[-4000:])
if r.stderr:
    print(r.stderr[-3000:], file=sys.stderr)
print("rc =", r.returncode)
sys.exit(r.returncode)
