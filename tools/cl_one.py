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


def gcc_plan():
    """MSVC 缺席时用 g++。只翻译库名，MSVC 专用开关一律丢弃。"""
    gxx = shutil.which("g++")
    if not gxx:
        return None, None
    libs = []
    for e in extra:
        if e.lower().endswith(".lib"):
            libs.append("-l" + e[:-4])
    cmd = [gxx, "-std=c++17", "-O2", "-o", out, src] + libs
    return cmd, None


plan = msvc_plan()
if plan is not None:
    cmd, env = plan
    print("[cl]", " ".join(cmd))
else:
    cmd, env = gcc_plan()
    if cmd is None:
        raise SystemExit("既没有可用的 cl.exe，也找不到 g++ —— 无法编译")
    print("[g++]", " ".join(cmd))

r = subprocess.run(cmd, env=env, capture_output=True, text=True, errors="replace",
                   cwd=os.path.dirname(out))
print(r.stdout[-6000:])
if r.returncode != 0:
    print(r.stderr[-6000:], file=sys.stderr)
    raise SystemExit("编译失败 exit={}".format(r.returncode))
print("[build] OK ->", out)
