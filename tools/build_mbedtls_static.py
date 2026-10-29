"""把 mbedTLS 编成一个静态库（脱离它的 CMake）。

为什么要绕开官方 CMake：mbedTLS 3.6.x 的构建需要 framework 子模块，
GitHub 的源码 tarball 里没有它，cmake 直接报 "framework/CMakeLists.txt not found"。
而我们要的东西很简单 —— 一堆 .c 编成 .lib 就行，顺手也省掉了 perl/子模块依赖。

产物：build/third_party/mbedtls-3.6.2/mbedtls_all.lib（mbedcrypto+x509+tls 全在一起，
链接器只会取用到的对象文件）。
"""
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from msvc_build import msvc_env  # noqa: E402

SRC = os.path.join(ROOT, "build", "third_party", "mbedtls-3.6.2")
OBJ = os.path.join(SRC, "obj")
OUT = os.path.join(SRC, "mbedtls_all.lib")


def main():
    env = msvc_env()
    cl = os.path.join(env.get("VCToolsInstallDir", ""), "bin", "Hostx64", "x64", "cl.exe")
    lib = os.path.join(env.get("VCToolsInstallDir", ""), "bin", "Hostx64", "x64", "lib.exe")
    os.makedirs(OBJ, exist_ok=True)

    srcs = sorted(glob.glob(os.path.join(SRC, "library", "*.c")))
    print("源文件 %d 个" % len(srcs))
    # 一次性把源文件列表喂给 cl 可能超过命令行长度限制，分批编
    objs = []
    BATCH = 25
    for i in range(0, len(srcs), BATCH):
        batch = srcs[i:i + BATCH]
        cmd = [cl, "/nologo", "/c", "/O2", "/MD", "/DNDEBUG", "/DMBEDTLS_ALLOW_PRIVATE_ACCESS",
               "/I", os.path.join(SRC, "include"), "/I", os.path.join(SRC, "library"),
               "/Fo" + OBJ + "\\"] + batch
        r = subprocess.run(cmd, env=env, cwd=SRC, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True, errors="replace")
        if r.returncode != 0:
            print(r.stdout[-4000:])
            raise SystemExit("编译失败于第 %d 批" % (i // BATCH + 1))
        print("  批次 %d/%d ok" % (i // BATCH + 1, (len(srcs) + BATCH - 1) // BATCH))

    objs = sorted(glob.glob(os.path.join(OBJ, "*.obj")))
    print("目标文件 %d 个" % len(objs))
    # 不删旧库：本机的 safe-delete 策略会拦截 os.remove 大文件，脚本会直接中断。
    # lib.exe 自己会覆盖同名输出。
    # 必须一次把所有 obj 交给 lib.exe。
    # 之前按 60 个一批、每批都带 /OUT 调用，结果每批都把库**重建**了一遍，
    # 最后只剩最后一批（bignum.c 排在前面，直接丢了）—— 链接期表现为
    # 一堆 mbedtls_mpi_* 未解析。用响应文件绕开命令行长度限制，只建一次。
    rsp = os.path.join(SRC, "mbedtls_all.rsp")
    with open(rsp, "w", encoding="utf-8") as f:
        f.write("\n".join('"%s"' % o for o in objs))
    cmd = [lib, "/nologo", "@" + rsp, "/OUT:" + OUT]
    r = subprocess.run(cmd, env=env, cwd=SRC, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.returncode != 0:
        print(r.stdout[-3000:])
        raise SystemExit("lib 失败")
    print("[OK]", OUT, "%.1f MB" % (os.path.getsize(OUT) / 1e6))


if __name__ == "__main__":
    main()
