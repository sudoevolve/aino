"""shadcn_demo 的现场验证 —— 菜单栏确实画出来了、点得开、点完标题跟着变。

widget_probe 已经用离屏驱动证明过"点下去改没改值"。这个脚本补的是另一半：
**在真的窗口里**，那 34px 高的菜单栏是不是真占了地方，下拉是不是真弹在
菜单栏下方，点完之后标题行有没有跟着变。

为什么还要单独做一遍：离屏探针里 impl->canvas 是我们自己塞的，没有
WM_PAINT、没有 DPI 缩放、没有 BitBlt —— 那些路径只有真窗口才走到。

为什么用"区域差异像素数"而不是单点取色：浅色主题下 popover 底色 (#FFFFFF)
和页面底色一模一样，单点采样根本分不出"面板开了"和"什么都没发生"。
数差异像素既能证明"画了东西"，又能证明"画在了该在的地方"。

用法:
  python verify_shadcn.py            # 断言
  python verify_shadcn.py --shot     # 顺带存 screenshots/shadcn-demo.png
"""

import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import verify_render as vr                      # noqa: E402
from verify_gallery import save_png             # noqa: E402

EXE = os.path.join(ROOT, "build", "aino_shadcn_demo.exe")

LOGICAL_W = 780.0

g_pass = 0
g_fail = 0


def check(ok, what, detail=""):
    global g_pass, g_fail
    if ok:
        g_pass += 1
        print("  [ ok ] %s" % what)
    else:
        g_fail += 1
        print("  [FAIL] %s  %s" % (what, detail))


def close_rgb(c1, c2, tol):
    return all(abs(a - b) <= tol for a, b in zip(c1[:3], c2[:3]))


class Shot:
    """一帧整窗位图 + 客户区原点，坐标换算都收在这里。"""

    def __init__(self, hwnd, scale):
        self.raw, self.w, self.h, self.ox, self.oy, self.cw, self.ch, self.ok = vr.grab_raw(hwnd)
        self.scale = scale

    def at(self, lx, ly):
        return vr.px(self.raw, self.w,
                     self.ox + int(round(lx * self.scale)),
                     self.oy + int(round(ly * self.scale)))

    def diff(self, other, lx0, ly0, lx1, ly1, tol=8):
        """两块图在逻辑矩形内有多少像素"看起来不一样"。"""
        s = self.scale
        n = 0
        y0 = self.oy + int(round(ly0 * s))
        y1 = self.oy + int(round(ly1 * s))
        x0 = self.ox + int(round(lx0 * s))
        x1 = self.ox + int(round(lx1 * s))
        for y in range(y0, y1):
            for x in range(x0, x1):
                if not close_rgb(vr.px(self.raw, self.w, x, y),
                                 vr.px(other.raw, other.w, x, y), tol):
                    n += 1
        return n


def main():
    if not os.path.exists(EXE):
        print("找不到 %s，先跑 tools/build.py" % EXE)
        return 1

    proc = subprocess.Popen([EXE])
    try:
        hwnd = vr.find_window_by_pid(proc.pid)
        if not hwnd:
            print("开不了窗口")
            return 1

        time.sleep(2.5)                     # 预热：字体、首帧缓冲都就位再抓

        r = vr.wintypes.RECT()
        vr.user32.GetClientRect(hwnd, vr.ctypes.byref(r))
        scale = r.right / LOGICAL_W
        print("客户区 %dx%d  scale=%.3f" % (r.right, r.bottom, scale))

        s0 = Shot(hwnd, scale)

        # ---- 1. 菜单栏真的占了 34 个逻辑像素 ----
        # 底边线画在 y = 33.5，落在像素 33 上，颜色是 th.border (#E4E4E7)
        line = s0.at(300.0, 33.0)
        check(close_rgb(line, (228, 228, 231), 10), "菜单栏底边线在 y≈33（th.border）",
              "got=%s" % (line,))

        # 菜单栏内部是 Ghost（透明）→ 就是窗口背景，不是一条灰带
        inside = s0.at(770.0, 16.0)
        check(close_rgb(inside, (255, 255, 255), 8), "菜单栏内部是透明底（Ghost）",
              "got=%s" % (inside,))

        # ---- 2. 点「文件」→ 菜单栏下方真的多出一块 ----
        # 顶级项从 box.x+4 起，每项宽 = 文字宽 + 24；「文件」中心约 x=30
        vr.click_logical(hwnd, 30.0, 17.0, scale)
        time.sleep(0.5)
        s1 = Shot(hwnd, scale)
        below = s0.diff(s1, 0.0, 36.0, 180.0, 200.0)
        check(below > 400, "点「文件」→ 菜单栏下方出现下拉面板",
              "差异像素=%d（阈值 400）" % below)

        # 菜单栏以上不该多出什么（除了触发器高亮那一小块）
        above = s0.diff(s1, 0.0, 0.0, 180.0, 31.0)
        check(above < 4000, "变化集中在菜单栏下方，没有整条糊开",
              "差异像素=%d（上限 4000）" % above)

        # ---- 3. 点「文件 → 新建」→ 标题行跟着变（回调真的拿到了选择） ----
        # 实测：「文件」的下拉面板左边框在 x≈4，条目从 y=41 起、每条 34 高，
        # 所以「新建」中心 y≈58。x 取 40 落在文字右侧的空白里。
        #
        # 注意这里必须点**面板里的条目**，不能点隔壁的「视图」来"换个面板看看"：
        # click_logical 会先发 WM_MOUSEMOVE，那一帧 hover 就把面板切过去了，
        # 紧接着的 DOWN/UP 落在"已经是当前项"的触发器上 —— 按桌面惯例这就是
        # "再点一下收起"，于是什么都不会发生。想验证切换请走 widget_probe
        # （离屏驱动拿得到 hover），真窗口这边只验证"画出来了 + 选中回调到了"。
        vr.click_logical(hwnd, 40.0, 58.0, scale)
        time.sleep(0.5)
        s2 = Shot(hwnd, scale)
        title = s1.diff(s2, 22.0, 84.0, 600.0, 106.0)
        check(title > 20, "点「文件 → 新建」→ 标题行文字变了",
              "差异像素=%d（阈值 20）" % title)
        # 面板该收起来了：菜单栏下方恢复原样
        gone = s0.diff(s2, 0.0, 36.0, 180.0, 200.0)
        check(gone < 200, "选中后下拉收起，下方的画面恢复原样",
              "与初始状态差异=%d（上限 200）" % gone)

        # ---- 4. 换个顶级菜单 → 面板出现在另一个横向位置 ----
        # 「编辑」是第 2 项，中心约 x=82
        vr.click_logical(hwnd, 82.0, 17.0, scale)
        time.sleep(0.5)
        s3 = Shot(hwnd, scale)
        again = s0.diff(s3, 0.0, 36.0, 260.0, 200.0)
        check(again > 400, "点「编辑」→ 又弹出一个面板",
              "差异像素=%d（阈值 400）" % again)

        if "--shot" in sys.argv:
            save_png(s3.raw, s3.w, s3.h, os.path.join(ROOT, "screenshots", "shadcn-demo.png"))
            print("  已存 screenshots/shadcn-demo.png")

        print("=== 通过 %d 项，失败 %d 项 ===" % (g_pass, g_fail))
        return 0 if g_fail == 0 else 1
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
