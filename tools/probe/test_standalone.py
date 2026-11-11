"""将 EXE 复制到临时目录，清除工具链 PATH 后检查能否启动。

仅验证本机可用的运行环境；/MD 程序仍可能使用本机安装的 VC++ 运行库。

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
    # 排除开发工具目录中的 DLL，保留系统运行环境。
    windows = os.environ.get("SystemRoot", r"C:\Windows")
    env["PATH"] = os.pathsep.join((os.path.join(windows, "System32"), windows,
                                 os.path.join(windows, "System32", "Wbem")))
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
