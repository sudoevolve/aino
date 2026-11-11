"""组件级性能台架驱动 —— 逐个组件单进程测三种工况。

为什么要单进程单组件：
  同一窗口里若同时存在 spinner / skeleton 这类"自请求重绘"的组件，
  量到的 CPU 是混在一起的，无法归因到具体组件。所以每个组件单独启动一个
  进程、只画它一个，彼此完全不干扰。

三种工况：
  idle   静置：事件驱动，不碰鼠标。CPU 必须是 0.00%，帧数必须是 0。
  draw   显式请求动画帧，测量单帧组件绘制成本（QPC 耗时）。
  hover  交互：持续投递 WM_MOUSEMOVE（10ms 一次）+ 周期性点击，
         量真实交互下的 CPU 与帧率。

统计口径（两边严格对齐）：
  台架从 elapsed >= warmup 开始累计 draw_us 与帧数，累计满 dur 自动退出；
  脚本在"启动后 warmup+0.1 秒"起用 psutil 采 dur 秒进程 CPU。
  预热期不统计，避开首帧建字体缓存 / 分配后备缓冲的一次性成本。

用法：
  python perf_bench.py                 # 全量
  python perf_bench.py --only button,table
  python perf_bench.py --quick         # 缩短时长，冒烟用
"""

import argparse
import ctypes
import json
import os
import re
import subprocess
import sys
import time
from ctypes import wintypes

import psutil

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PROBE = os.path.join(HERE, "probe")
EXE = os.path.join(PROBE, "perf_comp.exe")
TMPDIR = os.path.join(HERE, "_perf_logs")
RESULT = os.path.join(HERE, "perf_result.json")

user32 = ctypes.WinDLL("user32", use_last_error=True)

user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT,
                                wintypes.WPARAM, wintypes.LPARAM]
user32.PostMessageW.restype = wintypes.BOOL
user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int,
                                ctypes.c_int, ctypes.c_int, wintypes.UINT]
user32.SetWindowPos.restype = wintypes.BOOL
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.GetDpiForWindow.argtypes = [wintypes.HWND]
user32.GetDpiForWindow.restype = wintypes.UINT

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

HWND_TOPMOST = wintypes.HWND(-1)
SWP_NOSIZE, SWP_NOMOVE, SWP_SHOWWINDOW = 0x0001, 0x0002, 0x0040

WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_CLOSE = 0x0200, 0x0201, 0x0202, 0x0010

LOGICAL_W, LOGICAL_H = 460, 300

# 与 perf_comp.cpp 的 kNames 一一对应（顺序无关，但必须齐全）
COMPONENTS = [
    "empty",
    "label", "heading", "subheading", "muted_text", "code_text", "text_wrap",
    "panel", "card", "separator", "badge", "progress", "skeleton", "skeleton_anim",
    "button", "button_full", "icon_button", "nav_item", "input", "checkbox",
    "toggle", "slider", "tabs", "segmented", "select", "radio_group", "stepper",
    "textarea", "rating",
    "avatar", "spinner", "spinner_anim", "alert", "breadcrumb", "kbd", "accordion", "table",
    "sparkline", "bars",
    "tooltip", "menu", "dialog", "toast",
    "prim_rrect", "prim_circle", "prim_line", "prim_shadow", "prim_text",
    # 阴影尺寸阶梯：只用来拟合 SDF 阴影的 ns/像素，单独跑，不进主表
    "shadow_s", "shadow_m", "shadow_l",
]

# 需要"先激活"才能画出来的浮层：menu 要先点开，tooltip 要指针停在 anchor 上
NEEDS_ACTIVATE = {"menu", "tooltip"}


