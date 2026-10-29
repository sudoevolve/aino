"""验证"按住方向键/退格能连发"。

为什么需要单独一个脚本：这件事没法靠截图断言（截图只能看出"动了"，看不出
"动了 5 次"）。做法是让远端跑 `cat -v` 把收到的字节原样回显，然后往窗口里
投递 1 次 WM_KEYDOWN + N 次带 lParam bit30 的 WM_KEYDOWN（Windows 的自动重复
就是这个形状），最后读 AINO_SSH_DEBUG_FILE 里的终端快照，数 `^[[D` 出现了几次。

用法：
    python tools/probe/hold_key.py [按键] [次数]

两种模式：
    HOLD_MODE=keys  连本地开发服务器（127.0.0.1:2222 demo/demo），发 `keys` 进
                    按键回显模式，远端每收一个键回一个 `K:LEFT` token。
                    **本地能跑，不依赖 192.168.12.131 那台机器在线。**
    HOLD_MODE=raw   （默认）连真机跑 `stty -icanon; cat -v` 数 `^[[D`。
"""
import ctypes
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import verify_render as vr  # noqa: E402

# HOLD_EXE 可以指向别处的 exe（例如桌面那份"用户正在用的"），
# 用来分辨"代码没修好"和"你跑的是旧 exe"。
EXE = os.environ.get("HOLD_EXE") or os.path.join(ROOT, "build", "Release", "aino_ssh.exe")
STATE = os.path.join(ROOT, "build", "hold_state.txt")
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_CHAR = 0x0102
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_MOUSEMOVE = 0x0200

VK = {"LEFT": 0x25, "UP": 0x26, "RIGHT": 0x27, "DOWN": 0x28, "BACKSPACE": 0x08,
      "DELETE": 0x2E, "ENTER": 0x0D}


