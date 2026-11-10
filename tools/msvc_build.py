"""Windows MSVC/Ninja 构建入口。

从 vcvars64.bat 提取编译环境，并按安装目录补齐 Windows SDK。
用法：
  python msvc_build.py --src <源码目录> --build <构建目录> [--target T] [--config Release] [-- -DKEY=VAL ...]
  python msvc_build.py --build <构建目录> --pretend
"""

import argparse
import os
import re
import subprocess
import sys

def _find_vs_root():
    """查找包含 vcvars64.bat 的 Visual Studio 安装。"""
    import glob
    for pat in (r"C:\Program Files\Microsoft Visual Studio\*\*",
                r"C:\Program Files (x86)\Microsoft Visual Studio\*\*"):
        for d in sorted(glob.glob(pat), reverse=True):
            if os.path.isfile(os.path.join(d, "VC", "Auxiliary", "Build", "vcvars64.bat")):
                return d
    return r"C:\Program Files\Microsoft Visual Studio\2022\Community"


VS_ROOT = _find_vs_root()
VCVARS = os.path.join(VS_ROOT, "VC", "Auxiliary", "Build", "vcvars64.bat")
CMAKE = r"C:\Program Files\CMake\bin\cmake.exe"
# 从当前 Python 安装目录定位 Ninja。
NINJA_DIR = os.path.dirname(sys.executable)

# 关键：给 cmd.exe 一份干净的 Windows 环境，避免 PATH 里的 Git usr/bin
# 让批处理内的 find / sort 等命令解析到 Unix 版本，从而导致输出失真。
CLEAN_BASE = {
    "SystemRoot": r"C:\Windows",
    "windir": r"C:\Windows",
    "SystemDrive": "C:",
    "COMSPEC": r"C:\Windows\System32\cmd.exe",
    "PATHEXT": ".COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS",
    "TEMP": os.environ.get("TEMP") or r"C:\Windows\Temp",
    "TMP": os.environ.get("TMP") or r"C:\Windows\Temp",
    "PATH": r"C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem",
}


KITS_ROOT = r"C:\Program Files (x86)\Windows Kits\10"


def _latest_sdk_version():
    for sub in ("Include", "Lib", "bin"):
        d = os.path.join(KITS_ROOT, sub)
        if os.path.isdir(d):
            vers = [v for v in os.listdir(d) if re.fullmatch(r"10\.\d+\.\d+\.\d+", v)]
            if vers:
                return max(vers)
    return None


def augment_windows_sdk(env):
    """补齐 Windows SDK 的 PATH / INCLUDE / LIB。

    vcvars64.bat 未能导出 SDK 路径时，从安装目录补入 rc.exe、mt.exe、头文件与库。
    """
    ver = _latest_sdk_version()
    if not ver:
        print("[msvc] 警告：未发现 Windows SDK，rc.exe / mt.exe 可能缺失")
        return env

    add_path, add_inc, add_lib = [], [], []
    bin64 = os.path.join(KITS_ROOT, "bin", ver, "x64")
    if os.path.isfile(os.path.join(bin64, "rc.exe")):
        add_path.append(bin64)
    for sub in ("ucrt", "shared", "um", "winrt", "cppwinrt"):
        d = os.path.join(KITS_ROOT, "Include", ver, sub)
        if os.path.isdir(d):
            add_inc.append(d)
    for sub in ("ucrt", "um", "cppwinrt"):
        d = os.path.join(KITS_ROOT, "Lib", ver, sub, "x64")
        if os.path.isdir(d):
            add_lib.append(d)

    env["PATH"] = ";".join(add_path + [env.get("PATH", "")]).strip(";")
    for key, extra in (("INCLUDE", add_inc), ("LIB", add_lib)):
        parts = [p for p in [env.get(key, "")] + extra if p]
        env[key] = ";".join(parts)
    print("[msvc] Windows SDK {} 已补齐 (rc/mt 可用)".format(ver))
    return env


