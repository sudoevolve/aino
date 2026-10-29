"""构建 SSH 客户端和它的端到端探针（MSVC + 静态 libssh2）。

为什么单独一个脚本：
  cl_one.py 是为了编单个"只依赖 aino.hpp"的探针写的，它把工作目录切到 exe 所在
  目录、也不处理多级 include。ssh_client 需要
    /I <libssh2>/include
    /I <libssh2 构建目录>     （libssh2_config.h 是 cmake 生成的）
    /MD                       （libssh2.lib 是用动态 CRT 编的，/MT 会撞 __imp_* 未解析）
  这几个约束写在命令里容易漏，索性固化成脚本。

libssh2 从哪来：
  build/third_party/_mk.py 会下载 1.11.1 源码并用 Ninja+MSVC 编成静态库
  （Windows 走 WinCNG，不依赖 OpenSSL）。跑之前先确认这些文件在：
    build/third_party/libssh2-1.11.1/include/libssh2.h
    build/third_party/l2build/src/libssh2.lib
    build/third_party/l2build/src/libssh2_config.h

用法：
  python tools/build_ssh.py            # 编 app + probe
  python tools/build_ssh.py app        # 只编 app
  python tools/build_ssh.py probe      # 只编 probe
  python tools/build_ssh.py term       # 只编纯解析器测试（不连网）
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from msvc_build import msvc_env  # noqa: E402

L2_SRC = os.path.join(ROOT, "build", "third_party", "libssh2-1.11.1", "include")
# 加密后端用 mbedTLS（静态）而不是 WinCNG：
#   WinCNG 版本编出来的 libssh2 里 LIBSSH2_ECDSA / LIBSSH2_ED25519 都是关的，
#   也就是只有 diffie-hellman-group* 一族 KEX。而 OpenSSH 10.x 已经把
#   diffie-hellman-group* 全部停用，只留 curve25519 / ecdh —— 结果是零交集，
#   握手报 "Unable to exchange encryption keys"，表现就是 "VSCode 能连、这里连不上"。
# 为什么不用 OpenSSL：官方预编译包只有 import lib（要带 libcrypto-3-x64.dll），
#   单 exe 拷到别的机器就 0xC0000135；静态编译 OpenSSL 又卡在 Perl 模块上。
#   mbedTLS 自带 curve25519/ECDH/AES/ChaCha，且能直接编成 .lib，天然单文件。
L2_BUILD = os.path.join(ROOT, "build", "third_party", "l2mbed", "src")
L2_LIB = os.path.join(L2_BUILD, "libssh2.lib")
MBED_DIR = os.path.join(ROOT, "build", "third_party", "mbedtls-3.6.2")
MBED_INC = os.path.join(MBED_DIR, "include")
MBED_LIB = os.path.join(MBED_DIR, "mbedtls_all.lib")


def check_deps():
    missing = [p for p in (os.path.join(L2_SRC, "libssh2.h"), L2_BUILD + "/libssh2_config.h",
                           L2_LIB, MBED_LIB)
               if not os.path.isfile(p)]
    if missing:
        print("缺依赖产物：")
        for m in missing:
            print("  " + m)
        print("先跑：python build/third_party/_mk.py")
        raise SystemExit(1)


def build(src, out, extra_flags=(), asan=False, gui=False):
    env = msvc_env()
    cl = os.path.join(env.get("VCToolsInstallDir", ""), "bin", "Hostx64", "x64", "cl.exe")
    if not os.path.isfile(cl):
        raise SystemExit("找不到 cl.exe（VCToolsInstallDir 没导出来）")
    cmd = [cl, "/nologo", "/std:c++17", "/utf-8", "/EHsc", "/W3",
           "/DNDEBUG", "/DNOMINMAX", "/MD"]
    if asan:
        # 堆损坏（0xC0000374）靠肉眼找不到真凶时用这个：ASan 会在**越界写的那一行**
        # 直接报栈，而不是等后面某次分配才炸。必须配 /Od /Zi，优化会把越界写合并掉。
        cmd += ["/fsanitize=address", "/Od", "/Zi"]
    else:
        cmd += ["/O2"]
    cmd += ["/I" + L2_SRC, "/I" + L2_BUILD, "/I" + MBED_INC, "/Fe:" + out, src]
    cmd += list(extra_flags)
    # mbedtls_all.lib：curve25519 / ecdh / AES / ChaCha 都在里面，而且全是静态链接，
    # 所以 exe 拷到任何机器都能单独跑（不再依赖 libcrypto-3-x64.dll）
    cmd += [L2_LIB, MBED_LIB, "user32.lib", "gdi32.lib", "msimg32.lib", "ws2_32.lib", "crypt32.lib",
            "bcrypt.lib",     # mbedTLS 的熵源用 BCryptGenRandom
            "shell32.lib", "ole32.lib"]     # shell32/ole32：SHBrowseForFolder 选下载目录
    if gui:
        # GUI 程序必须声明 WINDOWS 子系统，否则双击/启动会先弹一个黑色控制台窗口
        # （就是"启动还开着个终端"）。入口是 WinMain 转发，见 ssh_client.cpp。
        # 自检（AINO_SSH_SELFTEST）时自己 AttachConsole，输出仍能在 shell 里看到。
        cmd += ["/link", "/SUBSYSTEM:WINDOWS"]
    os.makedirs(os.path.dirname(out), exist_ok=True)
    print("[cl]", os.path.basename(src), "->", os.path.relpath(out, ROOT), "(asan)" if asan else "")
    # cwd 固定成仓库根：探针里有 "build/sshtest_probe.txt" 这种相对路径
    r = subprocess.run(cmd, env=env, capture_output=True, text=True,
                       errors="replace", cwd=ROOT)
    if r.returncode != 0:
        print(r.stdout[-4000:])
        print(r.stderr[-4000:], file=sys.stderr)
        raise SystemExit("编译失败 exit={}".format(r.returncode))
    warn = [ln for ln in (r.stdout or "").splitlines() if "warning" in ln.lower()]
    for w in warn[:10]:
        print("  " + w.strip())
    print("[build] OK ->", os.path.relpath(out, ROOT))


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    asan = "--asan" in sys.argv
    what = (argv[0] if argv else "all").lower()
    check_deps()
    if what in ("all", "app"):
        build(os.path.join(ROOT, "examples", "ssh_client.cpp"),
              os.path.join(ROOT, "build", "Release", "aino_ssh.exe"), asan=asan, gui=True)
    if what in ("all", "probe"):
        build(os.path.join(ROOT, "tools", "probe", "ssh_probe.cpp"),
              os.path.join(ROOT, "tools", "probe", "ssh_probe.exe"), asan=asan)
    if what in ("all", "term"):
        build(os.path.join(ROOT, "tools", "probe", "term_scroll.cpp"),
              os.path.join(ROOT, "tools", "probe", "term_scroll.exe"), asan=asan)
    if what in ("all", "hxprobe"):
        build(os.path.join(ROOT, "tools", "probe", "term_hx.cpp"),
              os.path.join(ROOT, "tools", "probe", "term_hx.exe"), asan=asan)
    if what in ("all", "shellcmd"):
        build(os.path.join(ROOT, "tools", "probe", "shell_cmd.cpp"),
              os.path.join(ROOT, "tools", "probe", "shell_cmd.exe"), asan=asan)
    if what in ("all", "wrap"):
        build(os.path.join(ROOT, "tools", "probe", "term_wrap.cpp"),
              os.path.join(ROOT, "tools", "probe", "term_wrap.exe"), asan=asan)
    if what in ("all", "replay"):
        build(os.path.join(ROOT, "tools", "probe", "term_replay.cpp"),
              os.path.join(ROOT, "tools", "probe", "term_replay.exe"), asan=asan)
    if what in ("all", "dc"):
        build(os.path.join(ROOT, "tools", "probe", "dc_repro.cpp"),
              os.path.join(ROOT, "tools", "probe", "dc_repro.exe"), asan=asan)
    if what in ("all", "doctor"):
        build(os.path.join(ROOT, "tools", "probe", "sftp_doctor.cpp"),
              os.path.join(ROOT, "tools", "probe", "sftp_doctor.exe"), asan=asan)


if __name__ == "__main__":
    main()
