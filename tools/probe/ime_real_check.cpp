// 真实输入法手动验证（**不在 regress 里**，会抢键盘、切输入法，不适合无人值守）
//
// 为什么要它：ime_comp_probe 用 ImmSetCompositionStringW 走的是"消息路径"，
// 但真实输入法（微软拼音 = TSF → CUAS 转发）还有两件事只有真打拼音才看得见：
//   · 它发来的 WM_IME_COMPOSITION 里 w = 0x73（没有 GCS_COMPSTR 位），lParam 也不是
//     能读的 HIMC；
//   · 它会在本进程里开一个 "MSCTFIME Composition" 浮动窗，跟内联草稿重影。
//
// 用法：编译运行，它会自己切到中文输入法、点中输入框、打 4 个 's'，然后把
// 窗口连同下面的候选窗一起抓成 ime_shot.bmp / ime_shot2.bmp。
// 期望：草稿画在框里（带下划线）、框外**没有**第二份、候选窗还在。
// 编译：g++ -std=c++17 -O2 ime_real_check.cpp -o ime_real_check.exe
//       -luser32 -lgdi32 -lmsimg32 -limm32 -static
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cstdio>
#include <atomic>
#include <thread>

static std::string g_note = "支持多行输入：\nEnter 换行，长文会自动折行";
static std::string g_name;
static std::atomic<int>  g_phase{0};
static std::string g_comp_seen;   // UI 线程拍下的组字串快照
static std::atomic<HWND> g_hwnd{nullptr};
static const wchar_t* kTitle = L"ime real";

static void save_bmp(const char* path, int x, int y, int w, int h) {
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;          // 负数 = 自上而下，写文件时不用翻转
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY);

    const DWORD px = DWORD(w * h * 4);
    BITMAPFILEHEADER fh{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + px;
    std::FILE* f = std::fopen(path, "wb");
    if (f) {
        std::fwrite(&fh, sizeof fh, 1, f);
        std::fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f);
        std::fwrite(bits, px, 1, f);
        std::fclose(f);
    }
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    std::printf("[shot] saved %s (%dx%d)\n", path, w, h);
}

static BOOL CALLBACK dump_cb(HWND w, LPARAM lp) {
    (void)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    wchar_t cls[128] = {0}, title[128] = {0};
    GetClassNameW(w, cls, 128);
    GetWindowTextW(w, title, 128);
    RECT r{};
    GetWindowRect(w, &r);
    std::printf("[win] hwnd=%p cls=%ls title=%ls vis=%d rect=%ld,%ld,%ld,%ld\n",
                (void*)w, cls, title, int(IsWindowVisible(w)), r.left, r.top, r.right, r.bottom);
    return TRUE;
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

int main() {
    aino::Window win(520, 330, "ime real");
    if (!win.ok()) return 1;

    std::thread([&] {
        Sleep(1500);
        HWND h = g_hwnd.load();
        SetForegroundWindow(h);
        SetFocus(h);
        Sleep(300);
        if (HKL zh = find_zh()) SendMessageW(h, WM_INPUTLANGCHANGEREQUEST, 0, LPARAM(zh));
        Sleep(700);
        // 点一下空输入框（带 hint）—— 这正是用户截图里那个场景
        {
            RECT cr{};
            GetClientRect(h, &cr);
            const float sc = float(cr.right) / 520.0f;
            LPARAM pt = MAKELPARAM(int(100 * sc), int(50 * sc));
            PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, pt);
            PostMessageW(h, WM_LBUTTONUP, 0, pt);
            Sleep(300);
        }
        g_phase.store(1);
        auto key = [](WORD vk) {
            INPUT in{};
            in.type = INPUT_KEYBOARD;
            in.ki.wVk = vk;
            SendInput(1, &in, sizeof in);
            in.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &in, sizeof in);
        };
        for (int i = 0; i < 4; ++i) { key('S'); Sleep(220); }
        Sleep(600);
        EnumWindows(dump_cb, 0);           // 组字中途：看浮动组合窗还在不在
        g_phase.store(2);
        Sleep(300);
        RECT r{};
        GetWindowRect(h, &r);
        save_bmp("ime_shot.bmp", r.left - 10, r.top - 10,
                 (r.right - r.left) + 20, (r.bottom - r.top) + 130);

        // ---- 第二轮：多行 textarea（用户截图那个场景）----
        {
            RECT cr{};
            GetClientRect(h, &cr);
            const float sc = float(cr.right) / 520.0f;
            LPARAM pt = MAKELPARAM(int(400 * sc), int(150 * sc));
            PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, pt);
            PostMessageW(h, WM_LBUTTONUP, 0, pt);
            Sleep(300);
        }
        for (int i = 0; i < 3; ++i) { key('S'); Sleep(220); }
        Sleep(500);
        EnumWindows(dump_cb, 0);
        const std::string before_commit = g_note;
        const std::string comp = g_comp_seen;
        save_bmp("ime_shot2.bmp", r.left - 10, r.top - 10,
                 (r.right - r.left) + 20, (r.bottom - r.top) + 130);
        // 回车结束组字（上屏）：字符必须只进一次 —— 走 WM_CHAR，库里不自己插
        key(VK_RETURN);
        Sleep(700);
        std::printf("[commit] comp=\"%s\"\n", comp.c_str());
        std::printf("[commit] before.len=%zu after.len=%zu\n", before_commit.size(), g_note.size());
        const std::string tail = g_note.size() > before_commit.size()
            ? g_note.substr(before_commit.size()) : std::string("<shrunk>");
        std::printf("[commit] appended=\"%s\" (len=%zu)\n", tail.c_str(), tail.size());
        std::printf("[commit] duplicate=%d\n", int(tail.find(comp) != std::string::npos && comp.size() > 1
                                                     ? tail.rfind(comp) != tail.find(comp) : -1));
        std::fflush(stdout);
        PostMessageW(h, WM_CLOSE, 0, 0);
    }).detach();

    win.run([&](aino::Ui& ui) {
        g_hwnd.store(FindWindowW(nullptr, kTitle));
        ui.set_theme(aino::Theme::light());
        ui.clear(ui.theme().background);
        ui.label("空输入框（带 hint）：", aino::Rect(20, 12, 300, 20), aino::Style().left());
        ui.input(&g_name, aino::Rect(20, 36, 460, 34), aino::Style().hint("you@example.com"));
        ui.label("多行：", aino::Rect(20, 82, 300, 20), aino::Style().left());
        ui.textarea(&g_note, aino::Rect(20, 106, 460, 92), aino::Style().font(13.0f));

        if (g_phase.load() >= 1) g_comp_seen = ui.impl->ime_comp;
        if (g_phase.load() == 1) {
            static int n = 0;
            if (n < 30) {
                std::printf("[frame %2d] ime_comp.len=%zu\n", n++, ui.impl->ime_comp.size());
                std::fflush(stdout);
            }
        }
    });
    return 0;
}
