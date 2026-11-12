// IME 组字串（拼音）必须画在输入框里 —— 不是只在系统那个悬空的组合窗里
//
// 后端把预编辑串取进 ui->ime_comp，input / textarea 画在 caret 处（主色 + 下划线），
// caret 顺延到它后面。这个探针不依赖真人打拼音：用 ImmSetCompositionStringW 往输入法
// 上下文里塞串，再投 WM_IME_COMPOSITION —— 和真实输入法**同一条**消息路径。
//
// 平台与测试约束：
//   · ImmGetContext / ImmSetCompositionString 必须由**窗口所属线程**调，且窗口要有
//     前台焦点，否则 SetCompositionString 返回 FALSE、GCS_COMPSTR 读出来是 0。
//   · 必须先发 WM_IME_STARTCOMPOSITION 再 SetCompositionString。
//   · wParam 是起始字符，组字标志在 lParam；HIMC 始终通过 ImmGetContext(hwnd) 取得。
//     I9 仍覆盖 lParam 缺少 GCS_COMPSTR、上下文却已有草稿的兼容路径。
//   · 空输入框 + hint 很常见，组字时必须让 hint 让位（I4 就是拿 hint 当基线验的：
//     没让位的话 diff 直接是 0）。
//   · driver 线程**不要**直接读 ui->ime_comp —— std::string 跨线程读会撞上写入
//     中间态、读出空串。要读就读 UI 线程拍的快照。
//   · 像素断言使用基线帧，排除输入框本身的背景差异。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace aino;

static const wchar_t* kTitle = L"aino-imecomp-probe";
static const float kLogicalW = 420.0f, kLogicalH = 200.0f;

static std::atomic<HIMC> g_himc{nullptr};
static std::atomic<int>  g_stage{0};          // 2=注入 nihao  4=注入 nihao2（怪消息形态）
static std::atomic<bool> g_ui_saw_comp{false};
static std::mutex        g_mu;
static std::string       g_snapshot;          // UI 线程拍下来的 ime_comp

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* what, const std::string& detail = std::string()) {
    if (ok) { ++g_pass; std::printf("  [OK  ] %s %s\n", what, detail.c_str()); }
    else    { ++g_fail; std::printf("  [FAIL] %s %s\n", what, detail.c_str()); }
}

static std::string snapshot() {
    std::lock_guard<std::mutex> l(g_mu);
    return g_snapshot;
}

static std::vector<uint8_t> grab(HWND h, int* w, int* hgt) {
    RECT rc{};
    GetClientRect(h, &rc);
    *w = rc.right; *hgt = rc.bottom;
    HDC wdc = GetDC(h);
    HDC mdc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, *w, *hgt);
    HGDIOBJ old = SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, *w, *hgt, wdc, 0, 0, SRCCOPY);
    std::vector<uint8_t> px(size_t(*w) * size_t(*hgt) * 4u);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = *w;
    bi.bmiHeader.biHeight = -*hgt;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    GetDIBits(mdc, bmp, 0, UINT(*hgt), px.data(), &bi, DIB_RGB_COLORS);
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(h, wdc);
    return px;
}

// 两帧在给定区域（物理像素）内的差异像素数
static int diff_in(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int w,
                   int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4u;
            if (i + 2 >= a.size() || i + 2 >= b.size()) continue;
            if (std::abs(int(a[i]) - int(b[i])) > 8 ||
                std::abs(int(a[i + 1]) - int(b[i + 1])) > 8 ||
                std::abs(int(a[i + 2]) - int(b[i + 2])) > 8) ++n;
        }
    }
    return n;
}

// 模拟同进程 IME 窗口；后端不得按类名修改其样式、透明度或可见性。
static HWND make_fake_composition_window() {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"MSCTFIME Composition";
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassExW(&wc);       // 可能已被输入法注册过，失败也无所谓
        reg = true;
    }
    // WS_EX_NOACTIVATE 很关键：这个窗口一抢到前台，主窗口就丢了输入法上下文，
    // 组字当场结束（ime_comp 变空），探针就无法检查组字期间的窗口行为。
    return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"MSCTFIME Composition", L"",
                           WS_POPUP, 600, 600, 120, 24,
                           nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

