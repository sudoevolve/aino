"""原生画廊整窗验证：连续滚动、固定工具栏、搜索与主题联动。

离屏探针覆盖组件状态和精确几何；这里覆盖实际窗口、DPI 与消息链路。
截图差异只判断交互结果，不要求把现有原生皮肤换成参考网页的视觉样式。
"""

import argparse
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import verify_render as vr                      # noqa: E402

def find_exe():
    """优先使用显式路径，否则选择最近构建的常见落点。"""
    override = os.environ.get("AINO_GALLERY_EXE")
    if override:
        return os.path.abspath(override)
    candidates = [os.path.join(ROOT, "build", sub, "aino_gallery.exe")
                  for sub in ("", "Release", "Debug", "ssh-static")]
    existing = [path for path in candidates if os.path.isfile(path)]
    return max(existing, key=os.path.getmtime) if existing else candidates[0]


EXE = find_exe()

LOGICAL_W = 1180.0
LOGICAL_H = 760.0
NAV_W = 252.0        # 侧边栏宽
# 内容区顶部的逻辑高度，与 gallery_frame() 算出的页头一致（标题 40 + 说明 18 + 间距）。
# 版式一改这里就得跟，所以下面的按钮断言不再依赖它的具体数值。
TITLE_H = 94.0


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


