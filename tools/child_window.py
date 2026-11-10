"""验证素材子窗口的打开、创建、取消与主窗口回填。

使用 WM_NULL 回执检查两个消息循环的响应，防止共享帧定时器导致阻塞。
python tools/child_window.py --shot 可同时保存子窗口截图。
"""
import ctypes
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import verify_render as vr                                   # noqa: E402
from verify_gallery import Shot, save_png, client_scale      # noqa: E402

user32 = ctypes.windll.user32
TIMEOUT_MS = 600          # 修复前实测打满 1500ms（默认值），正常是 0~2ms


def _pick_exe():
    for rel in ("build/aino_gallery.exe", "build/Release/aino_gallery.exe",
                "build/Debug/aino_gallery.exe"):
        p = os.path.join(ROOT, rel)
        if os.path.exists(p):
            return p
    return None


def windows_of(pid):
    out = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def cb(hwnd, _lp):
        q = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(q))
        if q.value == pid and user32.IsWindowVisible(hwnd):
            n = user32.GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(hwnd, buf, n + 1)
            out.append((hwnd, buf.value))
        return True

    user32.EnumWindows(cb, 0)
    return out


def ping(hwnd, timeout_ms=TIMEOUT_MS):
    res = ctypes.c_size_t()
    t0 = time.time()
    ok = user32.SendMessageTimeoutW(hwnd, 0, 0, 0, 0x0002, timeout_ms, ctypes.byref(res))
    return bool(ok), (time.time() - t0) * 1000.0


def main():
    want_shot = "--shot" in sys.argv
    exe = _pick_exe()
    if not exe:
        print("SKIP: 没找到 aino_gallery.exe")
        return 2

    env = dict(os.environ)
    env.pop("AINO_CHILD_AT", None)
    env["AINO_QUERY"] = "empty"
    env["AINO_START_PAGE"] = "advanced"
    env["AINO_NAV"] = "0"
    layout_path = os.path.join(ROOT, "build", "material-child-layout.txt")
    env["AINO_LAYOUT_REPORT"] = layout_path
    if os.path.exists(layout_path):
        os.remove(layout_path)
    p = subprocess.Popen([exe], env=env)
    try:
        hwnd = None
        for _ in range(80):
            hwnd = vr.find_window_by_pid(p.pid)
            if hwnd:
                break
            time.sleep(0.1)
        if not hwnd:
            print("SKIP: 主窗口起不来")
            return 2

        for _ in range(40):
            if os.path.exists(layout_path):
                break
            time.sleep(0.1)
        with open(layout_path, encoding="utf-8") as f:
            layout = f.read()
        card = re.search(r"空状态: x=([\d.]+) y=([\d.]+) w=([\d.]+)", layout)
        if not card:
            print("[FAIL] 空状态布局未找到")
            return 1
        # 清单记录内容坐标；固定工具栏高 60，内容水平内边距 26。
        x, y, width = map(float, card.groups())
        scale = client_scale(hwnd)
        time.sleep(0.5)
        vr.click_logical(hwnd, 278 + x + width / 2, 60 + y + 48 + 146, scale)

        # 等子窗口真的出现（最多 6 秒）
        kids = []
        for _ in range(60):
            kids = [w for w in windows_of(p.pid) if w[0] != hwnd]
            if kids:
                break
            time.sleep(0.1)
        if not kids:
            print("[FAIL] 点击新建素材后未打开子窗口")
            return 1

        time.sleep(1.5)               # 让它进空闲态：这才是假死暴露的时机
        alive, ms = ping(hwnd)
        print("窗口：主 + %d 个子窗口；主窗口 WM_NULL 回执 %.0f ms" % (len(kids), ms))
        if not alive:
            print("[FAIL] 主窗口不响应消息（多窗口共享定时器 → 补预算永久阻塞）")
            return 1

        for h2, title in kids:
            a2, ms2 = ping(h2)
            if not a2:
                print("[FAIL] 子窗口「%s」不响应消息" % title)
                return 1
            if want_shot:
                sc = client_scale(h2) * 1180 / 460
                s = Shot(h2, sc)
                save_png(s.raw, s.w, s.h, os.path.join(ROOT, "build", "shots",
                                                       "child-window.png"))
                print("子窗口「%s」%.0f ms，截图 build/shots/child-window.png"
                      % (title, ms2))
            else:
                print("子窗口「%s」%.0f ms" % (title, ms2))
        child = kids[0][0]
        scale_child = client_scale(child) * 1180 / 460
        vr.click_logical(child, 396, 302, scale_child)
        time.sleep(0.2)
        if not user32.IsWindow(child):
            print("[FAIL] 空名称不应允许创建")
            return 1
        vr.click_logical(child, 80, 106, scale_child)
        for ch in "Material test":
            user32.PostMessageW(child, 0x0102, ord(ch), 0)
        time.sleep(0.3)
        before = Shot(hwnd, scale)
        vr.click_logical(child, 396, 302, scale_child)
        for _ in range(40):
            if not [w for w in windows_of(p.pid) if w[0] != hwnd]:
                break
            time.sleep(0.1)
        else:
            print("[FAIL] 创建后子窗口未关闭")
            return 1
        time.sleep(0.3)
        after = Shot(hwnd, scale)
        changed = before.diff(after, 278 + x, 60 + y + 90,
                              278 + x + width, 60 + y + 160)
        if not changed:
            print("[FAIL] 创建素材后主窗口未更新")
            return 1
        print("[ ok ] 点击打开子窗口，创建后主窗口更新")
        vr.click_logical(hwnd, 278 + x + width / 2, 60 + y + 48 + 146, scale)
        for _ in range(40):
            reopened = [w for w in windows_of(p.pid) if w[0] != hwnd]
            if reopened:
                break
            time.sleep(0.1)
        else:
            print("[FAIL] 创建后无法再次打开子窗口")
            return 1
        cancel = reopened[0][0]
        vr.click_logical(cancel, 306, 302, client_scale(cancel) * 1180 / 460)
        time.sleep(0.3)
        if user32.IsWindow(cancel):
            print("[FAIL] 取消未关闭子窗口")
            return 1
        print("[ ok ] 空名称禁用创建，支持重新打开与取消")
        print("[ ok ] 多窗口共存正常")
        return 0
    finally:
        try:
            p.terminate()
            p.wait(timeout=5)
        except Exception:
            p.kill()


if __name__ == "__main__":
    sys.exit(main())
