"""aino_gallery 的现场验证 —— Fluent Gallery 版式 · 导航 · 浮层层级 · 双主题。

这个脚本补的是探针补不到的那一半：离屏探针里 impl->canvas 是自建的，
不经过 WM_PAINT / DPI 缩放 / BitBlt；而且探针量的是控件内部状态，
量不到"整窗看起来对不对"。

所以这里全部用**区域差异**来断言，不比对固定截图：
  · 换页 → 页头与内容区必须变，导航高亮必须移动
  · 点按钮 → 页头右侧的"最近"回显必须出现
  · 开对话框 → 连左侧导航都要变暗（遮罩是整窗的；被滚动视口裁掉就是 bug）
  · 切主题 → 背景整体变暗

为什么不比对固定图：三个平台的字形光栅完全不同（GDI 灰度 AA / Xft /
CoreText），同一段字的像素必然对不上。差异像素数与亮度统计才跨平台有效。

用法:
  python verify_gallery.py              # 断言
  python verify_gallery.py --shots      # 顺带存 screenshots/gallery-*.png
"""

import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import verify_render as vr                      # noqa: E402

# 生成器不同 exe 落点不同：Ninja/Makefiles 放 build/，VS 多配置放 build/Release。
# 写死 build/ 会让整段断言在没有 VS 的环境里报"找不到 exe"，看着像失败其实是路径。
EXE = os.path.join(ROOT, "build", "aino_gallery.exe")
for _sub in ("", "Release", "Debug"):
    _cand = os.path.join(ROOT, "build", _sub, "aino_gallery.exe")
    if os.path.isfile(_cand):
        EXE = _cand
        break

LOGICAL_W = 1180.0
LOGICAL_H = 760.0
NAV_W = 220.0        # 侧边栏宽（整窗高，红绿灯浮在它上面）
TITLE_H = 52.0      # 标题栏高


def save_png(raw, w, h, path):
    try:
        from PIL import Image
    except ImportError:
        # 没装 Pillow 也能出图：自己按 PNG 规范拼（BGRX → RGB，逐行 filter 0）。
        # 抓图脚本只用来人眼核对版式，不值得为它强制一个第三方依赖。
        _save_png_nopil(raw, w, h, path)
        return
    Image.frombuffer("RGB", (w, h), raw, "raw", "BGRX", 0, 1).save(path)


def _save_png_nopil(raw, w, h, path):
    import struct
    import zlib

    stride = w * 4
    body = bytearray()
    for y in range(h):
        line = raw[y * stride:(y + 1) * stride]
        rgb = bytearray(w * 3)
        rgb[0::3] = line[2::4]      # R
        rgb[1::3] = line[1::4]      # G
        rgb[2::3] = line[0::4]      # B
        body.append(0)              # filter type: None
        body += rgb

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    out = b"\x89PNG\r\n\x1a\n"
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(bytes(body), 6))
    out += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(out)


def close(c1, c2, tol=6):
    return all(abs(a - b) <= tol for a, b in zip(c1[:3], c2[:3]))


def ink_span(s, x0, y0, x1, y1, thr=200):
    """区域内"有暗像素"的纵向跨度（逻辑坐标）。只看哪几行有墨，不看字形。"""
    first = last = None
    ly = y0
    while ly < y1:
        lx = x0
        while lx < x1:
            c = s.at(lx, ly)
            if (c[0] + c[1] + c[2]) / 3 < thr:
                if first is None:
                    first = ly
                last = ly
                break
            lx += 1
        ly += 1
    return first, last


class Report:
    def __init__(self):
        self.results = []
        self.failed = 0

    def step(self, name, ok, detail=""):
        self.results.append((name, ok, detail))
        if ok:
            print("  [ ok ] %s" % name)
        else:
            self.failed += 1
            print("  [FAIL] %s  %s" % (name, detail))


