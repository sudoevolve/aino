"""验证"单个 exe 能不能独立跑"。

为什么需要：换成 OpenSSL 后端后，exe 一开始依赖 libcrypto-3-x64.dll，
用户把 exe 单独拷到别的目录就直接 0xc0000135 打不开。所以每次改依赖都要
把 exe 复制到一个空目录里真跑一遍 —— 只检查本目录能不能启动会漏掉这个问题。

用法：
    python tools/probe/test_standalone.py build/Release/aino_ssh.exe
"""
import ctypes
import os
import shutil
import subprocess
import sys
import tempfile
import time


def kill(pid):
    k = ctypes.windll.kernel32
    h = k.OpenProcess(1, False, pid)
    if h:
        k.TerminateProcess(h, 1)
        k.CloseHandle(h)


def main():
    exe = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build/Release/aino_ssh.exe")
    tmp = tempfile.mkdtemp(prefix="aino_standalone_")
    lone = os.path.join(tmp, os.path.basename(exe))
    shutil.copy2(exe, lone)
    print("复制到空目录:", tmp)

    env = {k: v for k, v in os.environ.items() if not k.startswith("AINO_SSH")}
    # 关键：PATH 必须清成纯系统目录。
    # 本机 PATH 里有 Git 的 mingw64/bin，那里躺着 libcrypto-3-x64.dll ——
    # 不清 PATH 的话，缺依赖也能"启动成功"，测试就变成自欺欺人了。
    env["PATH"] = r"C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem"
    p = subprocess.Popen([lone], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    alive = True
    for _ in range(40):          # 最多等 4 秒
        time.sleep(0.1)
        if p.poll() is not None:
            alive = False
            break

    code = p.poll()
    if alive:
        print("OK  单 exe 启动正常（进程存活）")
        kill(p.pid)
        rc = 0
    else:
        # 0xC0000135 = STATUS_DLL_NOT_FOUND，0xC000007B = 架构/依赖不匹配
        hint = {0xC0000135: "缺 DLL（STATUS_DLL_NOT_FOUND）",
                0xC000007B: "依赖或架构不匹配"}.get(code & 0xFFFFFFFF if code else 0, "")
        print("FAIL 单 exe 启动失败 exit=%s %s" % (code, hint))
        rc = 1
    try:
        shutil.rmtree(tmp, ignore_errors=True)
    except OSError:
        pass
    return rc


if __name__ == "__main__":
    sys.exit(main())
