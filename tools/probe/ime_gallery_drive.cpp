// 通用小工具：给**另一个进程**里的 aino 窗口（gallery）做「切页 / 点击 / 打字 / 抓屏」，
// 用来在真实场景里复现输入法问题（我的小探针和 gallery 的布局不同，有时复现不出来）。
//
// 用法（逻辑坐标，脚本自己按 DPI 换算成物理像素）：
//   ime_gallery_drive.exe --nav 1 --sleep 400 --shot page.bmp
//   ime_gallery_drive.exe --click 120 60 --keys 5 --sleep 300 --shot typing.bmp
// 动作按命令行顺序执行。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cstdio>
#include <cstring>
#include <string>

static const wchar_t* kTarget = L"aino gallery";
static HWND g_hwnd = nullptr;
static float g_sc = 1.0f;

static bool attach() {
    g_hwnd = FindWindowW(nullptr, kTarget);
    if (!g_hwnd) { std::printf("[err] 找不到 \"aino gallery\"\n"); return false; }
    RECT cr{};
    GetClientRect(g_hwnd, &cr);
    g_sc = 1.0f;                               // 后面按 dpi 覆盖
    SetForegroundWindow(g_hwnd);
    SetFocus(g_hwnd);
    POINT org{0, 0};
    ClientToScreen(g_hwnd, &org);
    // ⚠️ 本工具**没有**开 DPI 感知，GetClientRect / ClientToScreen 拿到的是"虚拟化"
    // 坐标（96 dpi 空间）；目标窗口如果是 150% 感知的，真实物理客户区要大 1.5 倍。
    // 直接问目标窗口的 DPI，把换算关系打出来，别再靠猜。
    typedef UINT (WINAPI* GetDpiForWindowFn)(HWND);
    UINT dpi = 96;
    if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
        auto fn = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(u, "GetDpiForWindow"));
        if (fn) dpi = fn(g_hwnd);
    }
    // 实测：WM_LBUTTONDOWN 里的坐标按**客户区像素**解释，而 aino 的布局也是客户区
    // 像素（窗口没开 DPI 感知时系统整体放大，客户区仍是 1120x720），所以这里不换算。
    // GetDpiForWindow 只用来提示"此显示器不是 100%"，别拿它去乘坐标 —— 会点偏。
    g_sc = 1.0f;
    std::printf("[ok] hwnd=%p 虚拟客户区=%ldx%ld 客户区原点(虚拟屏幕)=%d,%d dpi=%u scale=%.2f\n",
                (void*)g_hwnd, cr.right, cr.bottom, org.x, org.y, dpi, g_sc);
    return true;
}

static void click_screen(int sx, int sy) {
    POINT p{sx, sy};
    ScreenToClient(g_hwnd, &p);
    const LPARAM pt = MAKELPARAM(p.x, p.y);
    PostMessageW(g_hwnd, WM_MOUSEMOVE, 0, pt);
    Sleep(40);
    PostMessageW(g_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pt);
    Sleep(40);
    PostMessageW(g_hwnd, WM_LBUTTONUP, 0, pt);
    std::printf("[sclick] %d,%d\n", sx, sy);
}

static void click_logical(float x, float y) {
    const LPARAM pt = MAKELPARAM(int(x * g_sc), int(y * g_sc));
    PostMessageW(g_hwnd, WM_MOUSEMOVE, 0, pt);
    Sleep(40);
    PostMessageW(g_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pt);
    Sleep(40);
    PostMessageW(g_hwnd, WM_LBUTTONUP, 0, pt);
    std::printf("[click] %.0f,%.0f\n", x, y);
}

static void type_key(WORD vk) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    SendInput(1, &in, sizeof in);
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof in);
}

static HKL find_zh() {
    HKL list[128];
    int n = GetKeyboardLayoutList(128, list);
    for (int i = 0; i < n; ++i) {
        DWORD lang = DWORD(0xFFFF & (DWORD_PTR)list[i]);
        if ((lang & 0x3FF) == 0x04) return list[i];
    }
    return nullptr;
}

static void shot(const char* path, RECT r) {
    const int x = r.left - 8, y = r.top - 8;
    const int w = (r.right - r.left) + 16, h = (r.bottom - r.top) + 150;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY);
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih = bi.bmiHeader;
    ih.biHeight = -h;
    const DWORD px = DWORD(w * h * 4);
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + px;
    std::FILE* f = std::fopen(path, "wb");
    if (f) {
        std::fwrite(&fh, sizeof fh, 1, f);
        std::fwrite(&ih, sizeof ih, 1, f);
        std::fwrite(bits, px, 1, f);
        std::fclose(f);
        std::printf("[shot] %s (%dx%d, 原点 %d,%d)\n", path, w, h, x, y);
    }
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

int main(int argc, char** argv) {
    if (!attach()) return 1;
    if (HKL zh = find_zh()) {
        SendMessageW(g_hwnd, WM_INPUTLANGCHANGEREQUEST, 0, LPARAM(zh));
        Sleep(400);
    }
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--nav") == 0 && i + 1 < argc) {
            const int idx = std::atoi(argv[++i]);
            click_logical(150.0f, float(119 + 37 * idx));
            Sleep(350);
        } else if (std::strcmp(argv[i], "--click") == 0 && i + 2 < argc) {
            const float x = float(std::atof(argv[++i]));
            const float y = float(std::atof(argv[++i]));
            click_logical(x, y);
            Sleep(250);
        } else if (std::strcmp(argv[i], "--sclick") == 0 && i + 2 < argc) {
            const int sx = std::atoi(argv[++i]);   // 别写成 click(atoi(++i), atoi(++i))：
            const int sy = std::atoi(argv[++i]);   // 实参求值顺序未定义，会读反
            click_screen(sx, sy);
            Sleep(250);
        } else if (std::strcmp(argv[i], "--keys") == 0 && i + 1 < argc) {
            const int n = std::atoi(argv[++i]);
            SetForegroundWindow(g_hwnd);
            SetFocus(g_hwnd);
            for (int k = 0; k < n; ++k) { type_key('A'); Sleep(160); }
            std::printf("[keys] %d\n", n);
        } else if (std::strcmp(argv[i], "--sleep") == 0 && i + 1 < argc) {
            Sleep(DWORD(std::atoi(argv[++i])));
        } else if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            RECT r{};
            GetWindowRect(g_hwnd, &r);
            shot(argv[++i], r);
        }
    }
    return 0;
}
