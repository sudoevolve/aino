"""GUI 渲染的【像素级】验证 —— 用采样颜色断言，代替"截图看一眼"。

为什么需要它：
  · 截图能抓下来，不等于画对了。窗口空白、全黑、缺控件，截图都照样能存成功。
  · 在多模态不可用 / 图太大看不清 / 只想在 CI 里跑的情况下，"看图"根本不可用。
  · 把预期色值写下来做断言，才是可复现的证据。

顺带解决三个必然踩的坑：
  · 模拟点击【不要用真实光标】(SetCursorPos + mouse_event)：SetForegroundWindow 常被系统
    拒绝，而且真实用户的鼠标会立刻把光标抢走 —— 于是点到别人窗口上，现象是"点击没反应/
    断言全失败"，看着像渲染坏了。改用 PostMessage 投递 WM_LBUTTONDOWN/UP（坐标放 lParam）。
  · 断言点【别选在文字上】：居中文字正好压在控件中心，采到的是抗锯齿过渡色（曾实测到
    (191,191,192)，怀疑了很久）。选控件内部但避开文字的位置，比如偏下 6~8px。
  · 客户区在整窗位图里的偏移不能靠猜边框宽度（Win11 标题栏也是浅色，靠颜色找边界会翻车），
    要用 ClientToScreen 精确算。

用法:
  python verify_render.py <exe> [选项]

选项:
  --out <png>                 存一张整窗截图（便于留档）
  --logical <W>               逻辑客户区宽度；坐标断言需要它来算 DPI 缩放（如 940）
  --click <LX,LY>             抓图前点一下这个逻辑坐标（可重复；用于切主题/点按钮）
  --expect <LX,LY,RRGGBB[,TOL]>
                              断言：逻辑坐标 (LX,LY) 处的像素应等于 RRGGBB，容差默认 6
                              （可重复；坐标是【逻辑客户区坐标】，脚本自己换物理像素）
  --wait <秒>                 抓图前等待，默认 1.0
  --title-wait <秒>           等窗口出现的上限，默认 10

退出码：全部断言通过 0，有失败 1。

例（浅色主题断言背景是白的、主按钮是近黑）:
  python verify_render.py app.exe --logical 940 \
      --expect "20,200,FFFFFF" --expect "57,196,18181B,6" --out shot.png

例（先点右上角主题按钮切深色，再断言底色变近黑）:
  python verify_render.py app.exe --logical 940 --click 890,48 \
      --expect "20,200,09090B" --out dark.png
"""

import argparse
import ctypes
import subprocess
import sys
import time
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

# 抓图进程必须先 DPI 感知，否则 150% 缩放下 GetWindowRect 返回虚拟化坐标，截图被裁
for _mode in ("PerMonitorV2", "System"):
    try:
        if _mode == "PerMonitorV2":
            user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
        else:
            user32.SetProcessDPIAware()
        DPI_MODE = _mode
        break
    except Exception:
        continue
else:
    DPI_MODE = "none"

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
user32.PrintWindow.restype = wintypes.BOOL
user32.GetWindowDC.argtypes = [wintypes.HWND]
user32.GetWindowDC.restype = wintypes.HDC
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
gdi32.CreateCompatibleDC.restype = wintypes.HDC
gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.CreateCompatibleBitmap.restype = wintypes.HBITMAP
# HDC/HBITMAP 在 64 位下是指针，不声明 argtypes 会 OverflowError: int too long to convert
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.SelectObject.restype = wintypes.HGDIOBJ
gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]
gdi32.DeleteObject.restype = wintypes.BOOL
gdi32.DeleteDC.argtypes = [wintypes.HDC]
gdi32.DeleteDC.restype = wintypes.BOOL


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG), ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wintypes.DWORD * 3)]


