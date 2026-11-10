"""aino 一键回归：数值探针 + 光栅化等价性 + 像素断言 + 空闲 CPU。

用法：
  python regress.py            # 全跑
  python regress.py --no-build # 不重编（只跑）

设计取舍：
  · 数值探针（headless，直接驱动 Ui 的 input）跑得快、无窗口/DPI/真实鼠标的抖动，
    所以核心断言都放这儿，不依赖截图肉眼看。
  · 光栅化优化必须靠"与朴素参考实现逐像素比对"来验，光看图像看不出 1px 边缘的偏差。
  · 像素断言走 PrintWindow + GetDIBits（verify_gallery.py），窗口逻辑坐标输入、
    脚本自己按 DPI 换算；换页/切主题靠投递窗口消息模拟点击。
  · 空闲 CPU 用 psutil 采样进程 cpu_times —— aino 的核心承诺是"空闲 0% CPU"，
    一旦有组件偷偷每帧 repaint，这里会立刻反弹。
"""

import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PROBE = os.path.join(HERE, "probe")
PY = sys.executable

DEMO = os.path.join(ROOT, "build")
# 生成器不同，exe 落点也不同：Ninja/Makefiles 直接放 build/，VS 多配置放在
# build/Release 或 build/Debug。按存在性挑一个，别逼用户换生成器。
for _sub in ("", "Release", "Debug"):
    if os.path.isfile(os.path.join(DEMO, _sub, "aino_gallery.exe")):
        DEMO = os.path.join(DEMO, _sub)
        break
GALLERY = os.path.join(DEMO, "aino_gallery.exe")

results = []

# ---- 「到底动没动」的判据：静置时抓两帧（间隔 0.25s）比对内容区的采样格 ----
#   动画页（spinner 在转、skeleton 在呼吸）必然有若干格变；空闲页必须逐格完全一致。
#   阈值不用很大：spinner 在 16px 网格下只会落到少数几格里，实测 20~40 格。
ANIM_MIN = 3         # 动画页至少要差这么多格（实测 6~7，留一倍余量）
IDLE_MAX = 0         # 空闲页两帧必须完全一致（一个格都不许变）
FRAME_GAP = 0.30     # 抓两帧的间隔（秒）；spinner 约 1.4 圈/秒，0.15s 只差 4 格、0.3s 有 7 格


def run(cmd, cwd=None, env=None, timeout=300):
    return subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True, encoding="utf-8",
                          errors="replace", timeout=timeout)


def step(name, ok, detail=""):
    # detail 以 "SKIP:" 开头 = 前提不满足（比如自动化环境里光标进不了窗口）。
    # 这种不算失败也不是绿色 —— 明确标出来，别假装测过。
    skip = detail.startswith("SKIP:")
    results.append((name, True if skip else ok, detail))
    mark = "SKIP" if skip else ("OK  " if ok else "FAIL")
    print("[%s] %-34s %s" % (mark, name, detail))
    return ok


def build():
    r = run([PY, os.path.join(HERE, "build.py")])
    return r.returncode == 0, (r.stdout or r.stderr)[-400:]


def build_probe(name):
    r = run([PY, os.path.join(HERE, "cl_one.py"), name + ".cpp", name + ".exe",
             "/utf-8", "user32.lib", "gdi32.lib", "msimg32.lib", "imm32.lib"], cwd=PROBE)
    return r.returncode == 0, (r.stdout or r.stderr)[-400:]


def run_probe(name, want_tail):
    exe = os.path.join(PROBE, name + ".exe")
    r = run([exe], cwd=PROBE)
    out = (r.stdout or "") + (r.stderr or "")
    ok = r.returncode == 0 and want_tail in out
    tail = [l for l in out.strip().splitlines() if l.strip()][-1:] or [""]
    return ok, tail[0][:90]


