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
GALLERY = os.path.join(DEMO, "aino_gallery.exe")

results = []

# ---- 「到底动没动」的判据：静置时抓两帧（间隔 0.25s）比对内容区的采样格 ----
#   动画页（spinner 在转、skeleton 在呼吸）必然有若干格变；空闲页必须逐格完全一致。
#   阈值不用很大：spinner 在 16px 网格下只会落到少数几格里，实测 20~40 格。
ANIM_MIN = 3         # 动画页至少要差这么多格（实测 6~7，留一倍余量）
IDLE_MAX = 0         # 空闲页两帧必须完全一致（一个格都不许变）
FRAME_GAP = 0.30     # 抓两帧的间隔（秒）；spinner 约 1.4 圈/秒，0.15s 只差 4 格、0.3s 有 7 格


def run(cmd, cwd=None, env=None, timeout=300):
    return subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True,
                          errors="replace", timeout=timeout)


def step(name, ok, detail=""):
    results.append((name, ok, detail))
    mark = "OK  " if ok else "FAIL"
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
    ok = want_tail in out
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


def idle_probe(exe, dur=3.0, click=None, expect_nav=None, want_anim=None, tries=3):
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

    for attempt in range(1, tries + 1):
        p = subprocess.Popen([exe], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
    """gallery 的布局自检：把每一页每一条示例都量一遍，要求"没有内容超过声明高度"。

    这一条钉的是用户直接看得见的 bug：卡片高度原先全靠手填的 preview_h，
    内容一多就爬出卡片下边框。现在绘制前先在测量模式下空跑一遍，取
    max(声明, 实测)，所以溢出在结构上不可能发生；本断言确保测量机制没坏、
    也没有哪条示例离声明值太远（太远说明卡片会留一大块空白）。
    """
    exe = os.path.join(ROOT, "build", "aino_gallery.exe")
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
    bad = [l for l in txt.splitlines() if "溢出" in l]
    summ = [l for l in txt.splitlines() if l.startswith("# over")]
    ok = not bad and bool(summ) and "over=0" in summ[0]
    detail = (summ[0].lstrip("# ") if summ else "没有统计行")
    if bad:
        detail += "  " + "; ".join(x.split("/")[-1].strip() for x in bad[:3])
    return ok, detail


def move_probe(exe=GALLERY, dur=3.0, limit=32.0):
    """指针持续移动时的 CPU（占**单核**百分比）。

    这段以前是"抖一下鼠标就整窗 60fps 重绘"，实测把一整个核心吃满
    （99.9% 单核 / 全机 6.25%）。后来按 30fps 合并降到 17~23%，但那是靠
    降帧率换的，看得见。现在换成"悬停目标没变就不重绘"：画圈时只有跨过
    控件边界才排帧，实测 6.8~12% 单核（全机 0.4~0.75%），而且悬停反馈
    是满刷新率的。
    判据 32% 单核 = 16 核机器上的 2%，正好是"交互时全机占用 <2%"那条线。
    """
    import psutil
    import ctypes
    import ctypes.wintypes as wt
    import math
    sys.path.insert(0, HERE)
    import verify_render as vr

    p = subprocess.Popen([exe], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(2.5)                       # 预热：字体初始化 + 首帧
        hwnd = vr.find_window_by_pid(p.pid, 5.0)
        if not hwnd:
            return False, "窗口没找到"
        vr.user32.SetForegroundWindow(hwnd)
        rc = wt.RECT()
        vr.user32.GetClientRect(hwnd, ctypes.byref(rc))
        pt = wt.POINT(0, 0)
        vr.user32.ClientToScreen(hwnd, ctypes.byref(pt))
        cx = pt.x + rc.right // 2
        cy = pt.y + rc.bottom // 2

        proc = psutil.Process(p.pid)
        c0 = proc.cpu_times(); t0 = time.time()
        end = t0 + dur
        i = 0
        # 在左侧导航里上下扫：那里每一行都是可交互的 nav_item，跨过边界就要重绘，
        # 是最坏情况（真会改变画面）。以前在窗口空白处画圈，那区域一个控件都没有，
        # 现在会被"悬停没变就不画"优化成 0 帧 —— 量出来 0.0%，等于没测。
        nav_x = pt.x + int(110 * (rc.right / 1180.0))
        top = pt.y + int(120 * (rc.right / 1180.0))
        bot = pt.y + int(700 * (rc.right / 1180.0))
        while time.time() < end:
            t = (i % 120) / 120.0
            y = int(top + (bot - top) * (t if t < 0.5 else 1.0 - t) * 2.0)
            vr.user32.SetCursorPos(nav_x, y)
            i += 1
            time.sleep(0.004)                 # ~200 次/秒的移动
        dt = time.time() - t0
        c1 = proc.cpu_times()
        pct = ((c1.user + c1.system) - (c0.user + c0.system)) / dt * 100.0
        return pct <= limit, "%d 次移动 %.1fs 内  单核 %.1f%%（上限 %.0f%%）" % (i, dt, pct, limit)
    finally:
        try:
            p.terminate(); p.wait(timeout=5)
        except Exception:
            p.kill()


PROBES = (
    ("layoutprobe",      "通过 27 项，失败 0 项",        "Stack 布局计算"),
    ("newcomp_probe",    "83 passed, 0 failed",           "新增组件行为 + 像素"),
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
    ("overlay_probe",    "通过 36 项，失败 0 项",           "浮层不穿透（点击/hover/光标/滚轮/键盘/层级）"),
    # 离屏驱动 Ctrl+C/X/V。A 组**不绑** set_clipboard / get_clipboard ——
    # 修复前这里调空 std::function 抛 bad_function_call，进程直接 exit 132
    # （用户报的"按 Ctrl+C 闪退"）。C 组真读写系统剪贴板，跑完还原；
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
    ("widget_probe",     "通过 76 项，失败 0 项",            "0.3.x 新控件行为（Toggle/OTP/Combobox/分页/月历/浮层/分割/菜单栏）"),
    # 存量控件的交互。之前这批一条交互断言都没有：只有"画出来了"的像素断言，
    # 点下去改不改值、能不能取消、失焦后键盘还进不进得去，全靠人眼。
    # 覆盖：按钮/图标按钮/输入框编辑/多行输入/复选框/开关/滑块/标签页/分段/
    # 下拉/单选/步进/评分/折叠/菜单/对话框/提示/导航项/滚动/闸门与裁剪/退化输入。
    ("interaction_probe", "通过 80 项，失败 0 项",           "存量控件交互（编辑/拖动/弹层/滚动/退化输入）"),
    # 测量模式是把同一份绘制代码空跑一遍来算高度，所以它**一个像素都不许落**——
    # 漏掉一个直连 raster_poly 的控件，屏幕上就会留下从 (0,0) 起的一块残影
    # （Chart 页左上角贴着一块折线，就是这么来的）。顺带钉住嵌套滚动区的滚轮归属。
    ("measure_probe",    "通过 9 项，失败 0 项",             "测量模式不落像素 + 嵌套滚动归属"),
)


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
    step("卡片高度够（内容不爬出下边框）", ok, d)

    print("\n== 跨平台像素断言（直接读后备缓冲，CI 三平台同跑）==")
    ok, d = ci_pixels()
    step("图元像素断言（29 项 / 无字形依赖）", ok, d)

    print("\n== 空闲 CPU（aino 的核心承诺）==")
    # 默认落在「概览」页：这一页没有任何常驻动画，必须真正空闲 —— 这是硬指标。
    # 判据两条一起用：CPU ≤1% + 静置两帧逐格完全一致。
    cpu, moved, att, ok, note = idle_probe(GALLERY, want_anim=False)
    step("空闲 aino_gallery（概览页）", ok and cpu <= 1.0,
         "%.2f%% / 两帧差 %d 格 (上限 1.0%%，要求 0 格；第 %d 次尝试)%s"
         % (cpu, moved, att, "" if ok else "  " + note))
    # 「展示」页有 spinner + 两条呼吸 skeleton。它们现在**只在鼠标悬停时**推进
    # （spinner / skeleton 都改成 opt-in 的局部相位），所以不碰鼠标时这一页也必须是
    # 0 帧 / ≤1%。这条以前是宽松的 55%（那时它们无条件 repaint），现在收紧成和概览页
    # 同一条硬指标 —— 常驻动画从"设计内开销"降级为"必须显式点名"。
    cpu2, moved2, att2, ok2, note2 = idle_probe(
        GALLERY, click=(100, 273), want_anim=False)
    step("空闲 aino_gallery（Progress 页，未悬停）", ok2 and cpu2 <= 1.0,
         "%.2f%% / 两帧差 %d 格 (上限 1.0%%，要求 0 格；第 %d 次尝试)%s"
         % (cpu2, moved2, att2, "" if ok2 else "  " + note2))

    # 指针持续移动时也不能把核心吃满：整窗重绘很贵，鼠标频率却可能上千 Hz。
    # 判据是"占单核 ≤25%"（≈ 全机 1.6% @16 核），对应 30fps 的移动合并预算。
    print("\n== 交互 CPU（指针持续移动）==")
    ok3, note3 = move_probe()
    step("移动指针时的 CPU", ok3, note3)

    nfail = sum(1 for _, ok, _ in results if not ok)
    print("\n===== 共 %d 项，失败 %d 项 =====" % (len(results), nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
