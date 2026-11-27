#!/usr/bin/env python3
# =============================================================================
#  aino Android APK 打包脚本（无 Gradle：cmake + aapt2 + zipalign + apksigner）
#
#  用法：
#    python3 android/build_apk.py                # 构建出 build/aino-gallery.apk
#    python3 android/build_apk.py --install      # 构建并 adb install -r
#    python3 android/build_apk.py --clean        # 先清空构建目录
#
#  依赖（均在 Android SDK 内，无需 Gradle/JAVA 项目）：
#    NDK（clang 工具链 + native_app_glue）、build-tools（aapt2/zipalign/apksigner）、
#    platforms/android-35/android.jar、Java keytool（生成调试签名用）。
# =============================================================================
import argparse
import os
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent          # 仓库根
APP_DIR = Path(__file__).resolve().parent              # android/
BUILD = APP_DIR / "build"
OUT_APK = BUILD / "aino-gallery.apk"
ABI = "arm64-v8a"


def find_tool(name: str) -> str:
    """在 SDK 里找 aapt2 / zipalign / apksigner / adb / d8。
    Windows 上这些工具带 .bat / .exe 后缀，按候选后缀依次查找。"""
    suffixes = ["", ".bat", ".exe", ".cmd"]
    for bt in sorted((SDK / "build-tools").glob("*"), reverse=True):
        for suffix in suffixes:
            candidate = bt / f"{name}{suffix}"
            if candidate.exists():
                return str(candidate)
    sys.exit(f"[build_apk] 在 {SDK}/build-tools 下找不到 {name}")


def detect_sdk() -> Path:
    for key in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        if os.environ.get(key):
            return Path(os.environ[key])
    default = Path.home() / "Android/Sdk"
    if default.exists():
        return default
    sys.exit("[build_apk] 找不到 Android SDK：设置 ANDROID_HOME")


SDK = detect_sdk()
NDK = Path(os.environ.get("ANDROID_NDK_HOME", SDK / "ndk" / "26.3.11579264"))
if not NDK.exists():
    candidates = sorted((SDK / "ndk").glob("*"))
    if not candidates:
        sys.exit("[build_apk] 找不到 NDK：请安装或设置 ANDROID_NDK_HOME")
    NDK = candidates[-1]
GLUE = NDK / "sources/android/native_app_glue"
PLATFORM_JAR = SDK / "platforms" / "android-35" / "android.jar"


def run(cmd, **kw):
    print(f"[build_apk] {' '.join(str(c) for c in cmd)}")
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def cmake_binary() -> str:
    bundled = sorted((SDK / "cmake").glob("*/bin/cmake"), reverse=True)
    if bundled:
        return str(bundled[0])
    return "cmake"   # 回退到系统 cmake


def build_so() -> Path:
    """cmake + ninja 编出 libmain.so。"""
    ninja = sorted((SDK / "cmake").glob("*/bin/ninja"), reverse=True)
    run([cmake_binary(), "-G", "Ninja", "-S", APP_DIR, "-B", BUILD / "obj",
         f"-DGLUE_DIR={GLUE}",
         f"-DCMAKE_TOOLCHAIN_FILE={NDK}/build/cmake/android.toolchain.cmake",
         f"-DANDROID_ABI={ABI}", "-DANDROID_PLATFORM=android-29",
         "-DCMAKE_BUILD_TYPE=Release",
         f"-DCMAKE_MAKE_PROGRAM={ninja[0] if ninja else 'ninja'}"])
    run([cmake_binary(), "--build", BUILD / "obj", "--target", "main"])
    so = BUILD / "obj" / "libmain.so"
    if not so.exists():
        sys.exit("[build_apk] libmain.so 未生成")
    print(f"[build_apk] libmain.so {so.stat().st_size / 1024:.0f} KiB")
    return so


