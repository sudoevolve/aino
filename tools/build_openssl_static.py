"""静态编译 OpenSSL（只出 libcrypto 静态库），给 libssh2 用。

为什么必须静态：一开始用的是预编译包的 import lib，结果 exe 旁边少了
libcrypto-3-x64.dll 就直接 0xc0000135 打不开 —— 用户拷一个 exe 到别的机器
就废了。静态链接才是"单文件 exe"。

参数：
  no-shared   只出静态库
  no-asm      不依赖 NASM（本机没装也能编）
  no-tests / no-apps  跳过测试和 openssl.exe，编译时间砍一半
  no-legacy   不要老算法（MD2/RC4 之类），减小体积
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from msvc_build import msvc_env  # noqa: E402

SRC = os.path.join(ROOT, "build", "third_party", "openssl-3.5.1-src")
PREFIX = os.path.join(ROOT, "build", "third_party", "openssl-static")
PERL = r"C:\Program Files\Git\usr\bin\perl.exe"


def run(cmd, env, cwd):
    print("[cmd]", " ".join(cmd[:6]), "...")
    p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, errors="replace")
    out = p.stdout or ""
    print(out[-3000:])
    print("[exit]", p.returncode)
    return p.returncode


def main():
    env = dict(msvc_env())
    prefix_fwd = PREFIX.replace("\\", "/")

    rc = run([PERL, "Configure", "VC-WIN64A", "no-shared", "no-asm",
              "no-tests", "no-apps", "no-legacy",
              "--prefix=" + prefix_fwd,
              "--openssldir=" + prefix_fwd + "/ssl"], env, SRC)
    if rc != 0:
        raise SystemExit("Configure 失败")

    rc = run(["nmake", "build_libs"], env, SRC)
    if rc != 0:
        raise SystemExit("nmake 失败")
    print("[OK] 静态库在", SRC)


if __name__ == "__main__":
    main()
