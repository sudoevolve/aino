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

EXE = os.path.join(ROOT, "build", "aino_gallery.exe")

LOGICAL_W = 1180.0
LOGICAL_H = 760.0
NAV_W = 232.0


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shots", action="store_true")
    a = ap.parse_args()

    if not os.path.exists(EXE):
        print("找不到 %s，先跑 tools/build.py" % EXE)
        return 2

    proc = subprocess.Popen([EXE])
    rep = Report()
    try:
        hwnd = None
        for _ in range(60):
            hwnd = vr.find_window_by_pid(proc.pid)
            if hwnd:
                break
            time.sleep(0.1)
        if not hwnd:
            print("  [FAIL] 窗口没建起来")
            return 2
        time.sleep(2.2)
        scale = client_scale(hwnd)
        print("  窗口 %.0fx%.0f 逻辑，缩放 %.2f" % (LOGICAL_W, LOGICAL_H, scale))

        s0 = Shot(hwnd, scale)
        rep.step("窗口可抓取", s0.ok, "ok=%s" % s0.ok)

        # ---- 1. 结构：导航与内容之间有一条分界线 ----
        bg_content = s0.at(400.0, 90.0)
        line_dark = 0
        for ly in range(120, 680, 20):
            c_line = s0.at(NAV_W - 1.0, ly)
            c_in = s0.at(NAV_W - 10.0, ly)
            if sum(c_line[:3]) + 10 < sum(c_in[:3]):
                line_dark += 1
        rep.step("导航右侧有 1px 分界线", line_dark >= 20, "命中 %d/28 个采样点" % line_dark)

        # ---- 2. 初始页（概览）有内容 ----
        ink0 = s0.ink(NAV_W + 28.0, 110.0, LOGICAL_W - 40.0, LOGICAL_H - 60.0, bg_content)
        rep.step("概览页有内容", ink0 > 300, "墨迹采样 = %d" % ink0)

        if a.shots:
            save_png(s0.raw, s0.w, s0.h, os.path.join(ROOT, "screenshots", "gallery-overview.png"))

        # ---- 2.5 页脚的主题按钮：列表比视口长时也必须点得到 ----
        # 导航有 45 项、视口只装得下 20 来行。滚出视口的那些行**看不见但还在**，
        # 以前照样参与命中：它们平平地压在页脚上，点主题按钮会被"看不见的行"
        # 吃掉 —— 主题没切换，页面还莫名跳走了（用户原话："换夜间模式按钮后面
        # 有东西的时候就点不动"）。
        # 这里刻意在**初始状态**（导航没滚过）点，那正是被压住的状态。
        luma_b = s0.avg(NAV_W + 300.0, 120.0, LOGICAL_W - 300.0, 200.0)
        vr.click_logical(hwnd, 33.0, 734.0, scale)
        time.sleep(0.7)
        s_dark = Shot(hwnd, scale)
        luma_a = s_dark.avg(NAV_W + 300.0, 120.0, LOGICAL_W - 300.0, 200.0)
        rep.step("页脚主题按钮可点（导航未滚动）", luma_a < luma_b - 100.0,
                 "内容区亮度 %.0f → %.0f" % (luma_b, luma_a))

        vr.click_logical(hwnd, 33.0, 734.0, scale)     # 切回来
        time.sleep(0.7)
        s_back = Shot(hwnd, scale)
        # 两次点击之后画面必须回到原样 —— 只要页面被顺带切走，这里就是几百个差异格。
        restored = s_back.diff(s0, NAV_W + 20.0, 40.0, LOGICAL_W - 20.0, LOGICAL_H - 80.0, step=3)
        rep.step("切主题不顺带切页", restored < 40, "差异采样 = %d" % restored)

        # ---- 2.6 组头文字必须和折叠三角在同一条中线上 ----
        # 用户原话："折叠选单文本为什么没居中，不允许再出现这类问题"。根因是把文字
        # 盒子写成 16 高再手算 y + 12 —— 行高 30 里整体偏上。现在盒子给整行，靠
        # Style 的 valign 居中。这里量"墨迹中线 vs 行中线"，行高或偏移一改就红。
        GY = 138.0                              # 「基础」组头：78 品牌 + 30 + 30
        gy_c = GY + 15.0                        # 行中线
        t0, t1 = ink_span(s0, 20.0, GY, 60.0, GY + 30.0)      # 文字
        c0, c1 = ink_span(s0, 196.0, GY, 226.0, GY + 30.0)    # 折叠三角
        mid_t = (t0 + t1) * 0.5
        mid_c = (c0 + c1) * 0.5
        # 文字墨迹天然不对称（有的字没下伸部），所以给 ±3 的容差；旧的 y + 12 偏 5px。
        rep.step("组头文字与折叠三角同中线",
                 abs(mid_t - mid_c) <= 2.0 and abs(mid_t - gy_c) <= 3.0,
                 "文字中线 %.1f / 三角中线 %.1f / 行中线 %.1f" % (mid_t, mid_c, gy_c))

        # ---- 3. 点导航切到 Button ----
        vr.click_logical(hwnd, 110.0, 183.0, scale)
        time.sleep(0.75)
        # 抓帧偶尔会得到一张"内容已经是新的一页、导航还是旧高亮"的拼贴帧。
        # 根因不是抓帧时机：点导航改的是**后面才画**的内容区状态，而导航行本身
        # 这一帧早就画完了 —— 状态改完没人再排帧，这帧错位画面就挂在屏幕上
        # （用户看到的是"页面换了、高亮卡住"）。现在 interact() 返回 pressed /
        # clicked 时会补一次 request_repaint，这里仍留着"多抓几帧"兜底。
        # 注意：**不重试点击**，只重抓。点两次会切换两次状态，那是改语义。
        s1 = Shot(hwnd, scale)
        navdiff = 0
        for _ in range(6):
            navdiff = s1.diff(s0, 8.0, 100.0, NAV_W - 12.0, 420.0)
            if navdiff > 20:
                break
            time.sleep(0.15)
            s1 = Shot(hwnd, scale)

        head = s1.diff(s0, NAV_W + 20.0, 20.0, LOGICAL_W - 20.0, 100.0)
        rep.step("切页后页头变化", head > 40, "差异采样 = %d" % head)

        body = s1.diff(s0, NAV_W + 20.0, 110.0, LOGICAL_W - 20.0, LOGICAL_H - 40.0)
        rep.step("切页后内容区变化", body > 2000, "差异采样 = %d" % body)

        # 带上页头 / 内容区的读数：万一将来又红了，一眼能分清是"页没切"
        # （三个都 0 → 点击被吞）还是"高亮没跟上"（只有 nav 是 0）。
        rep.step("导航高亮移动到 Button", navdiff > 20,
                 "差异采样 = %d（页头 %d / 内容 %d）" % (navdiff, head, body))

        # ---- 4. 卡片结构：预览区必须是独立的一块 ----
        #
        # 以前这条比的是"预览区底色 ≠ 卡片底色"（板子铺 muted.mix(background, 0.12)）。
        # 那个做法本身是个 bug 的源头：浅色主题里 secondary / muted / accent 都是
        # #F4F4F5，板子铺 #F5F5F6 时，头像的圆、Secondary 按钮、骨架、徽章全都
        # 糊在板子里看不见。现在板子改铺 background（和 shadcn 官网的预览区一致），
        # 分层交给 1px border —— 所以这里改成断言"边界线真的画出来了"，
        # 比原来的"底色差 3 级"更严：底色相同只剩描边一条线索，描边丢了就整块糊平。
        card_bg = s1.at(NAV_W + 60.0, 128.0)
        plate_bg = s1.at(NAV_W + 90.0, 200.0)
        edge = 0.0
        for px in range(int(NAV_W + 18), int(NAV_W + 130)):
            c = s1.at(float(px), 200.0)
            edge = max(edge, 255.0 - sum(c[:3]) / 3.0)
        rep.step("预览区有 1px 边界（描边）", edge >= 8.0,
                "最深一条暗 %.0f 级（card=%s plate=%s）" % (edge, card_bg[:3], plate_bg[:3]))

        # ---- 5. 代码块确实画了字 ----
        code_x0 = LOGICAL_W - 56.0 - 40.0 - 300.0
        code_ink = s1.ink(code_x0, 150.0, LOGICAL_W - 70.0, 330.0, plate_bg)
        rep.step("代码块有文字", code_ink > 200, "墨迹采样 = %d" % code_ink)

        # ---- 6. 点预览区的按钮 → 页头出现"最近"回显 ----
        vr.click_logical(hwnd, NAV_W + 28.0 + 14.0 + 20.0 + 60.0, 122.0 + 14.0 + 18.0, scale)
        time.sleep(0.6)
        s2 = Shot(hwnd, scale)
        # 回显画在页头第二行右侧（y 约 58..76），不是标题那一行
        echo = s2.diff(s1, 600.0, 44.0, LOGICAL_W - 120.0, 90.0)
        rep.step("点按钮后页头出现交互回显", echo > 15, "差异采样 = %d" % echo)

        if a.shots:
            save_png(s2.raw, s2.w, s2.h, os.path.join(ROOT, "screenshots", "gallery-button.png"))

        # ---- 7. 浮层：滚到 Dialog 页，开对话框，遮罩必须盖住导航 ----
        wheel(hwnd, 110.0, 400.0, -120, scale, times=30)
        time.sleep(0.5)
        s_nav = Shot(hwnd, scale)
        vr.click_logical(hwnd, 110.0, 263.0, scale)      # Dialog
        time.sleep(0.8)
        s3 = Shot(hwnd, scale)
        moved = s3.diff(s_nav, 240.0, 20.0, LOGICAL_W - 20.0, 110.0)
        rep.step("滚到 Dialog 并切页", moved > 40, "页头差异 = %d" % moved)

        nav_before = s3.avg(20.0, 150.0, NAV_W - 30.0, 400.0)
        # Dialog 页的代码比预览高，内容盒因此在卡片里垂直居中下移了约 12px，
        # 按钮中心落在 y≈182 而不是 170。
        vr.click_logical(hwnd, 354.0, 182.0, scale)
        time.sleep(0.8)
        s4 = Shot(hwnd, scale)
        nav_after = s4.avg(20.0, 150.0, NAV_W - 30.0, 400.0)
        rep.step("对话框遮罩覆盖整窗（导航也变暗）", nav_after < nav_before - 25.0,
                 "导航亮度 %.0f → %.0f" % (nav_before, nav_after))

        panel_luma = s4.avg(LOGICAL_W * 0.5 - 80, LOGICAL_H * 0.5 - 40,
                            LOGICAL_W * 0.5 + 80, LOGICAL_H * 0.5 + 40)
        rep.step("面板位于窗口中央且不透明", panel_luma > nav_after + 60.0,
                 "面板亮度 %.0f，遮罩 %.0f" % (panel_luma, nav_after))

        if a.shots:
            save_png(s4.raw, s4.w, s4.h, os.path.join(ROOT, "screenshots", "gallery-dialog.png"))

        send_key(hwnd, 0x1B)                              # Esc
        time.sleep(0.5)
        s5 = Shot(hwnd, scale)
        nav_closed = s5.avg(20.0, 150.0, NAV_W - 30.0, 400.0)
        rep.step("Esc 关闭后遮罩撤掉", nav_closed > nav_before - 8.0,
                 "导航亮度 %.0f（初始 %.0f）" % (nav_closed, nav_before))

        # ---- 8. 深色主题 ----
        vr.click_logical(hwnd, 33.0, 734.0, scale)
        time.sleep(0.7)
        s6 = Shot(hwnd, scale)
        luma_dark = s6.avg(NAV_W + 300.0, 120.0, LOGICAL_W - 300.0, 200.0)
        rep.step("切深色后背景变暗", luma_dark < 90.0, "内容区亮度 %.0f" % luma_dark)
        if a.shots:
            save_png(s6.raw, s6.w, s6.h, os.path.join(ROOT, "screenshots", "gallery-dark.png"))

        vr.click_logical(hwnd, 33.0, 734.0, scale)
        time.sleep(0.6)
        s7 = Shot(hwnd, scale)
        luma_light = s7.avg(NAV_W + 300.0, 120.0, LOGICAL_W - 300.0, 200.0)
        rep.step("切回浅色", luma_light > 180.0, "内容区亮度 %.0f" % luma_light)

        print("\n===== 共 %d 项，失败 %d 项 =====" % (len(rep.results), rep.failed))
        return 1 if rep.failed else 0
    finally:
        time.sleep(0.3)
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