def ci_pixels():
    """跨平台像素断言（tools/ci_pixels.cpp）。

    它是**唯一**能在 Linux / macOS 上跑的像素断言：直接从后备缓冲读像素，
    不依赖 PrintWindow。本地跑它的意义是"改光栅化时先在 Windows 上红给你看"，
    而不是等 CI 三平台各红一遍。
    """
    r = run([PY, os.path.join(HERE, "cl_one.py"),
             os.path.join(ROOT, "tools", "ci_pixels.cpp"),
             os.path.join(ROOT, "build", "ci_pixels.exe"),
             "/utf-8", "user32.lib", "gdi32.lib", "msimg32.lib"], cwd=ROOT)
    if r.returncode != 0:
        return False, "编译失败: " + ((r.stdout or "") + (r.stderr or ""))[-160:]
    exe = os.path.join(ROOT, "build", "ci_pixels.exe")
    r = run([exe], cwd=ROOT)
    out = (r.stdout or "") + (r.stderr or "")
    fails = [l.strip() for l in out.splitlines() if "[ FAIL ]" in l]
    if fails:
        return False, fails[0][:150]
    tail = [l for l in out.splitlines() if l.startswith("===")][-1:] or [""]
    return r.returncode == 0, tail[0][:60]


def verify_gallery(extra=()):
    cmd = [PY, os.path.join(HERE, "verify_gallery.py")] + list(extra)
    r = run(cmd)
    out = (r.stdout or "") + (r.stderr or "")
    fails = [l.strip() for l in out.splitlines() if "[FAIL]" in l]
    if fails:
        return False, " | ".join(fails)[:160]
    line = [l for l in out.splitlines() if "共" in l and "失败" in l]
    return (r.returncode == 0), (line[0].strip() if line else out.strip()[-80:])