class Shot:
    """一帧整窗位图 + 客户区原点。对外坐标一律用逻辑坐标。"""

    def __init__(self, hwnd, scale):
        self.raw, self.w, self.h, self.ox, self.oy, self.cw, self.ch, self.ok = vr.grab_raw(hwnd)
        self.scale = scale

    def at(self, lx, ly):
        sx = self.ox + int(round(lx * self.scale))
        sy = self.oy + int(round(ly * self.scale))
        return vr.px(self.raw, self.w, sx, sy)

    def diff(self, other, x0, y0, x1, y1, step=2):
        n = 0
        ly = y0
        while ly < y1:
            lx = x0
            while lx < x1:
                if not close(self.at(lx, ly), other.at(lx, ly), 3):
                    n += 1
                lx += step
            ly += step
        return n

    def ink(self, x0, y0, x1, y1, bg, tol=18, step=2):
        """与背景色明显不同的采样点数 —— 证明"这一段确实画了东西"。"""
        n = 0
        ly = y0
        while ly < y1:
            lx = x0
            while lx < x1:
                if not close(self.at(lx, ly), bg, tol):
                    n += 1
                lx += step
            ly += step
        return n

    def avg(self, x0, y0, x1, y1, step=4):
        """区域平均亮度。遮罩变暗、深色主题这类整体变化用它最稳。"""
        tot = 0
        cnt = 0
        ly = y0
        while ly < y1:
            lx = x0
            while lx < x1:
                c = self.at(lx, ly)
                tot += (c[0] * 299 + c[1] * 587 + c[2] * 114) // 1000
                cnt += 1
                lx += step
            ly += step
        return tot / max(cnt, 1)


# ---- 供 regress.py 的 idle_probe 复用：它要"先证明真翻到那页"再采 CPU ----
DIFF_MIN = 20        # 换页至少要有这么多格变化（16px 网格）


def content_cells(raw, w, h, ox, oy, scale):
    """内容区的等比格采样。导航区不采 —— 那一块换页时只有高亮在动。"""
    cells = []
    lx = 250.0
    while lx < LOGICAL_W - 8.0:
        ly = 40.0
        while ly < LOGICAL_H - 10.0:
            sx = ox + int(round(lx * scale))
            sy = oy + int(round(ly * scale))
            cells.append(vr.px(raw, w, sx, sy) if 0 <= sx < w and 0 <= sy < h else None)
            ly += 16.0
        lx += 16.0
    return cells


def diff_cells(a, b):
    return sum(1 for x, y in zip(a, b) if x != y)


def post_click(hwnd, lx, ly, scale):
    vr.click_logical(hwnd, lx, ly, scale)


def client_scale(hwnd):
    r = vr.wintypes.RECT()
    vr.user32.GetClientRect(hwnd, vr.ctypes.byref(r))
    return r.right / LOGICAL_W


def wheel(hwnd, lx, ly, delta, scale, times=1):
    """真实滚轮。WM_MOUSEWHEEL 不更新鼠标位置，而 scroll() 按位置命中，
    所以必须先把光标挪进去，否则滚轮落在 (0,0) 上不生效。"""
    vr.user32.SetForegroundWindow(hwnd)
    pt = vr.wintypes.POINT(int(lx * scale), int(ly * scale))
    vr.user32.ClientToScreen(hwnd, vr.ctypes.byref(pt))
    vr.user32.SetCursorPos(pt.x, pt.y)
    time.sleep(0.15)
    for _ in range(times):
        vr.user32.mouse_event(0x0800, 0, 0, delta, 0)
        time.sleep(0.05)


def send_key(hwnd, vk):
    vr.user32.PostMessageW(hwnd, 0x0100, vk, 0)     # WM_KEYDOWN
    time.sleep(0.05)
    vr.user32.PostMessageW(hwnd, 0x0101, vk, 0)     # WM_KEYUP


def launch(extra_env=None):
    """起一个 gallery，返回 (proc, hwnd)。"""
    env = dict(os.environ)
    if extra_env:
        env.update(extra_env)
    p = subprocess.Popen([EXE], env=env)
    hwnd = None
    for _ in range(80):
        hwnd = vr.find_window_by_pid(p.pid)
        if hwnd:
            break
        time.sleep(0.1)
    return p, hwnd


def stop(p):
    try:
        p.terminate(); p.wait(timeout=5)
    except Exception:
        p.kill()