def first_block(shot, test, x0, y0, x1, y1, step=2):
    """在区域内按行序找第一块满足 test 的实心色块，返回其中点（逻辑坐标）。

    主按钮的坐标会随版式移动，写死坐标的断言每次调间距就误报一次。这里改成
    「内容区里第一个强调色实心块就是主按钮」，然后向右 / 向下走到块的边界取中点。
    圆角按钮的中列是实心的，所以中点一定落在可点区域里。
    """
    ly = y0
    while ly < y1:
        lx = x0
        while lx < x1:
            if test(shot.at(lx, ly)):
                rx = lx
                while rx + step < x1 and test(shot.at(rx + step, ly)):
                    rx += step
                mid = lx + (rx - lx) // 2
                by = ly
                while by + step < y1 and test(shot.at(mid, by + step)):
                    by += step
                return ((lx + rx) // 2, (ly + by) // 2)
            lx += step
        ly += step
    return None


def is_tint_blue(c):
    return c[2] > 200 and c[0] < 60 and 100 < c[1] < 190


def is_tint_red(c):
    return c[0] > 200 and c[1] < 110 and c[2] < 110


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


# 性能采样复用相同的内容区网格。
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


def send_key(hwnd, vk):
    vr.user32.PostMessageW(hwnd, 0x0100, vk, 0)     # WM_KEYDOWN
    time.sleep(0.05)
    vr.user32.PostMessageW(hwnd, 0x0101, vk, 0)     # WM_KEYUP


def launch(extra_env=None, exe=None):
    """起一个 gallery，返回 (proc, hwnd)。"""
    env = dict(os.environ)
    if extra_env:
        env.update(extra_env)
    p = subprocess.Popen([exe or EXE], env=env)
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


# 导航位置与 gallery 的组头、分组间距和条目步长一致。
NAV_TOP = 80.0
NAV_STEP = 36.0

def nav_item_y(name):
    with open(os.path.join(ROOT, "examples", "gallery.cpp"), encoding="utf-8") as source:
        table = re.search(r"const Page kPages\[\] = \{(.*?)\n\};", source.read(), re.S)
    if table is None:
        raise ValueError("gallery catalog not found")
    y, previous_group = NAV_TOP, None
    for group, page in re.findall(r'\{\s*"([^"]*)"\s*,\s*"([^"]*)"', table.group(1)):
        if group != previous_group:
            if previous_group is not None:
                y += 14.0
            y += 24.0
            previous_group = group
        if page == name:
            return y + NAV_STEP / 2
        y += NAV_STEP
    raise ValueError("gallery section not found: " + name)

BUTTON_Y = nav_item_y("按钮")

def native_caption_dark(hwnd):
    query = vr.ctypes.WinDLL("dwmapi").DwmGetWindowAttribute
    query.argtypes = [vr.wintypes.HWND, vr.wintypes.DWORD, vr.ctypes.c_void_p, vr.wintypes.DWORD]
    query.restype = vr.wintypes.LONG
    value = vr.wintypes.BOOL()
    for attribute in (20, 19):
        if query(hwnd, attribute, vr.ctypes.byref(value), vr.ctypes.sizeof(value)) == 0:
            return bool(value.value)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shots", action="store_true")
    ap.add_argument("--exe", default=EXE, help="要验证的 aino_gallery.exe 路径")
    args = ap.parse_args()
    if not os.path.isfile(args.exe):
        ap.error("找不到 gallery 程序，请先构建或传入 --exe")
    rep = Report()
    proc, hwnd = launch({"AINO_DARK": "0", "AINO_START_PAGE": "0", "AINO_OPEN": ""}, args.exe)
    if not hwnd:
        stop(proc)
        print("[FAIL] gallery window did not start")
        return 2
    try:
        time.sleep(1.8)
        scale = client_scale(hwnd)
        def shot():
            time.sleep(.5)
            return Shot(hwnd, scale)
        def click(x, y):
            post_click(hwnd, x, y, scale)
            return shot()
        baseline = shot()
        rep.step("窗口可抓取", baseline.ok)
        rep.step("浅色窗口采用 F5F5F7 底色", close(baseline.at(1160, 180), (245, 245, 247), 1))
        rep.step("保留原有侧栏材质", 210 < baseline.avg(2, 80, 8, 600) < 245)
        primary = first_block(baseline, is_tint_blue, 290, TITLE_H + 20, 1150, 700)
        rep.step("原有蓝色主按钮正常绘制", primary is not None, str(primary))
        activated = click(primary[0], primary[1]) if primary else baseline
        rep.step("点击按钮显示右上角通知", activated.diff(baseline, 856, TITLE_H + 12, 1156, TITLE_H + 66) > 100)
        # 关闭通知，后续断言不依赖动画结束时刻。
        click(1140, 110)
        inputs = click(100, nav_item_y("输入框"))
        rep.step("侧栏入口滚动到输入框分类", inputs.diff(baseline, 278, 68, 1140, 690) > 1000)
        rep.step("分类跳转保留固定顶部工具栏", inputs.diff(baseline, 270, 8, 880, 48) < 15)
        dark = click(96, 648)
        light_luma = inputs.avg(270, 70, 1150, 700)
        dark_luma = dark.avg(270, 70, 1150, 700)
        rep.step("底部外观控件切换原生深色主题", light_luma - dark_luma > 130,
                 "%.1f -> %.1f" % (light_luma, dark_luma))
        header_color = dark.at(760, 30)
        rep.step("深色顶部工具栏与窗口底色一致", max(abs(c - 30) for c in header_color) <= 1,
                 str(header_color))
        native_dark = native_caption_dark(hwnd)
        if native_dark is not None:
            rep.step("系统标题栏同步深色模式", native_dark)
        light = click(146, 648)
        rep.step("浅色主题可恢复", light.avg(270, 70, 1150, 700) > 220)
        rep.step("顶部工具栏随浅色模式恢复", light.avg(580, 8, 650, 48) > 220)
        native_light = native_caption_dark(hwnd)
        if native_light is not None:
            rep.step("系统标题栏同步浅色模式", not native_light)
        red = click(138, 708)
        nav_tint = red.at(19, nav_item_y("输入框"))
        rep.step("强调色更新侧栏选中态", nav_tint[0] > nav_tint[1] + 20, str(nav_tint))
        red_buttons = click(100, nav_item_y("按钮"))
        # 同一个按钮、换成了红色：位置应当和蓝色那次重合（±30 逻辑像素），
        # 否则说明「强调色只作用于侧栏、没进控件」。
        red_block = first_block(red_buttons, is_tint_red, 290, TITLE_H + 20, 1150, 700)
        same = (red_block is not None and primary is not None and
                abs(red_block[0] - primary[0]) <= 30 and abs(red_block[1] - primary[1]) <= 30)
        rep.step("切换分类后保留所选强调色", same, "%s -> %s" % (primary, red_block))
        post_click(hwnd, 1040, 36, scale)
        for char in "password":
            vr.user32.PostMessageW(hwnd, 0x0102, ord(char), 0)
            time.sleep(.025)
        filtered = shot()
        rep.step("顶部搜索实际筛选组件内容", filtered.diff(red_buttons, 278, 80, 1140, 690) > 500)
        restored = click(1110, 36)
        rep.step("搜索清除按钮恢复目录", restored.diff(filtered, 278, 80, 1140, 690) > 500)
    finally:
        stop(proc)
    proc, hwnd = launch({"AINO_START_PAGE": "feedback", "AINO_OPEN": "dialog", "AINO_DARK": "0"}, args.exe)
    if not hwnd:
        stop(proc)
        print("[FAIL] dialog fixture did not start")
        return 2
    try:
        time.sleep(1.8)
        scale = client_scale(hwnd)
        opened = Shot(hwnd, scale)
        rep.step("原生对话框不额外添加全屏暗色遮罩", opened.avg(4, 80, 10, 600) > 210)
        # 面板内的取消按钮，来自原生 alert_dialog 的底部布局。
        send_key(hwnd, 0x1B)
        time.sleep(.6)
        closed = Shot(hwnd, scale)
        rep.step("Escape 关闭实际对话框", opened.diff(closed, 340, 220, 840, 520) > 100)
        if args.shots:
            save_png(opened.raw, opened.w, opened.h, os.path.join(ROOT, "screenshots", "gallery-dialog.png"))
    finally:
        stop(proc)
    if args.shots and not rep.failed:
        for name, page, dark in (("gallery-overview.png", "0", "0"),
                                 ("gallery-button.png", "cards", "0"),
                                 ("gallery-dark.png", "advanced", "1")):
            proc, hwnd = launch({"AINO_START_PAGE": page, "AINO_DARK": dark, "AINO_OPEN": ""}, args.exe)
            try:
                rep.step("预览窗口启动：" + name, bool(hwnd))
                if hwnd:
                    time.sleep(1.8)
                    frame = Shot(hwnd, client_scale(hwnd))
                    save_png(frame.raw, frame.w, frame.h, os.path.join(ROOT, "screenshots", name))
            finally:
                stop(proc)
    print("\n===== 共 %d 项，失败 %d 项 =====" % (len(rep.results), rep.failed))
    return 1 if rep.failed else 0


if __name__ == "__main__":
    sys.exit(main())