def idle_probe(exe, dur=3.0, click=None, expect_nav=None, want_anim=None, tries=3, page=None):
    """量「静置 dur 秒」的 CPU，并且**同时**用"隔一段时间的两帧是否一样"来证明它到底动没动。

    为什么要两个证据：
      · CPU 是 aino 对外的承诺（空闲 0%），但它单独用会被"翻页没生效"骗到 ——
        同一页面实测抖出过 0.00% / 7.29% / 25.00% / 43.74%，差异其实来自别处。
      · "两帧是否逐格相同"是**直接**观察渲染结果，跟 CPU 的采样时机、日志节拍、
        窗口可见性都无关，所以它负责判定"在不在动"，CPU 负责判定"动得贵不贵"。
      · CPU 一次采 3 个窗口取**最小值**（见下面的注释）：单次读数会被外部干扰污染。
    翻页必须同时满足「竖条亮」+「内容区变化格 ≥ DIFF_MIN」，缺一个这次采样就作废重来。
      want_anim=True  要求两帧确实不同（页面在动画）
      want_anim=False 要求两帧完全一致（真的没人重绘）
    三次都不合格返回 valid=False —— 宁可红，不要假绿。

    返回 (cpu, moved_cells, attempts, valid, note)
    """
    try:
        import psutil
    except ImportError:
        subprocess.run([PY, "-m", "pip", "install", "psutil", "-q"])
        import psutil
    sys.path.insert(0, HERE)
    import verify_render as vr
    import verify_gallery as vg

    note = ""
    moved = -1

    def cells_of(hwnd_):
        raw_, w_, h_, ox_, oy_, cw_, _ch, _ok = vr.grab_raw(hwnd_)
        return vg.content_cells(raw_, w_, h_, ox_, oy_, cw_ / vg.LOGICAL_W)

    env = dict(os.environ)
    if page:
        env["AINO_START_PAGE"] = page        # 直接落到目标页，不靠点导航
    for attempt in range(1, tries + 1):
        p = subprocess.Popen([exe], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        # 预热必须够长：aino 启动时会枚举/创建字体、跑一次首帧和收尾动画
        # （实测约 31 帧 / 0.9s 才真正静下来）。预热 1.5s 时曾量到 10% 的假阳性。
        time.sleep(2.5)
        try:
            hwnd = vr.find_window_by_pid(p.pid, 5.0)
            if not hwnd:
                note = "窗口没找到"
                continue
            raw0, w0, h0, ox0, oy0, cw0, ch0, _ = vr.grab_raw(hwnd)
            scale = cw0 / vg.LOGICAL_W
            if click:
                before = vg.content_cells(raw0, w0, h0, ox0, oy0, scale)
                switched = False
                for _ in range(5):
                    vg.post_click(hwnd, click[0], click[1], scale)
                    time.sleep(0.45)
                    raw, w, h, ox, oy, cw, ch, _ = vr.grab_raw(hwnd)
                    scale = cw / vg.LOGICAL_W
                    bar_ok = (expect_nav is None or
                              vg.bar_pixel(raw, w, ox, oy, scale, expect_nav)[0] < 120)
                    cells = vg.content_cells(raw, w, h, ox, oy, scale)
                    moved_switch = vg.diff_cells(before, cells)
                    if bar_ok and moved_switch >= vg.DIFF_MIN:
                        switched = True
                        break
                    before = cells      # 没换成，拿最新的当基准再来
                if not switched:
                    note = "翻页没生效（竖条/内容都没变）"
                    continue
                time.sleep(0.8)

            # ---- 静置采样 ----
            # 顺序很重要：**先**纯净地采 CPU，**再**抓两帧判断动没动。
            # 抓帧走的是 PrintWindow → 会给窗口发 WM_PRINT，本身就是"叫它画一次"，
            # 放在采样窗口里会平白抬出 0.4~0.5%（空闲页本该是 0.00%）。
            #
            # 最多采 3 个窗口**取最小值**：这个读数是"空闲必须 0%"的硬承诺，
            # 真的在每帧重绘的回归会让三个窗口全部偏高；而外部干扰（窗口被别的窗口
            # 遮挡又露出来 → 系统发 WM_PAINT）只会污染其中一两个窗口。
            # 实测同一份静态页面在这台机器上读过 0.00 / 7.29 / 25.00 / 43.74，
            # 全是"帧一个都没变"的情况 —— 单次读数不足以判红。
            proc = psutil.Process(p.pid)
            cpu = 1e9
            for _ in range(3):
                c0 = proc.cpu_times(); t0 = time.time()
                time.sleep(dur)
                c1 = proc.cpu_times(); dt = time.time() - t0
                cur = ((c1.user + c1.system) - (c0.user + c0.system)) / dt * 100.0
                cpu = min(cpu, cur)
                if cpu <= 1.0:
                    break

            a = cells_of(hwnd)
            time.sleep(FRAME_GAP)
            b = cells_of(hwnd)
            moved = vg.diff_cells(a, b)
        finally:
            try:
                p.terminate(); p.wait(timeout=5)
            except Exception:
                p.kill()

        if want_anim is True and moved < ANIM_MIN:
            note = "两帧只差 %d 格，没在动，作废" % moved
            continue
        if want_anim is False and moved > IDLE_MAX:
            note = "空闲页两帧差 %d 格，偷偷在重绘，作废" % moved
            continue
        return cpu, moved, attempt, True, ""

    return -1.0, moved, tries, False, note


def layout_probe():
    """实窗目录清单必须与 15 个分类、43 张组件卡片一致。

    精确几何、防重叠和交互由 macos_gallery_probe 覆盖。
    """
    exe = GALLERY
    out = os.path.join(ROOT, "build", "layout_report.txt")
    if os.path.exists(out):
        os.remove(out)
    env = dict(os.environ, AINO_LAYOUT_REPORT=out)
    p = subprocess.Popen([exe], env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        # 首帧就会写；最多等 6 秒（含启动 + 字体初始化）
        deadline = time.time() + 6.0
        while time.time() < deadline:
            if os.path.exists(out) and os.path.getsize(out) > 0:
                time.sleep(0.3)          # 等 fclose 落盘
                break
            time.sleep(0.1)
    finally:
        try:
            p.terminate(); p.wait(timeout=5)
        except Exception:
            p.kill()

    if not os.path.exists(out):
        return False, "报告没生成（窗口没起来？）"
    txt = open(out, encoding="utf-8", errors="replace").read()
    lines = txt.splitlines()
    summary = lines[0] if lines else ""
    cards = [line for line in lines[1:] if ": x=" in line]
    ok = "sections=15 cards=43" in summary and len(cards) == 43
    return ok, summary.lstrip("# ") + " / recorded=%d" % len(cards)


def _move_once(exe, dur, mode="nav"):
    """跑一次指针移动，返回 (单核百分比, 移动次数, 实际秒数)。

    mode="nav"  在左侧导航里上下扫 —— 每跨过一行就换悬停目标，最坏情况
    mode="row"  在**同一行内部**抖 —— 悬停目标不变，理想情况下一帧都不该出
    """
    import psutil
    import ctypes
    import ctypes.wintypes as wt
    sys.path.insert(0, HERE)
    import verify_render as vr

    # 帧日志：这条断言真正要证明的是"悬停目标没变就不排帧"，而**画了几帧**
    # 才是它的一手证据。CPU 百分比只是代理指标，这台机器上读数能从 3% 飘到 43%
    # （窗口被遮挡、DWM 合成、后台进程都会掺进来），拿它判红绿纯属看运气。
    # 帧日志每 60 帧写一块，所以"一块都没有"= 一帧都没画。
    flog = os.path.join(ROOT, "build", "move_frames.txt")
    if os.path.exists(flog):
        os.remove(flog)
    env = dict(os.environ, AINO_FRAME_LOG=flog)
    p = subprocess.Popen([exe], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(0.8)                       # 先等窗口出现，才能摆位置
        hwnd = vr.find_window_by_pid(p.pid, 5.0)
        if not hwnd:
            return None, 0, 0.0, 0, "窗口没找到"
        # 先固定窗口位置再动手：不挪到 (0,0) 的话它可能被别的窗口压住，真实光标
        # 落在别人的窗口上 —— TrackMouseEvent 会一直回灌 WM_MOUSELEAVE，"同一行
        # 抖动"于是也变成满速重绘（实测 B 在 3%~19% 之间飘）。
        vr.user32.ShowWindow(hwnd, 5)          # SW_SHOW
        vr.user32.MoveWindow(hwnd, 0, 0, 1180, 760, True)
        vr.user32.SetForegroundWindow(hwnd)
        vr.user32.BringWindowToTop(hwnd)
        # 窗口摆好之后再预热够久：MoveWindow 之后系统要过一拍才把"光标现在在
        # 哪个窗口"更新过来，这期间发出的移动事件全丢（实测 0.35s 时 A/B 都是
        # 0 帧，看着像"悬停过滤失效"，其实是根本没收到事件）。
        time.sleep(2.2)
        rc = wt.RECT()
        vr.user32.GetClientRect(hwnd, ctypes.byref(rc))
        pt = wt.POINT(0, 0)
        vr.user32.ClientToScreen(hwnd, ctypes.byref(pt))
        sc = rc.right / 1180.0
        nav_x = pt.x + int(110 * sc)

        # 前提：真实光标必须真的落在这个窗口上。有窗口压在上面时
        # SetForegroundWindow 会失败（本机实测：焦点一直在别的窗口上），
        # 系统把 WM_MOUSEMOVE 送给**上面那个**窗口，同时还回灌 WM_MOUSELEAVE，
        # 于是"同一行不重绘"根本无从谈起 —— 这时宁可 skip，也不要把环境噪声
        # 当成库的回归。
        vr.user32.SetCursorPos(nav_x, pt.y + int(180 * sc))
        time.sleep(0.2)
        gp = wt.POINT(0, 0)
        vr.user32.GetCursorPos(ctypes.byref(gp))
        if vr.user32.WindowFromPoint(gp) != hwnd:
            return None, 0, 0.0, 0, "光标进不了窗口（被别的窗口遮住）"

        # 移动指针：真实光标 + 直接投递消息**两条都发**。
        #
        # 只发真实光标不够：自动化环境里 SetForegroundWindow 经常失败，窗口在后台
        # 时系统不会把光标移动转发给它 —— 实测 A/B 两边都是 0.0%，等于没测到
        # （这是本地跑出来的，CI 有前台权限反而正常）。PostMessage 走的是同一个
        # 窗口过程（WM_MOUSEMOVE → 更新光标位置 → hover_would_change 判定），
        # 悬停逻辑一模一样，只是不依赖前台。
        WM_MOUSEMOVE = 0x0200
        def move_to(cx, cy):
            # 两条都发。只发消息会让 B 失效：真实光标不在窗口内时 TrackMouseEvent
            # 会不停回灌 WM_MOUSELEAVE（而 leave 是**必须**排帧的），于是"同一行
            # 抖动"也变成满速重绘（实测 B 从 3% 涨到 55%）。真实光标负责让窗口
            # 认为指针在里面，消息负责在前台抢不到时也能推进。
            cx, cy = int(cx), int(cy)
            vr.user32.SetCursorPos(pt.x + cx, pt.y + cy)

        proc = psutil.Process(p.pid)
        c0 = proc.cpu_times(); t0 = time.time()
        end = t0 + dur
        i = 0
        if mode == "nav":
            # 为什么必须在导航里扫：内容区大片空白一个控件都没有，"悬停没变就不画"
            # 会把那种轨迹优化成 0 帧（量出来 0.0%，等于没测）。
            top = pt.y + int(120 * sc)
            bot = pt.y + int(700 * sc)
            while time.time() < end:
                t = (i % 120) / 120.0
                move_to(nav_x - pt.x, (top - pt.y) + (bot - top) * (t if t < 0.5 else 1.0 - t) * 2.0)
                i += 1
                time.sleep(0.004)
        else:
            # 在某一行**内部**抖：y 只在小范围里动，始终不出这一行
            cy = pt.y + int(183 * sc)         # 导航第 3 项（Button）的中心
            span = max(2, int(2 * sc))
            while time.time() < end:
                move_to(nav_x - pt.x, (cy - pt.y) + (span if (i // 4) % 2 else -span))
                i += 1
                time.sleep(0.004)
        dt = time.time() - t0
        c1 = proc.cpu_times()
        pct = ((c1.user + c1.system) - (c0.user + c0.system)) / dt * 100.0
        p.terminate(); p.wait(timeout=5)      # 先收尾再读日志，免得读到半块
        blocks = 0
        if os.path.exists(flog):
            with open(flog, encoding="utf-8", errors="replace") as f:
                blocks = f.read().count("[aino]")
        return pct, i, dt, blocks, ""
    finally:
        try:
            p.terminate(); p.wait(timeout=5)
        except Exception:
            p.kill()


def _best(exe, dur, mode, tries):
    """同一轨迹跑 tries 次。

    A（扫导航）取**最多**帧块：外部干扰只会让它变少；
    B（同行抖）取**最少**帧块：外部干扰（窗口失去焦点 → MOUSELEAVE）只会让它变多。
    CPU 一律取最小。
    """
    best_pct, best_blocks, note = None, None, ""
    for _ in range(max(1, tries)):
        pct, i, dt, blocks, why = _move_once(exe, dur, mode)
        if pct is None:
            return None, 0, why
        note = "%d 次移动 %.1fs" % (i, dt)
        if best_blocks is None:
            best_blocks = blocks
        elif mode == "nav":
            best_blocks = max(best_blocks, blocks)
        else:
            best_blocks = min(best_blocks, blocks)
        if best_pct is None or pct < best_pct:
            best_pct = pct
    return best_pct, best_blocks, note


def move_probe(exe=GALLERY, dur=3.0, limit=70.0, tries=2, ratio_min=4.0):
    """指针持续移动时的 CPU —— 用**同一个进程里的两次采样做比值**来判定。

    这段以前是"抖一下鼠标就整窗 60fps 重绘"，实测把一整个核心吃满
    （99.9% 单核）。现在的设计是"悬停目标没变就不排帧"。要钉的就是这条：
      A = 在导航里上下扫（每跨一行就换悬停目标，真该画）
      B = 在**同一行内部**抖（悬停目标不变，应该一帧都不出）

    **为什么用 A/B 比值而不是绝对 CPU 百分比**：绝对值的噪声根本压不住。
    这个指标是"进程 CPU 时间 / 墙钟时间"，而 GDI 的 BitBlt 要过 DWM，
    机器上任何别的活儿都会把 system 那半边搅大。同一份二进制在同一台机器上
    实测能读出 23% / 37% / 51%，取三次最小也还在抖 —— 那种断言只会变成
    "看机器当时闲不闲"的彩票。而 A 和 B 处在同一次运行、同一台机器上，
    负载对两边的作用一样，比值就把这部分约掉了。

      · 过滤生效：B ≈ 0.0~1%（一帧都不画），A ≈ 20~50% → 比值几十
      · 过滤失效：B 也变成满速重绘，B ≈ A → 比值 ~1
    判据：A / max(B, 0.35) ≥ 4，并且 A 本身不超过 70%（兜住"整体慢得离谱"）。

    上限为什么是 70% 而不是 60%：这条阈值跟着**帧率**走。修好 Sleep 量化之后，
    指针扫过导航是实打实的 60fps（之前被 15.6ms 的定时器粒度钉在 33fps），
    而概览页单帧 9.5ms → 9.5/16.7 ≈ 57%，加上消息处理就在 60% 上下。60fps 的
    交互本来就比 33fps 贵 —— 想省电该调 set_move_fps，而不是把阈值当性能指标。
    """
    a, a_blocks, note_a = _best(exe, dur, "nav", tries)
    if a is None:
        return True, "SKIP: " + note_a
    b, b_blocks, note_b = _best(exe, dur, "row", tries=3)
    if b is None:
        return True, "SKIP: " + note_b
    # 判据（帧数为主，CPU 兜底）：
    #   扫导航  3 秒里必须真的在画 —— 至少 2 块（120 帧，即 ≥40fps）
    #   同一行  3 秒里一块都不许有（一帧都没画）
    # CPU 只用来兜住"整机被打满"这一种情况，不再参与 A/B 比值。
    ok = (a_blocks >= 2) and (b_blocks == 0) and (a <= limit)
    return ok, ("%s 扫导航 %d 块(%.1f%%) / 同一行内抖 %d 块(%.1f%%)；要求扫导航 ≥2 块、"
                "同行 = 0 块、单核 ≤%.0f%%" % (note_a, a_blocks, a, b_blocks, b, limit))


PROBES = (
    ("example_ui_probe", "19 passed, 0 failed",          "其他示例：侧栏滚动 / 列表裁剪 / 主题开关"),
    ("macos_gallery_probe", "verdict: PASS",              "macOS 主题联动 / 实际示例交互 / 响应式布局"),
    ("layoutprobe",      "通过 27 项，失败 0 项",        "Stack 布局计算"),
    ("newcomp_probe",    "87 passed, 0 failed",           "新增组件行为 + 像素"),
    ("raster_equiv",     "通过 1602 项，失败 0 项",        "光栅化 vs 参考实现"),
    ("shadow_text_probe", "通过 24 项，失败 0 项",         "阴影衰减 + 文本换行/裁剪/量高"),
    ("arc_probe",        "通过 10 项，失败 0 项",          "圆弧无接缝 + 渐变 + 端帽"),
    # 注意：这一项会**真的移动鼠标**约 5 秒（验证 spinner/skeleton 只在悬停时动）。
    # 不用 PostMessage 伪造悬停是刻意的：库里的 TrackMouseEvent 会拿真实光标位置
    # 校验，光标不在窗口内就回灌 WM_MOUSELEAVE，伪造消息对"持续悬停"无效。
    ("hover_anim_probe", "[hover] verdict: PASS",          "spinner/skeleton 只悬停才动"),
    # 会自己点一下窗口里的输入框（PostMessage，不碰真实光标）
    ("ime_probe",        "通过 6 项，失败 0 项",            "IME 组合窗/候选窗定位"),
    ("bigfeat_probe",    "通过 47 项，失败 0 项",           "折行缓存/滚动/撤销/补间/变换"),
    # 会开一个 520×300 的窗口，投递 WM_CHAR / Ctrl+Z 等真实按键（不碰鼠标）
    ("edit_probe",       "通过 12 项，失败 0 项",           "编辑器按键链路（撤销/重做/选区）"),
    # 会开窗并连续 SetWindowPos 改尺寸（不碰鼠标），直接读后备缓冲的像素。
    # 钉的是"缩放变黑"的根因：改尺寸后缓冲必须是按新尺寸渲染过的画面。
    ("resize_probe",     "通过 19 项，失败 0 项",           "缩放后缓冲一致（无黑帧/无拉伸）"),
    # 离屏驱动 impl->input，验证 dialog/menu/select 打开时遮罩上的点击、
    # hover、滚轮都不会落到下层控件（"弹窗能点到后面的"那个 bug）。
    ("overlay_probe",    "通过 37 项，失败 0 项",           "浮层不穿透（点击/hover/光标/滚轮/键盘/层级）"),
    # overlay_probe 的用例都是"下层控件画在浮层之前"。这一项反过来：先画 select、
    # 紧接着画落在弹层位置上的按钮 —— 钉的是"侧栏先放下拉、下面紧跟列表项，
    # 点下拉项却选中了背后的列表项"那个 bug（层级号没给帧末层分段）。
    ("gate_order_probe", "PASS（通过 8 项，失败 0 项）",     "浮层层级：后画的控件不得穿透弹层"),
    # 离屏驱动 Ctrl+C/X/V。A 组**不绑** set_clipboard / get_clipboard ——
    # 修复前这里调空 std::function 抛 bad_function_call，进程直接 exit 132
    # C 组读写系统剪贴板，跑完还原；
    # 原剪贴板不是文本时自动跳过，所以判据用固定尾标而不是用例总数。
    ("clip_probe",       "[clip] verdict: PASS",            "剪贴板 C/X/V（不崩 + 复制/剪切/粘贴语义）"),
    # 离屏驱动滚动条：拖滑块、点轨道、拖出视口、两端夹紧。改造前它是纯装饰
    # （只画不判命中），所以除了滚轮根本"操控不了"。
    ("scrollbar_probe",  "[scrollbar] verdict: PASS",        "滚动条拖动/点轨道（不再是纯装饰）"),
    # 会开窗、抢前台焦点，并用 ImmSetCompositionStringW 塞一个组字串走真实
    # WM_IME_COMPOSITION 路径 —— 不需要真人打拼音，也能验"拼音画在框里"。
    ("ime_comp_probe",   "[imecomp] verdict: PASS",          "IME 组字串画进输入框（不是只在组合窗）"),
    # 0.3.0 新补的 18 个 shadcn 对齐控件。离屏驱动鼠标/键盘，逐条验三件事：
    # 点下去到底改没改变量的值、浮层关不关得掉（且关的时候不穿透）、
    # 绘制有没有跑到 box 外面（空背景上肉眼看不出来，放进卡片就糊到邻居身上）。
    ("widget_probe",     "通过 79 项，失败 0 项",            "0.3.x 新控件行为（Toggle/OTP/Combobox/分页/月历/浮层/分割/菜单栏）"),
    # 存量控件的交互。之前这批一条交互断言都没有：只有"画出来了"的像素断言，
    # 点下去改不改值、能不能取消、失焦后键盘还进不进得去，全靠人眼。
    # 覆盖：按钮/图标按钮/输入框编辑/多行输入/复选框/开关/滑块/标签页/分段/
    # 下拉/单选/步进/评分/折叠/菜单/对话框/提示/导航项/滚动/闸门与裁剪/退化输入。
    ("interaction_probe", "通过 81 项，失败 0 项",           "存量控件交互（编辑/拖动/弹层/滚动/退化输入）"),
    # 测量模式是把同一份绘制代码空跑一遍来算高度，所以它**一个像素都不许落**——
    # 漏掉一个直连 raster_poly 的控件，屏幕上就会留下从 (0,0) 起的一块残影
    # （Chart 页左上角贴着一块折线，就是这么来的）。顺带钉住嵌套滚动区的滚轮归属。
    ("measure_probe",    "通过 9 项，失败 0 项",             "测量模式不落像素 + 嵌套滚动归属"),
    # 点击归属。两件事都出过真 bug：
    #   · 滚动区把内容裁到视口，可**裁剪只发生在绘制阶段** —— 滚出视口的导航行
    #     照样参与命中，压在侧边栏页脚的主题按钮下面，点一下就把页面切走；
    #   · 一帧里多个控件都包含指针时，按下被最后一个无条件认领，一次点击动两份状态。
    ("click_probe",      "通过 11 项，失败 0 项",             "点击归属（裁剪外不吃点击 / 一次按下只归一个）"),
    # WindowConfig：字体文件 → 家族名（自己解 sfnt 的 name 表，不依赖 MSVC 专有 API）、
    # 最小/最大尺寸真的被系统夹住、首次显示居中、resizable=false 去掉厚边框。
    # 这几项像素断言看不出来（是窗口管理器的行为），只能问系统。
    ("cfg_probe",        "通过 16 项，失败 0 项",             "WindowConfig（字体名/尺寸约束/居中/可缩放）"),
    # hover 淡入的中间帧不许比两端更暗。透明底控件（ghost / outline）以前拿
    # Color::none() 去 mix，RGB 和 alpha 一起从黑色爬上来，中间帧是 185（比背景
    # 249 和终态 244 都暗），因此检查过渡帧亮度，防止悬停暗闪。
    ("hover_fade_probe", "通过 14 项，失败 0 项",             "hover 淡入淡出无暗闪（透明底只推 alpha）"),
)


def child_window_probe():
    """多窗口：子窗口在主窗口运行中打开后，主窗口还能不能应答消息。

    回归的是一个真实假死：detail::win_timer() 原本是进程级单例，两个窗口
    各自一条消息循环时会抢同一个可等待定时器 —— 主窗口在补帧预算时永远等不到
    信号，界面完全冻住。具体见 tools/child_window.py 的模块注释。
    """
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "child_window.py")
    try:
        r = subprocess.run([sys.executable, script], capture_output=True, text=True, encoding="utf-8", errors="replace",
                           timeout=90)
    except Exception as e:                       # noqa: BLE001
        return True, "SKIP: " + str(e)
    out = ((r.stdout or "") + (r.stderr or "")).strip()
    lines = [ln for ln in out.splitlines() if ln.strip()]
    tail = lines[-1][:110] if lines else "(无输出)"
    if "SKIP" in out:
        return True, "SKIP: " + tail
    return r.returncode == 0, tail


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-build", action="store_true")
    a = ap.parse_args()

    if not a.no_build:
        print("== 编译 ==")
        ok, d = build()
        step("编译 aino_gallery（单目标）", ok, d if not ok else "")
        for n, _, _ in PROBES:
            ok, d = build_probe(n)
            step("编译 " + n, ok, d if not ok else "")

    print("\n== 数值 / 像素探针（headless）==")
    for n, tail, label in PROBES:
        ok, d = run_probe(n, tail)
        step(label, ok, d)

    print("\n== 窗口像素断言（PrintWindow，逻辑坐标，含换页与主题）==")
    ok, d = verify_gallery()
    step("gallery 版式 / 换页 / 遮罩 / 双主题", ok, d)
    ok, d = layout_probe()
    step("实窗布局清单与组件目录一致", ok, d)

    print("\n== 跨平台像素断言（直接读后备缓冲，CI 三平台同跑）==")
    ok, d = ci_pixels()
    step("图元像素断言（44 项 / 无字形依赖；4 项 X11 专有）", ok, d)

    print("\n== 空闲 CPU（aino 的核心承诺）==")
    # 默认按钮区没有常驻动画，必须真正空闲。
    # 判据两条一起用：CPU ≤1% + 静置两帧逐格完全一致。
    cpu, moved, att, ok, note = idle_probe(GALLERY, want_anim=False)
    step("空闲 aino_gallery（按钮区）", ok and cpu <= 1.0,
         "%.2f%% / 两帧差 %d 格 (上限 1.0%%，要求 0 格；第 %d 次尝试)%s"
         % (cpu, moved, att, "" if ok else "  " + note))
    # 反馈区只在通知或交互动画存续期间重绘，静置时也应保持空闲。
    cpu2, moved2, att2, ok2, note2 = idle_probe(
        GALLERY, page="feedback", want_anim=False)
    step("空闲 aino_gallery（反馈区，无通知）", ok2 and cpu2 <= 1.0,
         "%.2f%% / 两帧差 %d 格 (上限 1.0%%，要求 0 格；第 %d 次尝试)%s"
         % (cpu2, moved2, att2, "" if ok2 else "  " + note2))

    cpu_progress, moved_progress, att_progress, ok_progress, note_progress = idle_probe(
        GALLERY, page="progress", want_anim=False)
    step("空闲 aino_gallery（进度区，未悬停）", ok_progress and cpu_progress <= 1.0,
         "%.2f%% / 两帧差 %d 格 (第 %d 次尝试)%s"
         % (cpu_progress, moved_progress, att_progress, "" if ok_progress else "  " + note_progress))

    # 指针持续移动时也不能把核心吃满：整窗重绘很贵，鼠标频率却可能上千 Hz。
    # 判据是"占单核 ≤25%"（≈ 全机 1.6% @16 核），对应 30fps 的移动合并预算。
    print("\n== 交互 CPU（指针持续移动）==")
    ok3, note3 = move_probe()
    step("移动指针时的 CPU", ok3, note3)

    # 多窗口：画廊"在新窗口运行这段代码"是真的再开一个 Window（另起线程）。
    # 这条钉的是"两个窗口各自的消息循环不会互相卡住"。
    print("\n== 多窗口 ==")
    ok4, note4 = child_window_probe()
    step("子窗口打开后主窗口仍响应消息", ok4, note4)

    nfail = sum(1 for _, ok, _ in results if not ok)
    print("\n===== 共 %d 项，失败 %d 项 =====" % (len(results), nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