def main():
    key = (sys.argv[1] if len(sys.argv) > 1 else "LEFT").upper()
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    vk = VK[key]
    mode = os.environ.get("HOLD_MODE", "raw")

    env = dict(os.environ)
    env["AINO_SSH_DEBUG_FILE"] = STATE
    if mode == "keys":
        env.setdefault("AINO_SSH_HOST", "127.0.0.1")
        env.setdefault("AINO_SSH_PORT", "2222")
        env.setdefault("AINO_SSH_USER", "demo")
        env.setdefault("AINO_SSH_PASS", "demo")
    else:
        env.setdefault("AINO_SSH_HOST", "192.168.12.131")
        env.setdefault("AINO_SSH_USER", "aino")
        env.setdefault("AINO_SSH_PASS", "123456")
    if os.path.exists(STATE):
        os.remove(STATE)

    proc = subprocess.Popen([EXE], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        hwnd = None
        for _ in range(100):
            hwnd = vr.find_window_by_pid(proc.pid)
            if hwnd:
                break
            time.sleep(0.05)
        if not hwnd:
            print("没找到窗口")
            return 2
        vr.user32.SetForegroundWindow(hwnd)

        # 等 shell 真的开起来（状态文件里 conn=1 shell=1）
        for _ in range(120):
            if os.path.exists(STATE):
                txt = open(STATE, encoding="utf-8", errors="replace").read()
                if "conn=1 shell=1" in txt:
                    break
            time.sleep(0.1)

        cr = vr.wintypes.RECT()
        vr.user32.GetClientRect(hwnd, ctypes.byref(cr))
        scale = cr.right / 1320.0

        def mouse(msg, lx, ly, btn=0):
            lp = ((int(ly * scale)) << 16) | (int(lx * scale) & 0xFFFF)
            vr.user32.PostMessageW(hwnd, msg, btn, lp)

        # 点一下终端，把焦点给它（坐标可用 HOLD_X/HOLD_Y 覆盖，方便排查"点歪了"）
        # 900,400 落在终端区中部；(800,450) 会蹭到左边 SFTP 面板的边，焦点给不过去，
        # 表现为"一个键都收不到"——踩过一次，记在这里。
        tx = float(os.environ.get("HOLD_X", "900"))
        ty = float(os.environ.get("HOLD_Y", "400"))
        # 三条消息之间必须有间隔：全塞进一帧里，库里可能一次看到"按下+抬起"，
        # 焦点判定就落空，后面的按键一个都收不到（现象是"0 次"，像极了连发没修好）。
        mouse(WM_MOUSEMOVE, tx, ty)
        time.sleep(0.08)
        mouse(WM_LBUTTONDOWN, tx, ty, 1)
        time.sleep(0.08)
        mouse(WM_LBUTTONUP, tx, ty, 0)
        time.sleep(0.3)
        # 再点一次：SetForegroundWindow 偶尔失败（窗口在后台），第一下点击会被
        # 前台切换吃掉，终端拿不到焦点 —— 现象是"一个键都没到"，看起来像连发没修好。
        mouse(WM_LBUTTONDOWN, tx, ty, 1)
        time.sleep(0.08)
        mouse(WM_LBUTTONUP, tx, ty, 0)
        time.sleep(0.3)

        # cat -v：把远端收到的字节用 ^[ 形式回显，便于数次数。
        # 必须先进非规范模式（-icanon）：默认 canonical 下 0x7f 是 tty 的 ERASE 字符，
        # 被行规程吃掉，cat 根本收不到 —— 那样测退格会永远"0 次"，是测试方法的问题。
        # -echo 也必须加：只关 icanon 的话，行规程会把收到的字节**再回显一遍**，
        # cat -v 又打一遍 —— 数出来恒等于真实次数的 2 倍（单点也"翻倍"，很容易
        # 被误读成"连发发了两倍"）。
        prep = "keys\r" if mode == "keys" else "stty -icanon -echo; cat -v\r"
        for ch in prep:
            if ch == "\r":
                vr.user32.PostMessageW(hwnd, WM_KEYDOWN, 0x0D, 1)
                vr.user32.PostMessageW(hwnd, WM_CHAR, 0x0D, 1)
                vr.user32.PostMessageW(hwnd, WM_KEYUP, 0x0D, 1)
            else:
                vr.user32.PostMessageW(hwnd, WM_CHAR, ord(ch), 1)
            time.sleep(0.03)
        time.sleep(1.0)

        # 关键动作：1 次真实按下（lParam bit30=0），随后 n 次自动重复（bit30=1）
        vr.user32.PostMessageW(hwnd, WM_KEYDOWN, vk, 1)
        time.sleep(0.05)
        for _ in range(n):
            vr.user32.PostMessageW(hwnd, WM_KEYDOWN, vk, 1 | (1 << 30))
            time.sleep(0.05)
        vr.user32.PostMessageW(hwnd, WM_KEYUP, vk, 0xC0000001)
        time.sleep(0.8)

        txt = open(STATE, encoding="utf-8", errors="replace").read() if os.path.exists(STATE) else ""
        # 终端快照里每行形如 `[12] 内容`
        body = "\n".join(l for l in txt.splitlines() if l.startswith("["))
        if mode == "keys":
            tok = {"BACKSPACE": "BS", "DELETE": "DEL", "ENTER": "ENTER",
                   "TAB": "TAB", "ESC": "ESC"}.get(key, key)
            seq = "K:" + tok
        else:
            seq = {"LEFT": "^[[D", "RIGHT": "^[[C", "UP": "^[[A", "DOWN": "^[[B",
                   "BACKSPACE": "^?", "DELETE": "^[[3~", "ENTER": "^M"}[key]
        got = body.count(seq)
        print("按键 %s 连发：期望 %d 次，实际 %d 次" % (key, n + 1, got))
        print("终端快照：")
        print(body[-600:])
        return 0 if got >= n + 1 else 1
    finally:
        try:
            proc.terminate()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
