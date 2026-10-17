"""aino_gallery 的像素级验证 —— 单窗口 · 侧边栏分页 · 浅深双主题。

三个 exe 合并成一个之后，"点侧边栏换页"本身就是一条必须固化的行为，
所以这个脚本的核心不是"截图看着对"，而是三件事：

  1. 【结构断言】侧边栏底色 / 内容底 / 1px 分界线 / 选中竖条 —— 写死色值，
     直接证明布局位置算对了（竖条落在 box.x+3，说明 nav_item 的 rect 是对的）。
  2. 【差分断言】逐页点导航项，比较内容区的采样指纹：相邻两页必须有足够多的
     像素发生变化。这能同时抓住"点击没命中"和"换页没重绘"两类故障，
     而且不需要我为每一页写一串魔法坐标。
  3. 【色块质量断言】区域里某种颜色出现了多少像素（比如"按钮页必须有
     ≥N 个像素接近 destructive 红"）—— 用颜色统计锚定"确实是那一页"，
     比在文字上采样稳得多（文字是抗锯齿过渡色，实测踩过坑）。

为什么不用肉眼看图：本机沙箱对返回的图片做了内容过滤，截图拿到了也看不见。
有客观数字才叫验证过。

用法:
  python verify_gallery.py                # 全跑（浅 + 深）
  python verify_gallery.py --calibrate    # 只打印实测值，用来重新标定阈值
  python verify_gallery.py --shots        # 顺带把 6 页各存一张 png
"""

import argparse
import os
import subprocess
import sys
import time
from ctypes import wintypes

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import verify_render as vr                      # noqa: E402  复用抓图/点击/取色

EXE = os.path.join(ROOT, "build", "aino_gallery.exe")

# ---- 逻辑尺寸（aino::Window win(1120, 720, ...)）----
LOGICAL_W = 1120.0

# ---- 侧边栏导航项的纵向中心（来自 composeSidebar 的 vbox(sb, gap=3, pad=20)）----
#   insets 后内容 (20,20,176,680)：
#     heading 32 → muted 18 → sep 1 → 之后 8 个 34 高、间距 3 的 nav_item
#   即 102+37*i ~ 136+37*i，中心 119+37*i。下面这段是用标定脚本实测确认过的。
NAV_Y = [119 + 37 * i for i in range(6)]
NAV_X = 100          # 取在文字左边的空白处，避开"选中换前景色"带来的干扰
BAR_X = 24           # nav_item 的选中竖条在 box.x+3 .. +6

THEME_BTN = (38, 658)   # 左下角主题切换按钮中心

PAGES = ["概览", "控件", "展示", "布局", "浮层", "动效"]

# ---- 浅 / 深主题的写死色值（都来自 Theme::light()/dark() 的推导，已实测确认）----
#   sidebar = background.mix(muted, 0.6)
#   light: (255,255,255) x0.4 + (244,244,245) x0.6 = (248,248,249)
#   dark : (9,9,11)      x0.4 + (39,39,42)    x0.6 = (27,27,30)
LIGHT = {
    "sidebar_bg":    ((110, 430), (248, 248, 249), 2),
    "content_bg":    ((1100, 400), (255, 255, 255), 2),
    "divider":       ((215, 400), (228, 228, 231), 2),    # th.border #E4E4E7
    "active_bar_on": ((BAR_X, NAV_Y[0]), (41, 41, 44), 8),  # primary 叠 235/255
    "active_bar_off": ((BAR_X, NAV_Y[3]), (248, 248, 249), 2),
    "nav_sel_bg":    ((150, NAV_Y[0]), (244, 244, 245), 2),  # th.accent
}
DARK = {
    "sidebar_bg":    ((110, 430), (27, 27, 30), 2),
    "content_bg":    ((1100, 400), (9, 9, 11), 2),
    "divider":       ((215, 400), (39, 39, 42), 2),          # th.border #27272A
    "active_bar_on": ((BAR_X, NAV_Y[0]), (233, 233, 234), 8),
    "active_bar_off": ((BAR_X, NAV_Y[3]), (27, 27, 30), 2),
    "nav_sel_bg":    ((150, NAV_Y[0]), (39, 39, 42), 2),     # th.accent
}

