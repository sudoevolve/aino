"""构建 SSH 客户端与探针（MSVC x64、静态 libssh2 + mbedTLS、静态 CRT）。

依赖准备见 docs/01-quickstart.md；本脚本不下载第三方源码。
ASan 构建从当前 MSVC 工具链复制运行库，不使用仓库内预编译 DLL。

用法：
  python tools/build_ssh.py            # 编 app + probe
  python tools/build_ssh.py app        # 只编 app
  python tools/build_ssh.py probe      # 只编 probe
  python tools/build_ssh.py term       # 只编纯解析器测试（不连网）
  python tools/build_ssh.py ui         # 只编控件组合测试（不连网、不读配置）
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from msvc_build import msvc_env  # noqa: E402

L2_SRC = os.path.join(ROOT, "build", "third_party", "libssh2-1.11.1", "include")
# libssh2、mbedTLS 和客户端统一使用 /MT；依赖更换 CRT 后必须重新构建。
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
        print("依赖准备与构建命令见 docs/01-quickstart.md 的可选 SSH 示例。")
        raise SystemExit(1)


def ship_asan_runtime(cl, out):
    runtime = os.path.join(os.path.dirname(cl), "clang_rt.asan_dynamic-x86_64.dll")
    if not os.path.isfile(runtime):
        raise SystemExit("当前 MSVC 工具链缺少 ASan x64 运行库：" + runtime)
    shutil.copy2(runtime, os.path.join(os.path.dirname(out), os.path.basename(runtime)))


def build(src, out, extra_flags=(), asan=False, gui=False):
    env = msvc_env()
    cl = os.path.join(env.get("VCToolsInstallDir", ""), "bin", "Hostx64", "x64", "cl.exe")
    if not os.path.isfile(cl):
        raise SystemExit("找不到 cl.exe（VCToolsInstallDir 没导出来）")
    cmd = [cl, "/nologo", "/std:c++17", "/utf-8", "/EHsc", "/W3",
           "/DNDEBUG", "/DNOMINMAX", "/MT"]
    if asan:
        cmd += ["/fsanitize=address", "/Od", "/Zi"]
    else:
        cmd += ["/O2"]
    cmd += ["/I" + L2_SRC, "/I" + L2_BUILD, "/I" + MBED_INC, "/Fe:" + out, src]
    cmd += list(extra_flags)
    cmd += [L2_LIB, MBED_LIB, "user32.lib", "gdi32.lib", "msimg32.lib", "ws2_32.lib", "crypt32.lib",
            "bcrypt.lib",     # mbedTLS 的熵源用 BCryptGenRandom
            "shell32.lib", "ole32.lib", "uuid.lib", "comdlg32.lib"]     # 系统文件/目录选择及 KnownFolder ID
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
    if asan:
        ship_asan_runtime(cl, out)
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
    if what in ("all", "ui"):
        build(os.path.join(ROOT, "tools", "probe", "example_ui_probe.cpp"),
              os.path.join(ROOT, "build", "ssh_ui_probe.exe"),
              extra_flags=("/DAINO_PROBE_SSH",), asan=asan)
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
