"""跑 aino_ssh.exe 若干秒，抓 AINO_SSH_DEBUG_FILE 快照，再强杀进程。

用途：GUI 没法用眼睛断言的地方（连接状态、终端内容、SFTP 报错、标签数）在这里做自动化核对。
在 Git Bash 里 `taskkill //F` 有时会被 MSYS 当成路径、bash 的 kill 又杀不掉 GUI 进程，
所以统一走 ctypes TerminateProcess。

用法：
    python tools/probe/run_gui.py --secs 8 --env AINO_SSH_HOST=127.0.0.1,AINO_SSH_PORT=2223
    python tools/probe/run_gui.py --secs 8 --shot build/shot.png --click 300,400
"""
import argparse
import ctypes
import json
import os
import subprocess
import sys
import time

EXE = os.path.join(os.path.dirname(__file__), "..", "..", "build", "Release", "aino_ssh.exe")
STATE = os.path.join(os.path.dirname(__file__), "..", "..", "build", "gui_state.txt")


def kill_all(name="aino_ssh.exe"):
    # Windows 中文版 tasklist 输出是 GBK，utf-8 解码会炸
    out = subprocess.run(
        ["tasklist", "/FI", f"IMAGENAME eq {name}", "/FO", "CSV", "/NH"],
        capture_output=True, text=True, encoding="mbcs", errors="replace").stdout
    k = ctypes.windll.kernel32
    n = 0
    for line in out.strip().splitlines():
        parts = [p.strip('"') for p in line.split(",")]
        if len(parts) < 2 or name.lower() not in parts[0].lower():
            continue
        try:
            pid = int(parts[1])
        except ValueError:
            continue
        # PROCESS_TERMINATE = 0x0001
        h = k.OpenProcess(0x0001, False, pid)
        if h:
            k.TerminateProcess(h, 1)
            k.CloseHandle(h)
            n += 1
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--secs", type=float, default=8.0)
    ap.add_argument("--env", default="", help="逗号分隔的 K=V")
    ap.add_argument("--state", default=STATE)
    ap.add_argument("--shot", default="", help="截图落盘路径（调用 shot_ssh.py）")
    args = ap.parse_args()

    kill_all()
    time.sleep(0.4)

    env = dict(os.environ)
    env["AINO_SSH_DEBUG_FILE"] = os.path.abspath(args.state)
    for kv in filter(None, args.env.split(",")):
        k, _, v = kv.partition("=")
        env[k.strip()] = v
    if os.path.exists(args.state):
        os.remove(args.state)

    p = subprocess.Popen([os.path.abspath(EXE)], env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(args.secs)

    alive = p.poll() is None
    state = ""
    if os.path.exists(args.state):
        with open(args.state, "r", encoding="utf-8", errors="replace") as f:
            state = f.read()

    if args.shot:
        subprocess.run([sys.executable,
                        os.path.join(os.path.dirname(__file__), "..", "shot_ssh.py"),
                        args.shot], capture_output=True)

    n = kill_all()
    print(f"alive={alive} killed={n}")
    print("=== state ===")
    print(state.rstrip())


if __name__ == "__main__":
    main()
