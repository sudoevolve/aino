"""实际画廊的 CPU 与绘制耗时采样；不编译、不运行功能回归。"""

import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time
from ctypes import wintypes

import psutil
import verify_gallery as vg
import verify_render as vr

ROOT = Path(__file__).resolve().parent.parent


def benchmark(duration=4.0, output=None, only=None):
    rows = []
    saved = wintypes.POINT()
    vr.user32.GetCursorPos(ctypes.byref(saved))
    cases = {
        "buttons": ("idle", "same_control", "navigation", "scroll"),
        "sliders": ("drag",),
        "progress": ("idle", "hover"),
    }
    if only:
        unknown = set(only) - {page + ":" + mode for page, modes in cases.items() for mode in modes}
        if unknown:
            raise ValueError("未知工况: " + ", ".join(sorted(unknown)))
    try:
        for page, modes in cases.items():
            modes = [mode for mode in modes if not only or page + ":" + mode in only]
            if not modes:
                continue
            log = ROOT / "build" / ("perf-gallery-" + page + ".log")
            env = dict(os.environ, AINO_START_PAGE=page, AINO_DARK="0",
                       AINO_PROF=str(log))
            env.pop("AINO_FRAME_LOG", None)
            proc = subprocess.Popen([vg.EXE], env=env, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL)
            try:
                hwnd = vr.find_window_by_pid(proc.pid)
                if not hwnd:
                    raise RuntimeError("画廊窗口未出现")
                vr.user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND,
                    ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
                vr.user32.SetWindowPos(hwnd, wintypes.HWND(-1), 0, 0, 0, 0, 0x51)
                vr.user32.SetForegroundWindow(hwnd)
                rect = wintypes.RECT()
                origin = wintypes.POINT()
                vr.user32.GetClientRect(hwnd, ctypes.byref(rect))
                vr.user32.ClientToScreen(hwnd, ctypes.byref(origin))
                scale = rect.right / vg.LOGICAL_W

                def move(x, y):
                    px, py = round(x * scale), round(y * scale)
                    vr.user32.SetCursorPos(origin.x + px, origin.y + py)

                def cells():
                    raw, w, h, ox, oy, cw, _, _ = vr.grab_raw(hwnd)
                    return vg.content_cells(raw, w, h, ox, oy, cw / vg.LOGICAL_W)

                vr.user32.SetCursorPos(1, 1)
                time.sleep(3.0)
                ps = psutil.Process(proc.pid)
                for mode in modes:
                    vr.user32.SetWindowPos(hwnd, wintypes.HWND(-1), 0, 0, 0, 0, 0x53)
                    time.sleep(.1)
                    vr.user32.GetClientRect(hwnd, ctypes.byref(rect))
                    origin = wintypes.POINT()
                    vr.user32.ClientToScreen(hwnd, ctypes.byref(origin))
                    scale = rect.right / vg.LOGICAL_W
                    x, y = ((110, 180) if mode in ("same_control", "navigation")
                            else (450, 200) if mode == "drag"
                            else (450, 257) if mode == "hover" else (1100, 100))
                    move(x, y)
                    # 部分桌面环境不转发静止光标的首次移动，投递一次初始坐标。
                    vr.user32.PostMessageW(hwnd, 0x0200, 0,
                                           (round(y * scale) << 16) | round(x * scale))
                    time.sleep(.8)
                    point = wintypes.POINT()
                    vr.user32.GetCursorPos(ctypes.byref(point))
                    vr.user32.WindowFromPoint.argtypes = [wintypes.POINT]
                    vr.user32.WindowFromPoint.restype = wintypes.HWND
                    pointed = vr.user32.WindowFromPoint(point)
                    if pointed != hwnd:
                        title = ctypes.create_unicode_buffer(256)
                        vr.user32.GetWindowTextW(pointed, title, len(title))
                        reason = "光标被其它窗口阻挡: " + title.value
                        rows.append({"page": page, "mode": mode, "ok": False, "error": reason})
                        print(f"{page:9} {mode:13} 未测得：{reason}", flush=True)
                        continue
                    before = cells()
                    time.sleep(.5)
                    log_start = log.stat().st_size if log.exists() else 0
                    if mode == "drag":
                        vr.user32.PostMessageW(hwnd, 0x0201, 1,
                                               (round(y * scale) << 16) | round(x * scale))
                    c0 = ps.cpu_times()
                    start = time.perf_counter()
                    n = 0
                    while time.perf_counter() - start < duration:
                        phase = (n % 120) / 120.0
                        if mode == "same_control":
                            move(110 + 2 * math.sin(n), 180)
                        elif mode == "navigation":
                            move(110, 116 + 120 * (1 - abs(phase * 2 - 1)))
                        elif mode == "drag":
                            move(320 + 330 * (1 - abs(phase * 2 - 1)), y)
                        elif mode == "hover" and n % 12 == 0:
                            move(x, y)
                            vr.user32.PostMessageW(hwnd, 0x0200, 0,
                                (round(y * scale) << 16) | round(x * scale))
                        elif mode == "scroll" and n % 3 == 0:
                            move(1100, 400)
                            delta = -120 if (n // 40) % 2 == 0 else 120
                            vr.user32.PostMessageW(hwnd, 0x020A, (delta & 0xFFFF) << 16,
                                ((origin.y + round(400 * scale)) << 16)
                                | ((origin.x + round(1100 * scale)) & 0xFFFF))
                        time.sleep(.004)
                        n += 1
                    elapsed = time.perf_counter() - start
                    c1 = ps.cpu_times()
                    if mode == "drag":
                        vr.user32.PostMessageW(hwnd, 0x0202, 0, 0)
                    cpu = ((c1.user + c1.system) - (c0.user + c0.system)) / elapsed * 100
                    time.sleep(.1)
                    moved = vg.diff_cells(before, cells())
                    text = log.read_bytes()[log_start:].decode("utf-8", errors="replace") if log.exists() else ""
                    frame = re.findall(r"渲染均 ([\d.]+) ms", text)
                    draw = re.findall(r"绘制 ([\d.]+) \|", text)
                    row = {"page": page, "mode": mode, "cpu_one_core_pct": round(cpu, 2),
                           "cpu_machine_pct": round(cpu / psutil.cpu_count(), 2),
                           "duration_s": round(elapsed, 3), "scale": scale,
                           "client_px": [rect.right, rect.bottom], "changed_cells": moved,
                           "frame_ms": round(sum(map(float, frame)) / len(frame), 3) if frame else None,
                           "draw_ms": round(sum(map(float, draw)) / len(draw), 3) if draw else None}
                    row["ok"] = ((cpu <= (1 if mode == "idle" else 5) and moved == 0)
                                 if mode in ("idle", "same_control") else bool(frame))
                    rows.append(row)
                    print(f'{page:9} {mode:13} 单核 {cpu:6.2f}%  整机 {row["cpu_machine_pct"]:5.2f}%'
                          f'  帧 {row["frame_ms"]} ms  绘制 {row["draw_ms"]} ms'
                          f'  {"OK" if row["ok"] else "CHECK"}', flush=True)
            finally:
                proc.terminate()
                proc.wait(timeout=5)
    finally:
        vr.user32.SetCursorPos(saved.x, saved.y)
    result = {"logical_size": [1180, 760], "logical_processors": psutil.cpu_count(), "rows": rows}
    path = Path(output) if output else ROOT / "build" / "perf-gallery.json"
    path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("数据:", path)
    return all(r["ok"] for r in rows)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration", type=float, default=4.0)
    parser.add_argument("--out")
    parser.add_argument("--only", help="逗号分隔的 page:mode，例如 buttons:navigation,sliders:drag")
    args = parser.parse_args()
    if args.duration < 2:
        parser.error("采样时长至少 2 秒")
    raise SystemExit(0 if benchmark(args.duration, args.out,
                                   args.only.split(",") if args.only else None) else 1)
