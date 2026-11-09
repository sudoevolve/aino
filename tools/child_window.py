"""多窗口：子窗口在主窗口运行中打开后，主窗口还能不能正常响应。

这条是回归项，对应一个真实 bug：`detail::win_timer()` 原本是进程级单例
（`static`，注释还写着"主循环是单线程的"），而画廊"在新窗口运行这段代码"
是另起线程再建一个 Window。两个线程共用一个可等待定时器时，A 设的到期时间
会被 B 的 Set 覆盖，信号被 B 取走后 A 还卡在 WaitForSingleObject(INFINITE)，
而 B 建好窗口就空闲进了 WaitMessage —— 主窗口于是**永久假死**，子窗口却是好的。

判据：向主窗口发 WM_NULL 并量回执时间（SendMessageTimeout）。消息循环活着
就是 0~几毫秒；假死就是打满超时。这比"看画面有没有变"可靠 —— 后台窗口
本来就不会因为鼠标位置变化而重绘。

    python tools/child_window.py            # 判定
    python tools/child_window.py --shot     # 顺手存一张子窗口截图
"""
import ctypes
import os
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
    for rel in ("build/Release/aino_gallery.exe", "build/Debug/aino_gallery.exe",
                "build/aino_gallery.exe"):
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
    env["AINO_CHILD_AT"] = "2"        # 第 2 帧（主线程正在绘制时）打开子窗口
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

        # 等子窗口真的出现（最多 6 秒）
        kids = []
        for _ in range(60):
            kids = [w for w in windows_of(p.pid) if w[0] != hwnd]
            if kids:
                break
            time.sleep(0.1)
        if not kids:
            print("[FAIL] 子窗口没起来（AINO_CHILD_AT 钩子失效？）")
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
                sc = client_scale(h2)
                s = Shot(h2, sc)
                save_png(s.raw, s.w, s.h, os.path.join(ROOT, "build", "shots",
                                                       "child-window.png"))
                print("子窗口「%s」%.0f ms，截图 build/shots/child-window.png"
                      % (title, ms2))
            else:
                print("子窗口「%s」%.0f ms" % (title, ms2))
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
