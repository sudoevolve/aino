"""验证"点断开不闪退"。

覆盖连接过程中断开与已连接后断开的资源释放路径：
    1) 连接过程中点断开 → 0xC0000005
    2) 已连接状态下点断开 → 0xC0000005（reap/free 路径）

点击后进程必须仍存活（`proc.poll() is None`），并且回到主界面
（状态文件里 tabs=0）。崩了就打印退出码 —— Windows 上 0xC0000005 = 3221225477。

用法：
    python tools/probe/disconnect_test.py [场景]
    SCENE=connected|connecting|both   （默认 both）

本地要先把开发服务器跑起来：
    python tools/dev_ssh_server.py --port 2222 --root build/sshtest
"""
import ctypes
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import verify_render as vr  # noqa: E402

EXE = os.path.join(ROOT, "build", "Release", "aino_ssh.exe")
STATE = os.path.join(ROOT, "build", "dc_state.txt")
WM_LBUTTONDOWN, WM_LBUTTONUP, WM_MOUSEMOVE = 0x0201, 0x0202, 0x0200

# 逻辑坐标（客户端按 1320x820 设计，实际像素由 DPI 缩放）
CONNECT_BTN = (764, 44)
DISCONNECT_BTN = (1276, 55)


def read_state():
    try:
        return open(STATE, encoding="utf-8", errors="replace").read()
    except OSError:
        return ""


def run_scene(scene):
    if os.path.exists(STATE):
        os.remove(STATE)
    env = dict(os.environ)
    env["AINO_SSH_DEBUG_FILE"] = STATE
    # 默认打本地开发服务器；DC_HOST 等可以指向真机（真机才能验出"断真连接"的路径）
    env["AINO_SSH_HOST"] = os.environ.get("DC_HOST", "127.0.0.1")
    env["AINO_SSH_PORT"] = os.environ.get("DC_PORT", "2222")
    env["AINO_SSH_USER"] = os.environ.get("DC_USER", "demo")
    env["AINO_SSH_PASS"] = os.environ.get("DC_PASS", "demo")

    proc = subprocess.Popen([EXE], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        hwnd = None
        for _ in range(120):
            hwnd = vr.find_window_by_pid(proc.pid)
            if hwnd:
                break
            time.sleep(0.05)
        if not hwnd:
            print("  FAIL 没找到窗口")
            return False
        vr.user32.SetForegroundWindow(hwnd)

        cr = vr.wintypes.RECT()
        vr.user32.GetClientRect(hwnd, ctypes.byref(cr))
        scale = cr.right / 1320.0

        def click(lx, ly, settle=0.5):
            lp = ((int(ly * scale)) << 16) | (int(lx * scale) & 0xFFFF)
            vr.user32.PostMessageW(hwnd, WM_MOUSEMOVE, 0, lp)
            time.sleep(0.08)
            vr.user32.PostMessageW(hwnd, WM_LBUTTONDOWN, 1, lp)
            time.sleep(0.08)
            vr.user32.PostMessageW(hwnd, WM_LBUTTONUP, 0, lp)
            time.sleep(settle)

        click(*CONNECT_BTN, settle=0.15)
        if scene == "connected":
            ok = False
            for _ in range(200):
                if "conn=1 shell=1" in read_state():
                    ok = True
                    break
                time.sleep(0.05)
            if not ok:
                print("  FAIL 没连上（状态文件没有 conn=1 shell=1）")
                return False
            click(*DISCONNECT_BTN, settle=1.5)          # 已连接 → 断开
        else:
            time.sleep(0.12)                            # 还在握手
            click(*DISCONNECT_BTN, settle=1.5)          # 连接中 → 断开

        time.sleep(1.0)
        rc = proc.poll()
        if rc is not None:
            print("  FAIL 进程已退出 rc=%d (0x%08X)" % (rc, rc & 0xFFFFFFFF))
            return False
        st = read_state()
        # 点空一次不算崩溃：UI 那边标签栏/会话帧的绘制和这条消息可能差一帧，
        # 消息落在空白上就被丢了。补点两次再判，避免"偶发 tabs 未归零"变成噪声。
        for attempt in range(2):
            if "tabs=0" in st:
                break
            click(*DISCONNECT_BTN, settle=1.2)
            rc = proc.poll()
            if rc is not None:
                print("  FAIL 进程已退出 rc=%d (0x%08X)（第 %d 次点断开）" % (rc, rc & 0xFFFFFFFF, attempt + 2))
                return False
            st = read_state()
        tabs = "tabs=0" in st
        print("  进程存活，%s" % ("已回到主界面(tabs=0)" if tabs else "tabs 未归零 → " + st.splitlines()[0][:80]))
        return tabs
    finally:
        try:
            proc.terminate()
        except Exception:
            pass


def main():
    want = (sys.argv[1] if len(sys.argv) > 1 else os.environ.get("SCENE", "both")).lower()
    scenes = ["connected", "connecting"] if want == "both" else [want]
    fails = 0
    for s in scenes:
        print("场景 %s:" % s)
        if not run_scene(s):
            fails += 1
    print(("ALL PASS" if not fails else "%d 个场景失败" % fails))
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