int main() {
    // 必须在建 aino 窗口之前抢注这个类名（见 make_fake_composition_window 的注释）
    HWND fake_comp = make_fake_composition_window();
    const auto original_style = fake_comp ? GetWindowLongPtrW(fake_comp, GWL_EXSTYLE) : 0;

    Window win(int(kLogicalW), int(kLogicalH), "aino-imecomp-probe");
    if (!win.ok()) { std::printf("window failed\n"); return 1; }

    std::string text = "";                 // 空框 + hint：与 gallery 里的输入框一致
    const Rect box(20, 20, 260, 34);

    std::thread driver([&] {
        Sleep(900);
        HWND h = FindWindowW(nullptr, kTitle);
        if (!h) { std::printf("no hwnd\n"); win.close(); return; }
        RECT cr{};
        GetClientRect(h, &cr);
        const float sc = float(cr.right) / kLogicalW;

        // IMM 只给**有前台焦点**的窗口保存组合串
        SetForegroundWindow(h);
        SetFocus(h);
        Sleep(200);
        // 点一下进框
        PostMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(int(30 * sc), int(37 * sc)));
        Sleep(40);
        PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(int(30 * sc), int(37 * sc)));
        Sleep(40);
        PostMessageW(h, WM_LBUTTONUP, 0, MAKELPARAM(int(30 * sc), int(37 * sc)));
        Sleep(500);

        // ---- 基线：此时还没有组字串，框里只有 hint ----
        int w = 0, hh = 0;
        auto px0 = grab(h, &w, &hh);
        check(snapshot().empty() && text.empty(), "I0 基线：草稿与正文都是空（框里只有 hint）");

        // ---- 注入（由 UI 线程执行，IMM 只能窗口线程调）----
        g_stage.store(2);
        PostMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(int(31 * sc), int(37 * sc)));
        for (int i = 0; i < 30 && !g_ui_saw_comp.load(); ++i) Sleep(100);
        check(g_ui_saw_comp.load(), "I1 GCS_COMPSTR → ime_comp 拿到拼音串",
              "\"" + snapshot() + "\"");
        check(snapshot() == "nihao", "I2 草稿内容就是输入法送来的那串", "\"" + snapshot() + "\"");
        check(text.empty(), "I3 组字期间正文一个字节都不动（预编辑不许污染 value）",
              "text=\"" + text + "\"");

        // ---- 像素：和基线（hint）比，框内文字行必须换成草稿字形 ----
        // hint 没让位的话，两帧一模一样 → diff 恰好为 0（这正是修复前的表现）。
        Sleep(250);
        auto px1 = grab(h, &w, &hh);
        const int lx = int(46 * sc), ly = int(26 * sc), rx = int(126 * sc), ry = int(48 * sc);
        const int d = diff_in(px0, px1, w, lx, ly, rx, ry);
        check(d > 20, "I4 草稿真的画在输入框里，而且把 hint 顶掉了（相对 hint 基线有差异）",
              "diff=" + std::to_string(d));

        // ---- 上屏：草稿消失，库不许自己往正文插字 ----
        if (HIMC himc = g_himc.load()) {
            PostMessageW(h, WM_IME_COMPOSITION, 0, GCS_RESULTSTR);
            Sleep(350);
        }
        check(snapshot().empty(), "I5 GCS_RESULTSTR 后草稿清空");
        check(text.empty(), "I6 上屏消息也不会让库自己插字（真实 WM_CHAR 才写）",
              "text=\"" + text + "\"");

        // ---- 再次组字 + 结束组字 ----
        g_ui_saw_comp.store(false);
        g_stage.store(2);
        PostMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(int(32 * sc), int(37 * sc)));
        for (int i = 0; i < 30 && !g_ui_saw_comp.load(); ++i) Sleep(100);
        check(g_ui_saw_comp.load(), "I7 再次进入组字，草稿重新出现", "\"" + snapshot() + "\"");
        PostMessageW(h, WM_IME_ENDCOMPOSITION, 0, 0);
        Sleep(300);
        check(snapshot().empty(), "I8 WM_IME_ENDCOMPOSITION 后草稿清空");

        // ---- I9：wParam 是字符，lParam 无组字标志时仍查询真实输入法上下文 ----
        g_stage.store(4);
        PostMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(int(33 * sc), int(37 * sc)));
        std::string got;
        for (int i = 0; i < 30; ++i) {
            Sleep(100);
            got = snapshot();
            if (!got.empty()) break;
        }
        check(got == "nihao2",
              "I9 wParam=0x73 + lParam=0 时仍从当前上下文读取草稿",
              "\"" + got + "\"");

        // I10～I12：系统 IME 窗口由系统拥有，不能为了内联草稿隐藏共享候选宿主。
        if (fake_comp) {
            ShowWindow(fake_comp, SW_SHOWNOACTIVATE);
            g_ui_saw_comp.store(false);
            g_stage.store(4);
            PostMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(int(34 * sc), int(37 * sc)));
            for (int i = 0; i < 30 && (g_stage.load() != 5 || !g_ui_saw_comp.load()); ++i) Sleep(100);
            check(g_stage.load() == 5 && g_ui_saw_comp.load(), "I10 模拟窗口检查期间保持组字");
            Sleep(100);
            check(IsWindowVisible(fake_comp) != FALSE,
                  "I11 组字与空闲消息处理期间不隐藏 IME 窗口");
            COLORREF key = 0; BYTE alpha = 255; DWORD flags = 0;
            const bool transparent = GetLayeredWindowAttributes(fake_comp, &key, &alpha, &flags) != 0 &&
                                     (flags & LWA_ALPHA) != 0 && alpha == 0;
            check(GetWindowLongPtrW(fake_comp, GWL_EXSTYLE) == original_style && !transparent,
                  "I12 IME 窗口样式与透明度保持输入法原始设置");
        } else {
            check(false, "I10 创建模拟 IME 窗口");
        }

        win.close();
    });

    win.run([&](Ui& ui) {
        ui.input(&text, box, Style().hint("you@example.com"));
        {
            std::lock_guard<std::mutex> l(g_mu);
            g_snapshot = ui.impl->ime_comp;
        }
        if (!ui.impl->ime_comp.empty()) g_ui_saw_comp.store(true);

        // 注入只能窗口线程做（ImmGetContext / ImmSetCompositionString）
        const int st = g_stage.load();
        if (st == 2 || st == 4) {
            HWND h = FindWindowW(nullptr, kTitle);
            if (h) {
                HIMC himc = g_himc.load();
                if (!himc) himc = ImmGetContext(h);
                if (himc) {
                    // 真实输入法的节奏：START → COMPOSITION → END。
                    // 少了 START，IMM 不认后面塞进来的串。
                    SendMessageW(h, WM_IME_STARTCOMPOSITION, 0, 0);
                    const wchar_t* s = (st == 2) ? L"nihao" : L"nihao2";
                    ImmSetCompositionStringW(himc, SCS_SETSTR, (LPVOID)s,
                                             DWORD(std::wcslen(s) * sizeof(wchar_t)),
                                             nullptr, 0);
                    g_himc.store(himc);
                    // 走真实消息路径（不直接改 ui->ime_comp，否则测的是自己）。
                    // st==4 覆盖缺少 GCS_COMPSTR 标志但上下文已有草稿的情况。
                    if (st == 2)
                        PostMessageW(h, WM_IME_COMPOSITION, 0, GCS_COMPSTR);
                    else
                        PostMessageW(h, WM_IME_COMPOSITION, 0x73, 0);
                    g_stage.store(st + 1);
                }
            }
        }
    });
    if (fake_comp) DestroyWindow(fake_comp);
    driver.join();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    std::printf("[imecomp] verdict: %s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