def build_dex():
    """ecj（Eclipse 编译器，单 jar 跑在本机 JRE 上）+ d8 编出软键盘桥的
    classes.dex（Java 源码在 app/java/）。系统只有 JRE 没有 javac 也能工作；
    ecj 首次使用放到 ~/aino-tools/ecj.jar（约 3MB，Maven Central）。"""
    java_dir = APP_DIR / "app/java"
    sources = sorted(java_dir.rglob("*.java"))
    if not sources:
        return None
    java_home = Path(os.environ.get("JAVA_HOME", "/usr/lib/jvm/java-21-openjdk-amd64"))
    java = str(java_home / "bin/java")
    classes = BUILD / "classes"
    classes.mkdir(exist_ok=True)

    ecj = Path.home() / "aino-tools/ecj.jar"
    if not ecj.exists():
        ecj.parent.mkdir(exist_ok=True)
        url = os.environ.get("AINO_ECJ_URL",
                             "https://repo1.maven.org/maven2/org/eclipse/jdt/ecj/3.33.0/ecj-3.33.0.jar")
        run(["curl", "-fsSL", "--connect-timeout", "20", "-o", ecj, url])
    run([java, "-jar", ecj, "-source", "8", "-target", "8", "-encoding", "UTF-8",
         "-classpath", PLATFORM_JAR, "-d", classes] + sources)
    d8 = find_tool("d8")
    run([d8, "--release", "--lib", PLATFORM_JAR, "--output", str(BUILD),
         *[str(c) for c in sorted(classes.rglob("*.class"))]],
        env=dict(os.environ, JAVA_HOME=str(java_home)))
    dex = BUILD / "classes.dex"
    if not dex.exists():
        sys.exit("[build_apk] classes.dex 未生成")
    return dex


def package_apk(so: Path, dex) -> None:
    """aapt2 链资源 → 打入 so / classes.dex → 对齐 → 签名。"""
    aapt2 = find_tool("aapt2")
    compiled = BUILD / "res.flata"
    run([aapt2, "compile", "--dir", APP_DIR / "app/res", "-o", compiled])
    run([aapt2, "link", "-o", BUILD / "base.apk",
         "-I", PLATFORM_JAR,
         "--manifest", APP_DIR / "app/AndroidManifest.xml",
         "--min-sdk-version", "29", "--target-sdk-version", "35",
         "--auto-add-overlay",
         compiled])

    # 注入 lib/<abi>/libmain.so 与 classes.dex（软键盘桥）。
    # extractNativeLibs=true（manifest 已声明），允许压缩存储，无对齐要求 ——
    # zipalign 的 4 字节对齐即可。
    staged = BUILD / "staged.apk"
    with zipfile.ZipFile(BUILD / "base.apk") as src, \
         zipfile.ZipFile(staged, "w", zipfile.ZIP_DEFLATED) as dst:
        for item in src.infolist():
            dst.writestr(item, src.read(item.filename))
        dst.writestr(f"lib/{ABI}/libmain.so", so.read_bytes())
        if dex is not None:
            dst.writestr("classes.dex", dex.read_bytes())

    zipalign = find_tool("zipalign")
    run([zipalign, "-f", "4", staged, BUILD / "aligned.apk"])

    keystore = Path.home() / ".android/debug.keystore"
    if not keystore.exists():
        keytool = os.environ.get("JAVA_HOME", "/usr/lib/jvm/java-21-openjdk-amd64")
        run([Path(keytool) / "bin/keytool", "-genkeypair",
             "-keystore", keystore, "-alias", "androiddebugkey",
             "-storepass", "android", "-keypass", "android",
             "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
             "-dname", "CN=Android Debug,O=Android,C=US"])
    apksigner = find_tool("apksigner")
    java_home = os.environ.get("JAVA_HOME", "/usr/lib/jvm/java-21-openjdk-amd64")
    env = dict(os.environ, JAVA_HOME=java_home)
    run([apksigner, "sign", "--ks", keystore,
         "--ks-pass", "pass:android", "--key-pass", "pass:android",
         "--out", OUT_APK, BUILD / "aligned.apk"], env=env)
    print(f"[build_apk] 完成 → {OUT_APK} ({OUT_APK.stat().st_size / 1024:.0f} KiB)")


def adb() -> str:
    return str(SDK / "platform-tools/adb")


def main() -> None:
    parser = argparse.ArgumentParser(description="打包 aino Android APK")
    parser.add_argument("--install", action="store_true", help="构建后 adb install -r")
    parser.add_argument("--launch", action="store_true", help="安装后启动（隐含 --install）")
    parser.add_argument("--clean", action="store_true", help="清理构建目录后构建")
    args = parser.parse_args()

    BUILD.mkdir(exist_ok=True)
    if args.clean:
        import shutil
        shutil.rmtree(BUILD, ignore_errors=True)
        BUILD.mkdir(exist_ok=True)

    so = build_so()
    package_apk(so, build_dex())

    if args.install or args.launch:
        run([adb(), "install", "-r", OUT_APK])
    if args.launch:
        run([adb(), "shell", "am", "start",
             "-n", "dev.aino.mobilegallery/android.app.NativeActivity"])


if __name__ == "__main__":
    main()
