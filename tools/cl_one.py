"""用 cl.exe 编译单个 C++ 文件；MSVC 不可用时回退到 g++。

用法：python cl_one.py <src.cpp> <out.exe> [额外库或开关...]
默认链接 user32、gdi32、msimg32。探针保留 assert。
通过绝对路径启动 cl.exe；g++ 回退分支转换库参数并静态链接 C++ 运行时。
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
    # /DNOMINMAX：探针常在 include aino.hpp 之前先 include windows.h，那会儿
    # 库自己的 NOMINMAX 还没生效，min/max 宏会把头里的 std::min/std::max 撕裂。
    # 这是构建参数该管的事，不必要求每个探针都记得手动写。
    # /utf-8：源码里到处是中文注释和中文字面量。不写这个开关，MSVC 按系统代码页
    # （936/GBK）解析 UTF-8 字节，中文字符串会被吞掉换行、直接报 C2001「常量中有换行符」。
    # g++ 默认就是 UTF-8，所以这个坑只在 MSVC 蹲着 —— 表现为同一份代码一会儿能编一会儿不能。
    cmd = [cl, "/nologo", "/std:c++17", "/utf-8", "/EHsc", "/O2", "/W3", "/DNDEBUG", "/DNOMINMAX",
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