def find_window(pid, timeout=8.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        found = []

        def cb(hwnd, _):
            wpid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
            if wpid.value == pid and user32.IsWindowVisible(hwnd):
                r = wintypes.RECT()
                user32.GetWindowRect(hwnd, ctypes.byref(r))
                if r.right - r.left > 200:
                    found.append(hwnd)
            return True

        user32.EnumWindows(WNDENUMPROC(cb), 0)
        if found:
            return found[0]
        time.sleep(0.05)
    return None


def dpi_scale(hwnd):
    """逻辑→物理倍率。以客户区宽度反推，再用 GetDpiForWindow 交叉验证。"""
    cr = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(cr))
    by_client = cr.right / float(LOGICAL_W)
    by_dpi = user32.GetDpiForWindow(hwnd) / 96.0
    # 以客户区为准：库自己也是按客户区尺寸算的，GetDpiForWindow 在非
    # per-monitor-aware 进程上会给出系统 DPI，可能与实际缩放不一致
    scale = round(by_client * 4.0) / 4.0
    if abs(scale - by_dpi) > 0.26:
        scale = by_dpi
    return max(scale, 1.0)


def make_lparam(hwnd, lx, ly, scale):
    x = int(round(lx * scale))
    y = int(round(ly * scale))
    return wintypes.LPARAM(((y & 0xFFFF) << 16) | (x & 0xFFFF))


def post_move(hwnd, lx, ly, scale):
    user32.PostMessageW(hwnd, WM_MOUSEMOVE, wintypes.WPARAM(0),
                        make_lparam(hwnd, lx, ly, scale))


def post_click(hwnd, lx, ly, scale):
    lp = make_lparam(hwnd, lx, ly, scale)
    user32.PostMessageW(hwnd, WM_MOUSEMOVE, wintypes.WPARAM(0), lp)
    time.sleep(0.02)
    user32.PostMessageW(hwnd, WM_LBUTTONDOWN, wintypes.WPARAM(1), lp)
    time.sleep(0.02)
    user32.PostMessageW(hwnd, WM_LBUTTONUP, wintypes.WPARAM(0), lp)


def keep_visible(hwnd):
    """置顶但不抢焦点。窗口被别的窗口挡住时 BitBlt 的可视区为空，
    窗口 DC 内容不实际光栅化，CPU 读数会凭空掉到 0 —— 必须保证它真的在屏幕上。"""
    user32.SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW)


def parse_log(path):
    """从 stderr 日志里取台架自报的最终统计。"""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            txt = f.read()
    except OSError:
        return None, "", None
    m = re.search(r"\[perf\] final comp=(\S+) frames=(\d+) "
                  r"comp_avg_us=([\d.]+) comp_max_us=([\d.]+)", txt)
    stat = None
    if m:
        stat = {
            "frames": int(m.group(2)),
            "comp_avg_us": float(m.group(3)),
            "comp_max_us": float(m.group(4)),
        }
    # 库自带的分段计时：AINO_FRAME_LOG=1 时每 120 帧打一次，
    # 精度是 QPC（µs 级），比 psutil 的 15.6ms 节拍粒度靠谱得多。
    seg = None
    m2 = re.search(r"\[aino\]\s*\d+ 帧\s+渲染均 ([\d.]+) ms\s+最差 ([\d.]+) ms", txt)
    m3 = re.search(r"取输入 ([\d.]+) \| 绘制 ([\d.]+) \| 收尾 ([\d.]+) \| 上屏 ([\d.]+)", txt)
    if m2 and m3:
        seg = {
            "frame_avg_ms": float(m2.group(1)),
            "frame_max_ms": float(m2.group(2)),
            "t_feed_ms": float(m3.group(1)),
            "t_draw_ms": float(m3.group(2)),
            "t_end_ms": float(m3.group(3)),
            "t_present_ms": float(m3.group(4)),
        }
    return stat, txt, seg


def interact(hwnd, scale, dur):
    """持续投递鼠标移动 + 周期点击，模拟真实交互负载。

    10ms 一次（≈100 次/秒）：库对输入帧不做节流，投递多快就渲染多快，
    所以这个节奏决定的就是"交互时的帧率上限"。
    """
    deadline = time.perf_counter() + dur
    n = 0
    while time.perf_counter() < deadline:
        x = 60 + (n % 3) * 110
        y = 100 + (n % 2) * 40
        post_move(hwnd, x, y, scale)
        if n % 30 == 0:
            post_click(hwnd, x, y, scale)
        time.sleep(0.010)
        n += 1
    return n