def msvc_env():
    """运行 vcvars64.bat 并回读其导出的环境变量。

    注意：必须用 shell=True + 命令行字符串。若用 list 参数，Python 的
    list2cmdline 会把参数里的引号转义成 \\"，而 cmd.exe 不认这种转义，
    会直接报「命令语法不正确」且无任何输出。
    """
    script = 'call "{}" >nul 2>&1 && set'.format(VCVARS)
    proc = subprocess.run(
        script,
        shell=True,
        env=CLEAN_BASE,
        capture_output=True,
        text=True,
        errors="replace",
        timeout=180,
    )
    env = {}
    for line in proc.stdout.splitlines():
        if not line or line.startswith("="):
            continue
        key, sep, val = line.partition("=")
        if sep and key and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_()]*", key):
            env[key] = val
    if "VSCMD_ARG_TGT_ARCH" not in env and "VCToolsInstallDir" not in env:
        raise SystemExit("vcvars64.bat 未能导出 MSVC 环境:\n" + proc.stdout[-2000:])
    augment_windows_sdk(env)
    # Ninja 放在 PATH 最前面
    env["PATH"] = NINJA_DIR + ";" + env.get("PATH", "")
    return env


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", help="源码目录（CMakeLists.txt 所在或指向它的目录）")
    ap.add_argument("--build", required=True, help="构建目录")
    ap.add_argument("--target", default=None, help="仅构建指定 target")
    ap.add_argument("--config", default="Release", help="CMAKE_BUILD_TYPE，默认 Release")
    ap.add_argument("--install", action="store_true", help="构建后执行 cmake --install")
    ap.add_argument("--pretend", action="store_true", help="只探测环境，不构建")
    ap.add_argument("extra", nargs="*", help="透传给 cmake 的额外参数，如 -DFLTK_BUILD_TEST=OFF")
    args = ap.parse_args()

    env = msvc_env()

    probe = subprocess.run("cl 2>&1 | findstr /C:\"Version\"",
                           shell=True, env=env, capture_output=True, text=True,
                           errors="replace")
    print("[msvc] cl.exe :", " ".join(probe.stdout.split()) or "(no version banner)")
    print("[msvc] ninja  :", subprocess.run("ninja --version", shell=True, env=env,
                                            capture_output=True, text=True,
                                            errors="replace").stdout.strip())
    for tool in ("rc", "mt"):
        out = subprocess.run("where {}".format(tool), shell=True, env=env,
                             capture_output=True, text=True, errors="replace")
        loc = (out.stdout or "").strip().splitlines()
        print("[msvc] {:<6} : {}".format(tool + ".exe", loc[0] if loc else "!! 未找到 !!"))

    if args.pretend:
        return

    if args.src:
        cfg = [CMAKE, "-S", os.path.abspath(args.src), "-B", os.path.abspath(args.build),
               "-G", "Ninja", "-DCMAKE_BUILD_TYPE=" + args.config,
               "-DCMAKE_C_COMPILER=cl", "-DCMAKE_CXX_COMPILER=cl",
               "-DCMAKE_MAKE_PROGRAM=" + os.path.join(NINJA_DIR, "ninja.exe")]
        cfg += args.extra
        print("\n[msvc] configure:", " ".join(cfg), "\n", flush=True)
        r = subprocess.run(cfg, env=env, capture_output=True, text=True, errors="replace",
                           timeout=1800)
        print(r.stdout[-6000:])
        if r.returncode != 0:
            print(r.stderr[-6000:], file=sys.stderr)
            raise SystemExit("CMake 配置失败 (exit {})".format(r.returncode))

    build = [CMAKE, "--build", os.path.abspath(args.build)]
    if args.target:
        build += ["--target", args.target]
    print("\n[msvc] build:", " ".join(build), "\n", flush=True)
    r = subprocess.run(build, env=env, capture_output=True, text=True, errors="replace",
                       timeout=3600)
    tail = r.stdout[-8000:]
    print(tail)
    if r.returncode != 0:
        print(r.stderr[-8000:], file=sys.stderr)
        raise SystemExit("构建失败 (exit {})".format(r.returncode))

    if args.install:
        inst = [CMAKE, "--install", os.path.abspath(args.build)]
        print("\n[msvc] install:", " ".join(inst), "\n", flush=True)
        r = subprocess.run(inst, env=env, capture_output=True, text=True, errors="replace",
                           timeout=1800)
        print(r.stdout[-4000:])
        if r.returncode != 0:
            print(r.stderr[-4000:], file=sys.stderr)
            raise SystemExit("安装失败 (exit {})".format(r.returncode))
    print("\n[msvc] 完成 ✅")


if __name__ == "__main__":
    main()
