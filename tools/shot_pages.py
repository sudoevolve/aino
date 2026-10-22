"""逐页截整窗图，人工核对版式（内容有没有压到卡片下边框 / 标题条）。

不属于回归：回归跑的是像素断言，不看截图。这个脚本是给"改完版式先看一眼"
用的 —— 用 AINO_START_PAGE 直接翻到目标页，不去点导航。

    python tools/shot_pages.py                 # 全部页
    python tools/shot_pages.py Button Dialog   # 只拍这几页
"""

import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import verify_render as vr            # noqa: E402  抓图 / 找窗口
from verify_gallery import Shot, save_png, client_scale   # noqa: E402

EXE = os.path.join(ROOT, "build", "aino_gallery.exe")
OUT_DIR = os.path.join(ROOT, "build", "shots")


def names():
    """从 gallery 源码里抠出页名顺序（避免在 Python 里再抄一份）。

    页表每项长这样：{"基础", "Button", "描述…", "shadcn", kBasic, COUNT(kBasic)}
    所以取第 2 个字符串；第 1 个是分组名。
    """
    import re
    src = open(os.path.join(ROOT, "examples", "gallery.cpp"),
               encoding="utf-8", errors="replace").read()
    m = re.search(r"const Page kPages\[\] = \{(.*?)\n\};", src, re.S)
    if not m:
        return []
    return re.findall(r'\{\s*"[^"]*"\s*,\s*"([^"]*)"', m.group(1))


def main():
    if not os.path.exists(EXE):
        print("找不到 %s，先跑 tools/build.py" % EXE)
        return 2
    want = sys.argv[1:]
    pages = names()
    if not pages:
        print("没从源码里读到页名，页表结构变了？")
        return 2
    os.makedirs(OUT_DIR, exist_ok=True)

    todo = [(i, n) for i, n in enumerate(pages) if not want or n in want]
    for idx, name in todo:
        env = dict(os.environ, AINO_START_PAGE=str(idx))
        proc = subprocess.Popen([EXE], env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            hwnd = None
            for _ in range(60):
                hwnd = vr.find_window_by_pid(proc.pid)
                if hwnd:
                    break
                time.sleep(0.1)
            if not hwnd:
                print("  %-14s 窗口没起来" % name)
                continue
            time.sleep(1.8)                       # 等字体与首帧
            scale = client_scale(hwnd)
            s = Shot(hwnd, scale)
            path = os.path.join(OUT_DIR, "%02d-%s.png" % (idx, name.replace(" ", "_")))
            save_png(s.raw, s.w, s.h, path)
            print("  %-14s -> %s" % (name, os.path.relpath(path, ROOT)))
        finally:
            try:
                proc.terminate(); proc.wait(timeout=5)
            except Exception:
                proc.kill()
    return 0


if __name__ == "__main__":
    sys.exit(main())