def find_window_by_pid(pid, timeout=10.0, min_width=100):
    """按 PID 找窗口 —— 不依赖标题（中文标题经 shell 传递容易乱码匹配不上）"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        found = []

        def cb(hwnd, _):
            wpid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
            if wpid.value == pid and user32.IsWindowVisible(hwnd):
                r = wintypes.RECT()
                user32.GetWindowRect(hwnd, ctypes.byref(r))
                if r.right - r.left > min_width:
                    found.append(hwnd)
            return True

        user32.EnumWindows(WNDENUMPROC(cb), 0)
        if found:
            return found[0]
        time.sleep(0.25)
    return None


def grab_raw(hwnd):
    """整窗 PrintWindow → (raw BGRA bytes, w, h, ox, oy, client_w, client_h)

    ox/oy = 客户区左上角在整窗位图里的偏移（用 ClientToScreen 算，不猜边框）
    """
    wr, cr = wintypes.RECT(), wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(wr))
    user32.GetClientRect(hwnd, ctypes.byref(cr))
    w, h = wr.right - wr.left, wr.bottom - wr.top

    pt = wintypes.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    ox, oy = pt.x - wr.left, pt.y - wr.top

    hdc_win = user32.GetWindowDC(hwnd)
    hdc_mem = gdi32.CreateCompatibleDC(hdc_win)
    hbmp = gdi32.CreateCompatibleBitmap(hdc_win, w, h)
    old = gdi32.SelectObject(hdc_mem, hbmp)
    # PW_RENDERFULLCONTENT=2：自绘窗口必须走这条，否则可能抓到空白
    ok = user32.PrintWindow(hwnd, hdc_mem, 2)

    bmi = BITMAPINFO()
    bmi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmi.bmiHeader.biWidth = w
    bmi.bmiHeader.biHeight = -h          # 负 = 自上而下
    bmi.bmiHeader.biPlanes = 1
    bmi.bmiHeader.biBitCount = 32
    bmi.bmiHeader.biCompression = 0
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits.argtypes = [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT,
                                ctypes.c_void_p, ctypes.POINTER(BITMAPINFO), wintypes.UINT]
    gdi32.GetDIBits.restype = ctypes.c_int
    got = gdi32.GetDIBits(hdc_mem, hbmp, 0, h, buf, ctypes.byref(bmi), 0)

    gdi32.SelectObject(hdc_mem, old)
    gdi32.DeleteObject(hbmp)
    gdi32.DeleteDC(hdc_mem)
    user32.ReleaseDC(hwnd, hdc_win)
    if not got:
        raise SystemExit("GetDIBits 失败")

    return buf.raw, w, h, ox, oy, cr.right, cr.bottom, bool(ok)


def px(raw, w, x, y):
    i = (y * w + x) * 4
    return raw[i + 2], raw[i + 1], raw[i]      # BGRA → (r,g,b)


def click_logical(hwnd, lx, ly, scale):
    """向窗口直接投递鼠标消息（不动真实光标、不抢焦点）。

    ⚠️ 别用 SetCursorPos + mouse_event —— 实测在真实机器上会稳定失败：
      · SetForegroundWindow 常返回 0（系统拒绝非前台进程抢焦点），前台仍是别的窗口；
      · SetCursorPos 之后光标会立刻被真实用户抢回去（诊断时目标 (1536,307)，
        实际停在 (1254,936)）。
    结果是点击落在别人的窗口上，现象是"点击没反应 / 断言全失败"，看着像渲染坏了。

    PostMessage 把坐标放在 lParam 里，只要目标窗口用 GET_X_LPARAM 之类读消息坐标
    （Win32 的常规写法）就能点中，而且完全不打扰用户、不移动光标、不要求窗口在前台。
    前提：若目标窗口自己用 GetCursorPos 取坐标，这个方法无效，只能退回真实光标。

    lParam 里是【物理客户区坐标】= 逻辑坐标 × scale。
    """
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT,
                                    wintypes.WPARAM, wintypes.LPARAM]
    x = int(round(lx * scale))
    y = int(round(ly * scale))
    lp = wintypes.LPARAM(((y & 0xFFFF) << 16) | (x & 0xFFFF))
    user32.PostMessageW(hwnd, 0x0200, wintypes.WPARAM(0), lp)   # WM_MOUSEMOVE
    time.sleep(0.05)
    user32.PostMessageW(hwnd, 0x0201, wintypes.WPARAM(1), lp)   # WM_LBUTTONDOWN
    time.sleep(0.05)
    user32.PostMessageW(hwnd, 0x0202, wintypes.WPARAM(0), lp)   # WM_LBUTTONUP
    return x, y


def parse_color(s):
    r, g, b = int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16)
    return r, g, b


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("--out")
    ap.add_argument("--logical", type=float, default=0.0,
                    help="逻辑客户区宽度，用于算 DPI 缩放（坐标断言必需）")
    ap.add_argument("--click", action="append", default=[])
    ap.add_argument("--expect", action="append", default=[])
    ap.add_argument("--wait", type=float, default=1.0)
    ap.add_argument("--title-wait", type=float, default=10.0)
    a = ap.parse_args()

    proc = subprocess.Popen([a.exe])
    try:
        hwnd = find_window_by_pid(proc.pid, a.title_wait)
        if not hwnd:
            rc = proc.poll()
            raise SystemExit("没找到窗口" + ("（进程已退出 rc=%s）" % rc if rc is not None else ""))

        time.sleep(a.wait)
        raw, w, h, ox, oy, cw, ch, ok = grab_raw(hwnd)
        scale = (cw / a.logical) if a.logical else 1.0
        print("DPI=%s hwnd=%s PrintWindow=%s 整窗=%dx%d 客户区=%dx%d 偏移=(%d,%d) scale=%.3f"
              % (DPI_MODE, hwnd, ok, w, h, cw, ch, ox, oy, scale))

        # 空白检测：整幅图颜色种类太少，说明根本没画东西
        seen = set()
        for y in range(oy, min(h, oy + 400), 3):
            for x in range(ox, min(w, ox + 900), 3):
                seen.add(px(raw, w, x, y))
        print("客户区采样颜色种类 = %d（几十以上才说明确实画了东西）" % len(seen))

        if a.click:
            user32.SetForegroundWindow(hwnd)
            time.sleep(0.3)
            for c in a.click:
                lx, ly = (float(v) for v in c.split(","))
                sx, sy = click_logical(hwnd, lx, ly, scale)
                print("已点击逻辑(%g,%g) → 屏幕(%d,%d)" % (lx, ly, sx, sy))
            time.sleep(a.wait)
            raw, w, h, ox, oy, cw, ch, ok = grab_raw(hwnd)

        fails = 0
        if a.expect:
            print("\n%-24s %-16s %-16s %s" % ("断言（逻辑坐标）", "实测 RGB", "预期 RGB", "判定"))
            print("-" * 68)
            for e in a.expect:
                parts = e.split(",")
                if len(parts) < 3:
                    print("  跳过无法解析的 --expect: %s" % e)
                    fails += 1
                    continue
                lx, ly = float(parts[0]), float(parts[1])
                want = parse_color(parts[2])
                tol = int(parts[3]) if len(parts) > 3 else 6
                sx = ox + int(round(lx * scale))
                sy = oy + int(round(ly * scale))
                if not (0 <= sx < w and 0 <= sy < h):
                    print("%-24s %-16s %-16s ✗ 越界" % ("(%g,%g)" % (lx, ly), "-", "-"))
                    fails += 1
                    continue
                got = px(raw, w, sx, sy)
                d = max(abs(got[0] - want[0]), abs(got[1] - want[1]), abs(got[2] - want[2]))
                bad = d > tol
                fails += bad
                print("%-24s %-16s %-16s %s" % (
                    "(%g,%g)" % (lx, ly),
                    "(%3d,%3d,%3d)" % got, "(%3d,%3d,%3d)" % want,
                    "✗ 偏差%d" % d if bad else "✓"))

        if a.out:
            from PIL import Image
            Image.frombuffer("RGB", (w, h), raw, "raw", "BGRX", 0, 1).save(a.out)
            print("\n已保存 %s" % a.out)

        if a.expect:
            print("结论：%d 项通过 / %d 项失败" % (len(a.expect) - fails, fails))
        return 1 if fails else 0
    finally:
        time.sleep(0.3)
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