# ---- 色块质量断言：区域里某色出现多少像素 ----
#   数量是【逻辑像素】级别的（采样按逻辑坐标走，与 DPI 缩放无关），
#   所以阈值可以写死。两个下限都留了 3 倍余量。
MASS = [
    # 按钮页左栏第 5 个按钮是 Destructive（#EF4444），实心块 ≈ 236x36，
    # 扣掉"Destructive"这行字之后实测 8203
    (1, (256, 130, 510, 700), (0xEF, 0x44, 0x44), 5000),
    # 概览页两条 progress 的已填充段是 primary #18181B，实测 1855
    (0, (540, 130, 800, 700), (0x18, 0x18, 0x1B), 1200),
]

# 相邻两页内容区至少要有这么多采样格变化。
# 这个数的唯一用途是"证明翻页真的生效了"（没生效时约等于 0），不是衡量页面丰富度。
# 2026-10-17：清掉 gallery 里的说明性文字后，浮层页从 257 掉到 234 ——
# 那是版面变干净了，不是翻页失效；同时"竖条跟随/复位"那条判据仍然成立，
# 所以把阈值下调到 200（实测最低 234，仍有 17% 余量）。
DIFF_MIN = 200


# ---------------------------------------------------------------------------
#  采样工具
# ---------------------------------------------------------------------------
def dist(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2]))


def save_png(raw, w, h, path):
    from PIL import Image
    Image.frombuffer("RGB", (w, h), raw, "raw", "BGRX", 0, 1).save(path)


def content_cells(raw, w, h, ox, oy, scale):
    """内容区（逻辑 x>=230, y in [40,706]）的等比格采样。"""
    cells = []
    lx = 230
    while lx < LOGICAL_W - 8:
        ly = 40
        while ly < 706:
            sx = ox + int(round(lx * scale))
            sy = oy + int(round(ly * scale))
            if 0 <= sx < w and 0 <= sy < h:
                cells.append(vr.px(raw, w, sx, sy))
            else:
                cells.append(None)
            ly += 16
        lx += 16
    return cells


def diff_cells(a, b):
    n = 0
    for x, y in zip(a, b):
        if x is None or y is None:
            continue
        if max(abs(x[0] - y[0]), abs(x[1] - y[1]), abs(x[2] - y[2])) > 8:
            n += 1
    return n


def color_mass(raw, w, h, ox, oy, scale, box, want, tol=8):
    """区域里与 want 距离 <= tol 的像素个数。"""
    x0, y0, x1, y1 = box
    n = 0
    ly = y0
    while ly < y1:
        lx = x0
        while lx < x1:
            sx = ox + int(round(lx * scale))
            sy = oy + int(round(ly * scale))
            if 0 <= sx < w and 0 <= sy < h:
                c = vr.px(raw, w, sx, sy)
                if dist(c, want) <= tol:
                    n += 1
            lx += 1
        ly += 1
    return n


class Report:
    def __init__(self):
        self.results = []

    def step(self, name, ok, detail=""):
        self.results.append((name, ok, detail))
        print("  [%s] %-40s %s" % ("OK  " if ok else "FAIL", name, detail))
        return ok

    @property
    def failed(self):
        return sum(1 for _, ok, _ in self.results if not ok)


# ---------------------------------------------------------------------------
#  点击 —— 必须"投递 → 校验 → 不行就重投"
# ---------------------------------------------------------------------------
def post_click(hwnd, lx, ly, scale):
    """投递一次点击：move → down → up，各留 50ms（≈ 各占一帧）。

    实测过两种投法：
      · 三条消息背靠背挤在一起投（指望它们落在同一帧里被一次抽干）——
        不可靠，7 个导航项只成功 1 个。别这么干。
      · 各留 50ms 分开投（就是 verify_render.click_logical 的做法）——
        稳定成功，7/7。
    所以这里直接复用 click_logical。
    """
    vr.click_logical(hwnd, lx, ly, scale)


def click_until(hwnd, lx, ly, scale, check, tries=5, settle=0.45):
    """点一次 → 抓图 → 用 check 判断"真的生效了吗"，没生效就再点。

    返回 (ok, raw, w, h, ox, oy, scale, 实际尝试次数)。
    """
    res = None
    for k in range(1, tries + 1):
        post_click(hwnd, lx, ly, scale)
        time.sleep(settle)
        raw, w, h, ox, oy, cw, ch, _ok = vr.grab_raw(hwnd)
        s = cw / LOGICAL_W
        res = (raw, w, h, ox, oy, s)
        if check(raw, w, h, ox, oy, s):
            return (True,) + res + (k,)
    return (False,) + res + (tries,)