def run_case(comp, anim, warmup, dur, do_interact=False, activate=False):
    os.makedirs(TMPDIR, exist_ok=True)
    logpath = os.path.join(TMPDIR, "%s_%s.log" % (comp, "draw" if anim else
                                                  ("hover" if do_interact else "idle")))
    lf = open(logpath, "w", encoding="utf-8")
    args = [EXE, "--comp=" + comp, "--warmup=%.2f" % warmup, "--dur=%.2f" % dur]
    if anim:
        args.append("--anim")

    env = dict(os.environ)
    if anim:
        # 让库把「取输入 | 绘制 | 收尾 | 上屏」四段 QPC 耗时打到 stderr。
        # 这是唯一能精确拆开一帧成本的办法 —— psutil 的进程 CPU 只有 15.6ms 粒度。
        env["AINO_FRAME_LOG"] = "1"

    t_launch = time.perf_counter()
    proc = subprocess.Popen(args, stderr=lf, stdout=subprocess.DEVNULL, env=env,
                            creationflags=0x08000000)   # CREATE_NO_WINDOW
    try:
        hwnd = find_window(proc.pid)
        if not hwnd:
            return {"component": comp, "error": "窗口未出现"}

        keep_visible(hwnd)
        scale = dpi_scale(hwnd)
        time.sleep(0.15)

        if activate:
            # 预热期内激活：菜单点开、指针停进 tooltip 的 anchor
            post_click(hwnd, 150, 128, scale)
            post_move(hwnd, 230, 157, scale)

        # 等到台架的统计窗口开始（台架从自己 main 里计时，比这里早约 50ms，
        # 所以多等 100ms，落在窗口内侧）
        wait = warmup + 0.10 - (time.perf_counter() - t_launch)
        if wait > 0:
            time.sleep(wait)

        try:
            ps = psutil.Process(proc.pid)
            c0 = ps.cpu_times()
            w0 = time.perf_counter()
            if do_interact:
                posts = interact(hwnd, scale, dur)
            else:
                posts = 0
                time.sleep(dur)
            c1 = ps.cpu_times()
            w1 = time.perf_counter()
            cpu_sec = (c1.user - c0.user) + (c1.system - c0.system)
            wall = w1 - w0
        except psutil.Error:
            return {"component": comp, "error": "进程提前退出"}

        # 台架到点会自己 close()；事件驱动工况（idle）没有帧回调，得我们送 WM_CLOSE
        user32.PostMessageW(hwnd, WM_CLOSE, wintypes.WPARAM(0), wintypes.LPARAM(0))
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.terminate()
            proc.wait(timeout=2)
    finally:
        lf.close()
        if proc.poll() is None:
            proc.kill()

    stat, raw, seg = parse_log(logpath)
    rec = {
        "component": comp,
        "cpu_pct": round(cpu_sec / wall * 100.0, 2) if wall > 0 else 0.0,
        "cpu_sec": round(cpu_sec, 4),
        "wall_s": round(wall, 3),
        "scale": scale,
        "frames": stat["frames"] if stat else 0,
        "comp_avg_us": stat["comp_avg_us"] if stat else 0.0,
        "comp_max_us": stat["comp_max_us"] if stat else 0.0,
        "posts": posts,
        "seg": seg,
    }
    rec["fps"] = round(rec["frames"] / wall, 1) if wall > 0 else 0.0
    # 每帧 CPU 比百分比更有意义：交互工况的帧数取决于脚本投递消息的节奏，
    # 不归一化就没法横向比（投递快 → 帧多 → CPU 高，跟组件本身无关）。
    rec["cpu_per_frame_us"] = (round(cpu_sec * 1e6 / rec["frames"], 1)
                               if rec["frames"] > 0 else 0.0)
    rec["log_ok"] = stat is not None
    return rec


