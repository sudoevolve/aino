// 诊断：输入框下面那个浮动组合窗（"MSCTFIME Composition"）到底是怎么露出来的。
//
// 两个阶段，都按帧抓图逐像素比，不靠 IsWindowVisible 猜：
//   阶段 A（打字中）：200ms 一个键，16ms 抓一帧，看每次按键会不会亮出一份。
//   阶段 B（空闲）：  打完停手，连抓 240 帧（~8ms/帧），统计小窗区域每帧是
//                    "有" 还是 "无"。如果空闲时也在 0/1/0/1 地翻，那就是
//                    库里每 20ms 的 SW_HIDE 和输入法的 ShowWindow 在拉锯 ——
//                    这就是用户说的"一直闪烁"。
//
// 用法（手动跑，不进 regress：会抢键盘、切输入法）：
//   python tools/cl_one.py tools/probe/ime_flicker_probe.cpp build/ime_flicker_probe.exe \
//          user32.lib gdi32.lib msimg32.lib imm32.lib
//   ./build/ime_flicker_probe.exe
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <string>
#include <vector>

static std::atomic<HWND> g_hwnd{nullptr};
static std::atomic<bool> g_stop{false};
static const wchar_t* kTitle = L"ime flicker";

struct Frame {
    int w = 0, h = 0;
    std::vector<unsigned char> px;   // BGRA, 自上而下
};

static void grab(Frame& f, int x, int y, int w, int h) {
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
    f.w = w; f.h = h;
    f.px.assign(static_cast<unsigned char*>(bits),
                static_cast<unsigned char*>(bits) + size_t(w) * h * 4);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

static void save_bmp(const char* path, const Frame& f) {
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof ih;
    ih.biWidth = f.w;
    ih.biHeight = -f.h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    const DWORD px = DWORD(f.px.size());
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + px;
    std::FILE* fp = std::fopen(path, "wb");
    if (!fp) return;
    std::fwrite(&fh, sizeof fh, 1, fp);
    std::fwrite(&ih, sizeof ih, 1, fp);
    std::fwrite(f.px.data(), px, 1, fp);
    std::fclose(fp);
}

// 只在 [y0,y1) 这条带子里比，返回差异像素数；bbox 写回（相对整帧）
static int diff_band(const Frame& a, const Frame& b, int y0, int y1,
                     int* bx, int* by, int* bw, int* bh) {
    int n = 0, x0 = 1 << 30, yy0 = 1 << 30, x1 = -1, y1o = -1;
    for (int y = y0; y < y1 && y < a.h; ++y) {
        const unsigned char* pa = &a.px[size_t(y) * a.w * 4];
        const unsigned char* pb = &b.px[size_t(y) * b.w * 4];
        for (int x = 0; x < a.w; ++x) {
            const int d = std::abs(int(pa[x * 4]) - int(pb[x * 4]))
                        + std::abs(int(pa[x * 4 + 1]) - int(pb[x * 4 + 1]))
                        + std::abs(int(pa[x * 4 + 2]) - int(pb[x * 4 + 2]));
            if (d > 24) {
                ++n;
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < yy0) yy0 = y;
                if (y > y1o) y1o = y;
            }
        }
    }
    if (bx) { *bx = x1 >= 0 ? x0 : 0; *by = y1o >= 0 ? yy0 : 0;
              *bw = x1 >= 0 ? x1 - x0 + 1 : 0; *bh = y1o >= 0 ? y1o - yy0 + 1 : 0; }
    return n;
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

static void type_key(WORD vk) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    SendInput(1, &in, sizeof in);
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof in);
}