def bar_pixel(raw, w, ox, oy, scale, idx):
    """nav_item 选中竖条上的那个像素（逻辑坐标 (24, NAV_Y[idx])）。"""
    sx = ox + int(round(BAR_X * scale))
    sy = oy + int(round(NAV_Y[idx] * scale))
    return vr.px(raw, w, sx, sy)


def point(raw, w, ox, oy, scale, lx, ly):
    sx = ox + int(round(lx * scale))
    sy = oy + int(round(ly * scale))
    return vr.px(raw, w, sx, sy)


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--calibrate", action="store_true", help="只打印实测值")
    ap.add_argument("--shots", action="store_true", help="给每页存一张 png")
    a = ap.parse_args()

    if not os.path.isfile(EXE):
        raise SystemExit("没找到 " + EXE + "（先编译）")

    proc = subprocess.Popen([EXE])
    rep = Report()
    try:
        hwnd = vr.find_window_by_pid(proc.pid, 10.0)
        if not hwnd:
            raise SystemExit("没找到窗口（进程 rc=%s）" % proc.poll())
        time.sleep(1.3)

        raw, w, h, ox, oy, cw, ch, ok = vr.grab_raw(hwnd)
        scale = cw / LOGICAL_W
        print("PrintWindow=%s 客户区=%dx%d 偏移=(%d,%d) scale=%.3f\n" % (ok, cw, ch, ox, oy, scale))

        if a.calibrate:
            for label, (lp, _want, _tol) in LIGHT.items():
                sx, sy = ox + int(round(lp[0] * scale)), oy + int(round(lp[1] * scale))
                print("%-16s (%4d,%4d) -> %s" % (label, lp[0], lp[1], vr.px(raw, w, sx, sy)))
            for idx, box, want, _min in MASS:
                vr.click_logical(hwnd, NAV_X, NAV_Y[idx], scale)
                time.sleep(0.55)
                raw, w, h, ox, oy, cw, ch, ok = vr.grab_raw(hwnd)
                m = color_mass(raw, w, h, ox, oy, scale, box, want)
                print("mass 页%d %s want=%s -> %d px" % (idx, box, want, m))
            return 0

        # ---- 1. 浅色主题结构断言 ----
        print("== 浅色主题：结构 ==")
        base_cells = content_cells(raw, w, h, ox, oy, scale)
        variety = len(set(c for c in base_cells if c))
        # 内容区绝大多数像素是面板白 + 文字抗锯齿灰，实测 30 上下。
        # 空窗只有 1~3 种颜色，所以 20 这个下限已经足以区分"画了"和"没画"。
        rep.step("内容区确实画了东西", variety >= 20, "采样格颜色种类 = %d (>=20)" % variety)
        if a.shots:
            save_png(raw, w, h, os.path.join(ROOT, "screenshots", "gallery-1.png"))
        for label, (lp, want, tol) in LIGHT.items():
            sx, sy = ox + int(round(lp[0] * scale)), oy + int(round(lp[1] * scale))
            got = vr.px(raw, w, sx, sy)
            rep.step("浅色 " + label, dist(got, want) <= tol,
                     "(%d,%d,%d) 期望 (%d,%d,%d)" % (got + want))

        # ---- 2. 逐页换页：结构 + 差分 ----
        print("\n== 浅色主题：逐个点导航项换页 ==")
        prev = base_cells
        prev_idx = 0
        for i in range(1, len(PAGES)):
            # 成功判据 = 选中竖条真的落到第 i 项上；不成立就重投（真人鼠标会捣乱）
            def bar_here(raw_, w_, h_, ox_, oy_, s_, idx=i):
                return bar_pixel(raw_, w_, ox_, oy_, s_, idx)[0] < 120
            ok, raw, w, h, ox, oy, scale, tries = click_until(
                hwnd, NAV_X, NAV_Y[i], scale, bar_here)
            cells = content_cells(raw, w, h, ox, oy, scale)

            on_ok = bar_pixel(raw, w, ox, oy, scale, i)[0] < 120        # 选中：primary 叠色，很暗
            off_ok = bar_pixel(raw, w, ox, oy, scale, prev_idx)[0] > 200  # 上一个：回到侧栏底色
            d = diff_cells(prev, cells)
            rep.step("第 %d 页 %s" % (i + 1, PAGES[i]),
                     on_ok and off_ok and d >= DIFF_MIN and ok,
                     "竖条 %s/%s 变化格 %d (>=%d) 点击%d次" % (
                         "跟随" if on_ok else "没跟", "复位" if off_ok else "没复位",
                         d, DIFF_MIN, tries))
            if a.shots:
                save_png(raw, w, h, os.path.join(ROOT, "screenshots", "gallery-%d.png" % (i + 1)))
            prev, prev_idx = cells, i

        # ---- 3. 色块质量断言（钉住"确实是那一页"）----
        print("\n== 浅色主题：色块质量 ==")
        for idx, box, want, need in MASS:
            def on_page(raw_, w_, h_, ox_, oy_, s_, idx=idx):
                return bar_pixel(raw_, w_, ox_, oy_, s_, idx)[0] < 120
            ok, raw, w, h, ox, oy, scale, tries = click_until(
                hwnd, NAV_X, NAV_Y[idx], scale, on_page)
            m = color_mass(raw, w, h, ox, oy, scale, box, want)
            rep.step("页%d(%s) 含 #%02X%02X%02X" % (idx + 1, PAGES[idx], *want),
                     m >= need, "实测 %d px（要求 >=%d）%s" % (
                         m, need, "" if ok else "  ⚠ 点击未确认"))

        # ---- 3.5 浮层页：打开 dialog，验证遮罩 + "正文不横向溢出面板" ----
        #   这是本轮修掉的那个 bug：正文一长就横着爬出面板，而且爬出去的部分落在
        #   半透明遮罩上，肉眼很难发现。这里用一组可计算的断言把它钉死：
        #     · 遮罩是整整一层的黑色 alpha=110 → 底层像素该变成 round(dst * 145/255)
        #     · 面板右侧那条带子上，开 dialog 前后必须严格满足上面这个关系
        #       （只要有一个字爬出去，那条带子上就会出现明显更暗的像素）
        print("\n== 浮层页：dialog 遮罩与正文裁剪 ==")
        def on_overlay(raw_, w_, h_, ox_, oy_, s_):
            return bar_pixel(raw_, w_, ox_, oy_, s_, 4)[0] < 120
        click_until(hwnd, NAV_X, NAV_Y[4], scale, on_overlay)
        raw0, w, h, ox, oy, cw, ch, _ = vr.grab_raw(hwnd)
        base = raw0
        scale = cw / LOGICAL_W

        def mask_on(raw_, w_, h_, ox_, oy_, s_):
            return dist(point(raw_, w_, ox_, oy_, s_, 1100, 400), (255, 255, 255)) > 60
        # 点左栏的「打开对话框」（面板 (262,127,236,547)，按钮在第一栏 y≈273..309）
        ok, raw, w, h, ox, oy, scale, tries = click_until(
            hwnd, 380, 291, scale, mask_on, tries=4, settle=0.5)
        rep.step("点「打开对话框」后遮罩出现", ok, "点击 %d 次" % tries)

        # 遮罩外的一个点：应当是底层像素被压暗到 145/255
        outside_before = point(base, w, ox, oy, scale, 1100, 400)
        outside_after = point(raw, w, ox, oy, scale, 1100, 400)
        expect_after = tuple(int(round(c * 145.0 / 255.0)) for c in outside_before)
        rep.step("遮罩 = 底层 × 145/255", dist(outside_after, expect_after) <= 4,
                 "实测 (%d,%d,%d) 期望 (%d,%d,%d)" % (outside_after + expect_after))

        # 面板内部必须是"没被遮罩"的干净底色
        inside = point(raw, w, ox, oy, scale, 560, 250)
        rep.step("面板本身不被遮罩", dist(inside, (255, 255, 255)) <= 2,
                 "面板内 (560,250) = (%d,%d,%d)" % inside)

        # 关键断言：面板右侧带上，开 dialog 前后必须满足"仅被压暗"的关系。
        # 注意带子必须从 x=845 起 —— 面板右边缘在 795，而 dialog 自己的 drop_shadow
        # 是 blur 22 + spread 8，会往外再糊 ~30px。把阴影算进"疑似溢出"会误报
        # （第一版取 x>=800，就误报了 150 个点、最大偏差 15）。
        bad = 0
        worst = 0
        n_pt = 0
        for yy in range(245, 476, 3):
            for xx in range(845, 1108, 3):
                n_pt += 1
                pb = point(base, w, ox, oy, scale, xx, yy)
                pa = point(raw, w, ox, oy, scale, xx, yy)
                e = tuple(int(round(c * 145.0 / 255.0)) for c in pb)
                d = dist(pa, e)
                if d > 8:
                    bad += 1
                    worst = max(worst, d)
        rep.step("正文没有横向溢出面板", bad == 0,
                 "面板右侧 %d 个采样点全部只被压暗（越界点 %d，最大偏差 %d）"
                 % (n_pt, bad, worst))

        if a.shots:
            save_png(raw, w, h, os.path.join(ROOT, "screenshots", "gallery-dialog.png"))

        # 用 Esc 关掉，免得影响后面的主题断言
        vr.user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT,
                                          wintypes.WPARAM, wintypes.LPARAM]
        vr.user32.PostMessageW(hwnd, 0x0100, wintypes.WPARAM(27), wintypes.LPARAM(0))
        time.sleep(0.5)
        raw2, w2, h2, ox2, oy2, cw2, ch2, _ = vr.grab_raw(hwnd)
        back_px = point(raw2, w2, ox2, oy2, cw2 / LOGICAL_W, 1100, 400)
        rep.step("Esc 关掉 dialog", dist(back_px, outside_before) <= 3,
                 "遮罩已消失（(%d,%d,%d) → (%d,%d,%d)）" % (outside_after + back_px))

        # ---- 4. 切深色：再跑一遍结构断言 ----
        print("\n== 深色主题 ==")
        light_side = LIGHT["sidebar_bg"][1]

        def became_dark(raw_, w_, h_, ox_, oy_, s_):
            return dist(point(raw_, w_, ox_, oy_, s_, 110, 430), light_side) > 100
        ok, raw, w, h, ox, oy, scale, tries = click_until(
            hwnd, THEME_BTN[0], THEME_BTN[1], scale, became_dark, tries=5, settle=0.5)

        # 确保停在第 1 页（点已经选中的项是幂等的，不会把页面切走）
        def first_page(raw_, w_, h_, ox_, oy_, s_):
            return bar_pixel(raw_, w_, ox_, oy_, s_, 0)[0] < 120
        ok2, raw, w, h, ox, oy, scale, tries2 = click_until(
            hwnd, NAV_X, NAV_Y[0], scale, first_page)
        if a.shots:
            save_png(raw, w, h, os.path.join(ROOT, "screenshots", "gallery-dark.png"))
        for label, (lp, want, tol) in DARK.items():
            got = point(raw, w, ox, oy, scale, lp[0], lp[1])
            rep.step("深色 " + label, dist(got, want) <= tol,
                     "(%d,%d,%d) 期望 (%d,%d,%d)" % (got + want))
        d_dark = diff_cells(base_cells, content_cells(raw, w, h, ox, oy, scale))
        rep.step("深色下内容区整体变了", d_dark >= DIFF_MIN,
                 "变化格 %d (>=%d)" % (d_dark, DIFF_MIN))

        # ---- 5. 切回浅色必须完全复原 ----
        print("\n== 切回浅色 ==")
        def became_light(raw_, w_, h_, ox_, oy_, s_):
            return dist(point(raw_, w_, ox_, oy_, s_, 110, 430), light_side) <= 2
        ok, raw, w, h, ox, oy, scale, tries = click_until(
            hwnd, THEME_BTN[0], THEME_BTN[1], scale, became_light, tries=5, settle=0.5)
        back = content_cells(raw, w, h, ox, oy, scale)
        d_back = diff_cells(base_cells, back)
        rep.step("浅色可逆（与初始一致）", d_back == 0, "差异格 = %d" % d_back)

        if a.shots:
            save_png(raw, w, h, os.path.join(ROOT, "screenshots", "gallery-light.png"))

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