# 导航行的纵向位置由「组头 24 + 条目 26」决定，页表第一项是概览。
NAV_TOP = TITLE_H + 8.0 + 24.0 + 10.0      # 搜索框下沿 + 留白 = 94
GROUP1_Y = NAV_TOP                          # 「入门」组头
ITEM1_Y = GROUP1_Y + 24.0                   # 概览
GROUP2_Y = ITEM1_Y + 26.0                   # 「基础」组头
BUTTON_Y = GROUP2_Y + 24.0 + 13.0           # Button 条目中心


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shots", action="store_true")
    a = ap.parse_args()

    if not os.path.exists(EXE):
        print("找不到 %s，先跑 tools/build.py" % EXE)
        return 2

    rep = Report()
    proc, hwnd = launch()
    if not hwnd:
        print("  [FAIL] 窗口没建起来")
        return 2
    try:
        time.sleep(2.2)
        scale = client_scale(hwnd)
        print("  窗口 %.0fx%.0f 逻辑，缩放 %.2f" % (LOGICAL_W, LOGICAL_H, scale))

        s0 = Shot(hwnd, scale)
        rep.step("窗口可抓取", s0.ok, "ok=%s" % s0.ok)

        # ---- 1. 外壳结构：标题栏下沿、侧边栏右沿各有一条 1px 分界线 ----
        # 采样"线上 vs 线内 3px"，两条线都必须比邻近底色明显更深。
        hits = 0
        for lx in range(300, 1150, 40):
            if sum(s0.at(lx, TITLE_H - 1)[:3]) + 10 < sum(s0.at(lx, TITLE_H - 4)[:3]):
                hits += 1
        rep.step("标题栏底部有 1px 分界线", hits >= 15, "命中 %d/22 个采样点" % hits)

        hits = 0
        for ly in range(120, 700, 20):
            if sum(s0.at(NAV_W - 1.0, ly)[:3]) + 10 < sum(s0.at(NAV_W - 4.0, ly)[:3]):
                hits += 1
        rep.step("导航右侧有 1px 分界线", hits >= 20, "命中 %d/29 个采样点" % hits)

        # ---- 2. 侧边栏顶部是搜索框（比侧边栏材质亮一档的白底）----
        sidebar_bg = s0.avg(20.0, 300.0, NAV_W - 30.0, 600.0)
        search_luma = s0.avg(16.0, 64.0, NAV_W - 16.0, 80.0)
        rep.step("侧边栏顶部是搜索框", search_luma > sidebar_bg + 3.0,
                 "搜索框亮度 %.0f / 侧边栏 %.0f" % (search_luma, sidebar_bg))

        # ---- 3. 初始页（概览）有内容 ----
        bg_content = s0.at(400.0, 200.0)
        ink0 = s0.ink(NAV_W + 28.0, TITLE_H + 20.0, LOGICAL_W - 40.0, LOGICAL_H - 40.0, bg_content)
        rep.step("概览页有内容", ink0 > 300, "墨迹采样 = %d" % ink0)

        if a.shots:
            save_png(s0.raw, s0.w, s0.h, os.path.join(ROOT, "screenshots", "gallery-overview.png"))

        # ---- 4. 组头文字必须和披露三角在同一条中线上 ----
        # 新版导航：三角在行首（x≈11），文字从 21 起；行高 24。
        gy = GROUP2_Y
        t0, t1 = ink_span(s0, 21.0, gy, 80.0, gy + 24.0)      # 「基础」
        c0, c1 = ink_span(s0, 4.0, gy, 18.0, gy + 24.0)       # 三角
        if t0 is None or c0 is None:
            rep.step("组头文字与三角同中线", False,
                     "找不到墨迹（文字 %s / 三角 %s）—— 导航版式又变了？" % (t0, c0))
        else:
            mid_t = (t0 + t1) * 0.5
            mid_c = (c0 + c1) * 0.5
            rep.step("组头文字与三角同中线", abs(mid_t - mid_c) <= 2.0,
                     "文字中线 %.1f / 三角中线 %.1f（行中 %.1f）" % (mid_t, mid_c, gy + 12.0))

        # ---- 5. 点导航切到 Button ----
        vr.click_logical(hwnd, 110.0, BUTTON_Y, scale)
        time.sleep(0.75)
        s1 = Shot(hwnd, scale)
        navdiff = 0
        for _ in range(6):
            navdiff = s1.diff(s0, 8.0, 100.0, NAV_W - 12.0, 460.0)
            if navdiff > 20:
                break
            time.sleep(0.15)
            s1 = Shot(hwnd, scale)

        head = s1.diff(s0, NAV_W + 28.0, TITLE_H + 10.0, LOGICAL_W - 28.0, TITLE_H + 70.0)
        body = s1.diff(s0, NAV_W + 28.0, TITLE_H + 80.0, LOGICAL_W - 28.0, LOGICAL_H - 30.0)
        rep.step("切页后页头变化", head > 40, "差异采样 = %d" % head)
        rep.step("切页后内容区变化", body > 2000, "差异采样 = %d" % body)
        rep.step("导航高亮移动到 Button", navdiff > 20,
                 "差异采样 = %d（页头 %d / 内容 %d）" % (navdiff, head, body))

        # ---- 6. Group Box：表头下面那条 1px 细线 ----
        # 一条示例 = 表头 30 + 细线 + 预览。第一条 box 从内容区顶端 +6 开始。
        box_top = TITLE_H + 20.0 + 74.0 + 6.0
        line_y = box_top + 29.5
        # 1.5 倍缩放下 1px 的线会落在两个物理像素之间，单点采样量到的是
        # "半条线"（实测只剩 5 级）。这里按行取 10 列的**平均**暗度再找最暗行，
        # 并且扫 ±8（渲染把线对齐到哪一格都可能）。
        darkest, best_y = 0.0, line_y
        for yy in range(int(line_y) - 8, int(line_y) + 9):
            tot = 0
            for lx in range(500, 1000, 50):
                c = s1.at(float(lx), float(yy))
                tot += 255.0 - sum(c[:3]) / 3.0
            d = tot / 10.0
            if d > darkest:
                darkest, best_y = d, float(yy)
        # 背景是 1.5% 的淡底（相对白约 4 级），细线是 10% 分隔线色（约 25 级）。
        rep.step("Group Box 表头下有 1px 细线", darkest >= 10.0,
                 "最暗一行平均 %.0f 级（y=%.0f）" % (darkest, best_y))

        if a.shots:
            save_png(s1.raw, s1.w, s1.h, os.path.join(ROOT, "screenshots", "gallery-button.png"))

        # ---- 7. 外观：标题栏右侧的分段控件，点右段切深色、点左段切回 ----
        seg_x = LOGICAL_W - 16.0 - 108.0
        luma_b = s1.avg(NAV_W + 300.0, 140.0, LOGICAL_W - 300.0, 240.0)
        vr.click_logical(hwnd, seg_x + 81.0, 26.0, scale)        # 「深色」
        time.sleep(0.8)
        s2 = Shot(hwnd, scale)
        luma_d = s2.avg(NAV_W + 300.0, 140.0, LOGICAL_W - 300.0, 240.0)
        rep.step("分段控件切深色", luma_d < luma_b - 100.0, "内容区亮度 %.0f → %.0f" % (luma_b, luma_d))
        if a.shots:
            save_png(s2.raw, s2.w, s2.h, os.path.join(ROOT, "screenshots", "gallery-dark.png"))

        vr.click_logical(hwnd, seg_x + 27.0, 26.0, scale)        # 「浅色」
        time.sleep(0.8)
        s3 = Shot(hwnd, scale)
        luma_l = s3.avg(NAV_W + 300.0, 140.0, LOGICAL_W - 300.0, 240.0)
        rep.step("分段控件切回浅色", luma_l > 180.0, "内容区亮度 %.0f" % luma_l)

        # 切主题不能顺带切页：整块内容区必须回到原样
        restored = s3.diff(s1, NAV_W + 28.0, TITLE_H + 80.0, LOGICAL_W - 28.0, LOGICAL_H - 40.0, step=3)
        rep.step("切主题不顺带切页", restored < 40, "差异采样 = %d" % restored)
    finally:
        stop(proc)

    # ---- 8. 对话框 ----
    # macOS 的模态**不压暗整窗**（窗口只是失去焦点），所以这里查的是"导航亮度
    # 不变 + 中间多出一块浮起的面板"，而不是"整屏变暗"。判据全部跟同一页不开
    # 对话框的基线比，不写死绝对亮度。
    def nav_luma_of(env):
        p2, h2 = launch(env)
        if not h2:
            return None, None, None
        time.sleep(2.2)
        sc2 = client_scale(h2)
        sh = Shot(h2, sc2)
        v = sh.avg(20.0, 150.0, NAV_W - 30.0, 420.0)
        return p2, h2, (sh, sc2, v)

    p_b, h_b, base = nav_luma_of({"AINO_START_PAGE": "Dialog"})
    if base is None:
        print("  [FAIL] 对话框页没建起来")
        return 2
    stop(p_b)
    nav_base = base[2]

    proc, hwnd = launch({"AINO_START_PAGE": "Dialog", "AINO_OPEN": "dialog"})
    if not hwnd:
        print("  [FAIL] 对话框窗口没建起来")
        return 2
    try:
        time.sleep(2.2)
        scale = client_scale(hwnd)
        s4 = Shot(hwnd, scale)
        nav_luma = s4.avg(20.0, 150.0, NAV_W - 30.0, 420.0)
        rep.step("对话框不压暗整窗（macOS 无黑幕）", abs(nav_luma - nav_base) <= 5.0,
                 "导航亮度 %.0f → %.0f" % (nav_base, nav_luma))
        # 不再用"整块区域的平均亮度"判断面板是否盖住内容：面板 #F6F6F6（246）和
        # 示例卡 #F2F2F2（242）只差 4 级，平均下来分不开。真正的判据是下面那条
        # 「面板是一整块实心面」——面板内一条竖带的同色占比，开/关差别是 0.95 vs 0.2。

        # 面板左侧那一条竖带（内缩 15~5px，避开通栏文字）在打开时应该是**同一种
        # 面板色**；关掉之后那里是示例页的卡面/页底，颜色立刻变杂。
        def panel_strip(sh):
            px0, px1 = LOGICAL_W * 0.5 - 185.0, LOGICAL_W * 0.5 - 175.0
            py0, py1 = LOGICAL_H * 0.5 - 90.0, LOGICAL_H * 0.5 + 90.0
            target = sh.avg(px0, py0, px1, py1)
            hit = tot = 0
            for yy in range(int(py0), int(py1), 3):
                for xx in range(int(px0), int(px1), 2):
                    r, g, b = sh.at(xx, yy)[:3]
                    tot += 1
                    if abs((r + g + b) / 3.0 - target) <= 1.5:
                        hit += 1
            return (hit / float(tot) if tot else 0.0)

        strip_open = panel_strip(s4)
        rep.step("面板是一整块实心面（不是透明框）", strip_open >= 0.95,
                 "同色占比 %.2f" % strip_open)
        if a.shots:
            save_png(s4.raw, s4.w, s4.h, os.path.join(ROOT, "screenshots", "gallery-dialog.png"))

        # 点面板外的遮罩关掉。Esc 也支持，但那要真实键盘焦点 —— 自动化环境里
        # 窗口抢不到前台时 send_key 会被系统吞掉（实测按了没反应）。
        vr.click_logical(hwnd, 40.0, LOGICAL_H - 40.0, scale)
        time.sleep(0.7)
        s5 = Shot(hwnd, scale)
        strip_closed = panel_strip(s5)
        rep.step("点面板外关闭（面板消失）", strip_closed < strip_open - 0.25,
                 "同色占比 %.2f → %.2f" % (strip_open, strip_closed))
    finally:
        stop(proc)

    print("\n===== 共 %d 项，失败 %d 项 =====" % (len(rep.results), rep.failed))
    return 1 if rep.failed else 0


if __name__ == "__main__":
    sys.exit(main())