def fmt_row(vals, widths):
    return "  ".join(str(v).ljust(w) if i == 0 else str(v).rjust(w)
                     for i, (v, w) in enumerate(zip(vals, widths)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", help="逗号分隔的组件名")
    ap.add_argument("--quick", action="store_true", help="缩短时长（冒烟）")
    ap.add_argument("--warmup", type=float, default=2.0)
    ap.add_argument("--idle-dur", type=float, default=3.0)
    ap.add_argument("--draw-dur", type=float, default=4.0)
    ap.add_argument("--hover-dur", type=float, default=6.0)
    ap.add_argument("--out", help="结果 JSON 路径（默认 tools/perf_result.json）")
    a = ap.parse_args()

    comps = COMPONENTS
    if a.only:
        want = [s.strip() for s in a.only.split(",") if s.strip()]
        bad = [w for w in want if w not in COMPONENTS]
        if bad:
            raise SystemExit("未知组件: %s\n可选: %s" % (", ".join(bad), ", ".join(COMPONENTS)))
        comps = want

    if a.quick:
        warmup, idle_dur, draw_dur, hover_dur = 1.0, 1.2, 1.5, 1.5
    else:
        warmup = a.warmup
        idle_dur, draw_dur, hover_dur = a.idle_dur, a.draw_dur, a.hover_dur

    print("台架: %s" % EXE)
    print("预热 %.1fs | 静置 %.1fs | 连续重绘 %.1fs | 交互 %.1fs | %d 个组件\n"
          % (warmup, idle_dur, draw_dur, hover_dur, len(comps)), flush=True)
    print("%-14s %8s %6s %9s %8s %8s %9s" %
          ("组件", "静置CPU%", "帧", "绘制µs", "收尾µs", "上屏µs", "交互CPU%"), flush=True)
    print("-" * 72, flush=True)

    rows = []
    for comp in comps:
        t0 = time.time()
        idle = run_case(comp, anim=False, warmup=warmup, dur=idle_dur)
        draw = run_case(comp, anim=True, warmup=warmup, dur=draw_dur,
                        activate=(comp in NEEDS_ACTIVATE))
        hover = run_case(comp, anim=False, warmup=warmup, dur=hover_dur,
                         do_interact=True, activate=(comp in NEEDS_ACTIVATE))
        rec = {"component": comp, "idle": idle, "draw": draw, "hover": hover}
        rows.append(rec)

        err = [k for k in ("idle", "draw", "hover") if "error" in rec[k]]
        if err:
            print("%-14s !! %s" % (comp, "; ".join(rec[k]["error"] for k in err)), flush=True)
        else:
            seg = draw.get("seg") or {}
            print("%-14s %8.2f %6d %9.1f %8s %8s %9.2f  (%.0fs)" %
                  (comp, idle["cpu_pct"], idle["frames"], draw["comp_avg_us"],
                   ("%.0f" % (seg["t_end_ms"] * 1000)) if seg else "-",
                   ("%.0f" % (seg["t_present_ms"] * 1000)) if seg else "-",
                   hover["cpu_pct"], time.time() - t0), flush=True)

    with open(a.out or RESULT, "w", encoding="utf-8") as f:
        json.dump({
            "warmup_s": warmup,
            "idle_dur_s": idle_dur, "draw_dur_s": draw_dur, "hover_dur_s": hover_dur,
            "logical_w": LOGICAL_W, "logical_h": LOGICAL_H,
            "rows": rows,
        }, f, ensure_ascii=False, indent=1)
    print("\n完整数据 -> %s" % (a.out or RESULT))

    ok = [r for r in rows if all("error" not in r[k] for k in ("idle", "draw", "hover"))]
    if ok:
        print("\n== 按组件自身绘制耗时排序（贵的在前）==")
        print("  %-14s %9s %9s %9s %9s %8s" %
              ("组件", "组件µs", "帧均µs", "上屏µs", "交互µs/帧", "交互fps"))
        for r in sorted(ok, key=lambda x: -x["draw"]["comp_avg_us"])[:20]:
            d, h = r["draw"], r["hover"]
            seg = d.get("seg") or {}
            print("  %-14s %9.1f %9s %9s %9.1f %8.1f" %
                  (r["component"], d["comp_avg_us"],
                   ("%.0f" % (seg["frame_avg_ms"] * 1000)) if seg else "-",
                   ("%.0f" % (seg["t_present_ms"] * 1000)) if seg else "-",
                   h["cpu_per_frame_us"], h["fps"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