int main() {
    aino::Window win(560, 340, "ime flicker");
    if (!win.ok()) { std::printf("[fatal] window\n"); return 1; }

    std::string name;

    std::thread([&] {
        Sleep(1500);
        HWND h = g_hwnd.load();
        SetForegroundWindow(h);
        SetFocus(h);
        Sleep(300);
        if (HKL zh = find_zh()) SendMessageW(h, WM_INPUTLANGCHANGEREQUEST, 0, LPARAM(zh));
        Sleep(800);
        RECT cr{};
        GetClientRect(h, &cr);
        const float sc = float(cr.right) / 560.0f;
        LPARAM pt = MAKELPARAM(int(120 * sc), int(46 * sc));
        PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, pt);
        PostMessageW(h, WM_LBUTTONUP, 0, pt);
        Sleep(400);

        RECT r{};
        GetWindowRect(h, &r);
        const int gx = r.left - 6, gy = r.top - 6;
        const int gw = (r.right - r.left) + 12, gh = (r.bottom - r.top) + 170;
        // 小窗就贴在输入框下沿（客户区 y≈78 处），候选窗在更下面
        const int band_y0 = int(76 * sc) + 12;
        const int band_y1 = band_y0 + int(34 * sc);

        std::printf("---- A: 打字中 ----\n");
        std::atomic<bool> typing{true};
        std::thread typer([&] {
            for (int i = 0; i < 4 && typing.load(); ++i) { type_key('S'); Sleep(400); }
        });
        {
            Frame prev, base;
            grab(prev, gx, gy, gw, gh);
            base = prev;
            save_bmp("cap_base.bmp", base);
            for (int i = 0; i < 200 && typing.load(); ++i) {
                Frame f;
                grab(f, gx, gy, gw, gh);
                int bx = 0, by = 0, bw = 0, bh = 0;
                const int n = diff_band(prev, f, band_y0, band_y1, &bx, &by, &bw, &bh);
                if (n > 25) {
                    std::printf("[A %03d] diff=%-5d bbox=%d,%d %dx%d\n", i, n, bx, by, bw, bh);
                    char path[64];
                    std::snprintf(path, sizeof path, "capA_%03d.bmp", i);
                    save_bmp(path, f);
                    std::fflush(stdout);
                }
                prev = std::move(f);
                Sleep(8);
            }
        }
        typing.store(false);
        typer.join();

        std::printf("---- B: 空闲 ----\n");
        Sleep(600);                          // 等库里把它压下去
        Frame clean;
        grab(clean, gx, gy, gw, gh);
        save_bmp("capB_clean.bmp", clean);
        std::string timeline;
        int ons = 0, flips = 0, last = -1;
        for (int i = 0; i < 240; ++i) {
            Frame f;
            grab(f, gx, gy, gw, gh);
            int bx = 0, by = 0, bw = 0, bh = 0;
            const int n = diff_band(clean, f, band_y0, band_y1, &bx, &by, &bw, &bh);
            const int on = n > 60 ? 1 : 0;
            timeline.push_back(on ? '#' : '.');
            if (on) ++ons;
            if (last >= 0 && on != last) ++flips;
            last = on;
            if (on && ons <= 3) {
                char path[64];
                std::snprintf(path, sizeof path, "capB_%03d.bmp", i);
                save_bmp(path, f);
                std::printf("[B %03d] diff=%-5d bbox=%d,%d %dx%d\n", i, n, bx, by, bw, bh);
            }
            Sleep(8);
        }
        std::printf("[B] %s\n", timeline.c_str());
        std::printf("[B] 240 帧里 %d 帧有影子，翻转 %d 次\n", ons, flips);
        std::fflush(stdout);
        Sleep(200);
        PostMessageW(h, WM_CLOSE, 0, 0);
    }).detach();

    win.run([&](aino::Ui& ui) {
        g_hwnd.store(FindWindowW(nullptr, kTitle));
        ui.set_theme(aino::Theme::light());
        ui.clear(ui.theme().background);
        ui.label("输入框：", aino::Rect(20, 16, 300, 20), aino::Style().left());
        ui.input(&name, aino::Rect(20, 40, 500, 34), aino::Style().hint("在这里打拼音"));
    });
    g_stop.store(true);
    return 0;
}
