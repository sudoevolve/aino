"""用 cl.exe 直接编单个 cpp；MSVC 不可用时自动回退到 g++。

坑：subprocess 传 env= 只影响"子进程自己的环境变量"，不影响 CreateProcess
查找可执行文件的 PATH 搜索路径 —— 所以裸写 "cl" 会 FileNotFoundError。
这里改成从 VCToolsInstallDir 拼出 cl.exe 的绝对路径。
（shell=True 也行，但绝对路径更可控。）

2026-10-17：加了 g++ 回退。原因是这台机器上 vcvars64.bat 导不出 MSVC 环境，
msvc_env() 直接抛 SystemExit，导致 regress.py 里 10 个编译步骤全红 ——
但那是环境问题，不是代码问题，不该让回归完全跑不动。
回退分支只翻译参数（`*.lib` → `-l*`、丢掉 `/utf-8` 这类 MSVC 专用开关），
刻意**不**加 `-DNDEBUG`：宁可保留 assert 多查一点，也不要让探针假通过。

2026-10-19：g++ 分支加 -static-libgcc -static-libstdc++。默认动态链会让 exe 依赖
libgcc_s_seh-1.dll / libstdc++-6.dll —— 那是 CLion 自带 MinGW 的 bin 目录，本机能跑、
别人的机器直接报「缺 DLL」。静态链之后只剩 libwinpthread-1.dll（thread_local 要用），
本脚本会在 exe 旁边补一份，便于整包拷走。

用法:
  python cl_one.py <src.cpp> <out.exe> [额外库或开关...]
默认链接 user32 gdi32 msimg32（aino 在 Windows 上需要）。
"""

import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

src, out = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
extra = sys.argv[3:] or ["user32.lib", "gdi32.lib", "msimg32.lib"]

os.makedirs(os.path.dirname(out), exist_ok=True)


def msvc_plan():
    """能拿到 MSVC 环境就返回 (cmd, env)，否则 None。"""
    try:
        from msvc_build import msvc_env
        env = msvc_env()
    except SystemExit:
        return None
    except Exception:
        return None
    cl = os.path.join(env.get("VCToolsInstallDir", ""), "bin", "Hostx64", "x64", "cl.exe")
    if not os.path.isfile(cl):
        return None
    libs = [e for e in extra if e.lower().endswith(".lib")]
    flags = [e for e in extra if not e.lower().endswith(".lib")]
    cmd = [cl, "/nologo", "/std:c++17", "/EHsc", "/O2", "/W3", "/DNDEBUG",
           "/Fe:" + out, src] + flags + libs
    return cmd, env


def gcc_build():
    """MSVC 缺席时用 g++：直接编译并返回 (cmd, env, result)。

    候选工具链按优先级挑（见 toolchain.py）：PATH 里排第一的往往正是 CLion 自带那套
    残废 MinGW 15.2.0（-static 必失败），所以先找便携工具链，再兜底 PATH。
    """
    from toolchain import gxx_candidates
    cands = gxx_candidates()
    if not cands:
        return None, None, None

    libs = []
    for e in extra:
        if e.lower().endswith(".lib"):
            libs.append("-l" + e[:-4])
    common = ["-std=c++17", "-O2", "-s", "-o", out, src] + libs

    gxx = cands[0]
    # 先试真·全静态（一个 DLL 都不带）；该工具链不支持就退回半静态，
    # 之后 ship_runtime_dll 会把 libwinpthread-1.dll 补到 exe 旁边。
    for flags in (["-static"], ["-static-libgcc", "-static-libstdc++"]):
        cmd = [gxx] + common + flags
        r = subprocess.run(cmd, capture_output=True, text=True, errors="replace",
                           cwd=os.path.dirname(out))
        if r.returncode == 0:
            return cmd, None, r
    return cmd, None, r          # 全失败：返回最后一次结果供调用方报错


WINPTHREAD = b"libwinpthread-1.dll"

def ship_runtime_dll(exe, gxx):
    """半静态产物仍会导入 libwinpthread-1.dll（libstdc++.a 的 guard.o / eh_alloc.o
    和 libgcc 的 emutls.o 都要 pthread_mutex_lock）。从该工具链目录拷一份到 exe 旁边。"""
    if not gxx or not os.path.isfile(exe):
        return
    # 探针只在本机跑（工具链的 bin 在 PATH 里），不随附 DLL，免得污染 git。
    probe_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "probe")
    if os.path.dirname(os.path.abspath(exe)).lower() == probe_dir.lower():
        return
    if WINPTHREAD not in open(exe, "rb").read():
        return
    src_dll = os.path.join(os.path.dirname(os.path.abspath(gxx)), "libwinpthread-1.dll")
    if not os.path.isfile(src_dll):
        return
    dst = os.path.join(os.path.dirname(exe), "libwinpthread-1.dll")
    if (not os.path.exists(dst)) or os.path.getmtime(src_dll) > os.path.getmtime(dst):
        shutil.copy2(src_dll, dst)
    print("[dll] 已随附 libwinpthread-1.dll（半静态链的运行时依赖）")


gxx = None
plan = msvc_plan()
if plan is not None:
    cmd, env = plan
    print("[cl]", " ".join(cmd))
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, errors="replace",
                       cwd=os.path.dirname(out))
else:
    cmd, env, r = gcc_build()
    if cmd is None:
        raise SystemExit("既没有可用的 cl.exe，也找不到 g++ —— 无法编译")
    print("[g++]", " ".join(cmd))
    gxx = cmd[0]

print(r.stdout[-6000:])
if r.returncode != 0:
    print(r.stderr[-6000:], file=sys.stderr)
    raise SystemExit("编译失败 exit={}".format(r.returncode))
ship_runtime_dll(out, gxx)
print("[build] OK ->", out)
