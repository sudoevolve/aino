// IME 组字串（拼音）必须画在输入框里 —— 不是只在系统那个悬空的组合窗里
//
// 后端把预编辑串取进 ui->ime_comp，input / textarea 画在 caret 处（主色 + 下划线），
// caret 顺延到它后面。这个探针不依赖真人打拼音：用 ImmSetCompositionStringW 往输入法
// 上下文里塞串，再投 WM_IME_COMPOSITION —— 和真实输入法**同一条**消息路径。
//
// 踩过的坑（全是实测，别再踩）：
//   · ImmGetContext / ImmSetCompositionString 必须由**窗口所属线程**调，且窗口要有
//     前台焦点，否则 SetCompositionString 返回 FALSE、GCS_COMPSTR 读出来是 0。
//   · 必须先发 WM_IME_STARTCOMPOSITION 再 SetCompositionString。
//   · **别用 (w & GCS_COMPSTR) 当门槛**。微软拼音是 TSF 输入法，经 CUAS 转发时
//     w = 0x73（只有 COMPREADSTR/READATTR/COMPATTR/COMPCLAUSE），就是没有 GCS_COMPSTR
//     这一位。照那一位判断 → 永远拿不到串 → 用户看到的"拼音没有预先填充"。
//   · **别用 lParam 那个 HIMC**：CUAS 塞过来的句柄 ImmGetCompositionString 返回 0，
//     得自己 ImmGetContext(hwnd)。两条都在 I9 里验。
//   · 空输入框 + hint 很常见，组字时必须让 hint 让位（I4 就是拿 hint 当基线验的：
//     没让位的话 diff 直接是 0）。
//   · driver 线程**不要**直接读 ui->ime_comp —— std::string 跨线程读会撞上写入
//     中间态、读出空串。要读就读 UI 线程拍的快照。
//   · 像素断言必须有基线帧：直接和"全白"比会把背景差也算进来（第一版就这么假通过）。
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

// 假扮一个输入法的浮动组合窗（真名就叫这个类）。后端应该把它压成不可见。
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
    // 组字当场结束（ime_comp 变空），探针就测不到"压制"这件事了。
    return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"MSCTFIME Composition", L"",
                           WS_POPUP, 600, 600, 120, 24,
                           nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

int main() {
    // 必须在建 aino 窗口之前抢注这个类名（见 make_fake_composition_window 的注释）
    HWND fake_comp = make_fake_composition_window();

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
            PostMessageW(h, WM_IME_COMPOSITION, GCS_RESULTSTR, LPARAM(himc));
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

        // ---- I9：真实输入法的消息形态（w=0x73 没有 GCS_COMPSTR 位、lParam 也没有 HIMC）----
        g_stage.store(4);
        PostMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(int(33 * sc), int(37 * sc)));
        std::string got;
        for (int i = 0; i < 30; ++i) {
            Sleep(100);
            got = snapshot();
            if (!got.empty()) break;
        }
        check(got == "nihao2",
              "I9 w=0x73（无 GCS_COMPSTR 位）+ lParam=0 也要能拿到草稿（CUAS 转发就是这个形态）",
              "\"" + got + "\"");

        // ---- I10 / I11：系统那个浮动组合窗要被压下去 ----
        // 两段分开验，因为压制有两条路：
        //   I10 组字消息到达时（win_proc 在 DefWindowProc 之后顺手压）
        //   I11 组字中途它自己又亮出来、一条消息都没有（只能靠 20 ms 线程计时器）
        // 量"它被看见了多少" —— 100 次 2 ms 采样。别单点读 IsWindowVisible：
        // 库里 20 ms 计时器随时把它压回去，撞上那一刻就读成 0（上一版曾因此假红/假绿）。
        auto vis_samples = [&](int n) {
            int v = 0;
            for (int i = 0; i < n; ++i) { if (IsWindowVisible(fake_comp)) ++v; Sleep(2); }
            return v;
        };
        if (fake_comp) {
            // 上一段之后真实 IME 可能已经发过 ENDCOMPOSITION，重新进一次组字
            SetForegroundWindow(h);
            SetFocus(h);
            Sleep(150);
            // 重新点一下输入框：窗口前台一有变动，焦点可能已经被清掉了，
            // 没焦点就不会调 ime_pos，那么 ime_owner 是 false，后端"该不该藏"就判成不该。
            PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(int(34 * sc), int(37 * sc)));
            PostMessageW(h, WM_LBUTTONUP, 0, MAKELPARAM(int(34 * sc), int(37 * sc)));
            Sleep(250);
            g_ui_saw_comp.store(false);
            g_stage.store(4);
            PostMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(int(34 * sc), int(37 * sc)));
            for (int i = 0; i < 30 && !g_ui_saw_comp.load(); ++i) Sleep(100);
            // 故意把它亮出来 100 次采样：只要库在压，它几乎一次都不该被看见。
            ShowWindow(fake_comp, SW_SHOWNOACTIVATE);
            Sleep(60);                                  // 让消息路径/计时器先动手
            const int vis10 = vis_samples(100);
            check(vis10 <= 5,
                  "I10 组字期间把 MSCTFIME Composition 浮动窗压成不可见（不然框里框外两份）",
                  "200ms 内被看见 " + std::to_string(vis10) + "/100 次");

            // I11：再亮一次，且**不发任何消息** —— 这时候只有计时器能压住它
            ShowWindow(fake_comp, SW_SHOWNOACTIVATE);
            Sleep(60);
            const int vis11 = vis_samples(100);
            check(vis11 <= 5,
                  "I11 组字中途别人把它亮出来，计时器也能再压下去（不依赖消息/重绘）",
                  "200ms 内被看见 " + std::to_string(vis11) + "/100 次");

            // I12：光"藏"不够 —— 必须是"分层 + 全透明"。
            // SW_HIDE 只是改状态，输入法每次更新组字都会再 ShowWindow 一次，
            // 我们 20 ms 后才压，于是"亮一下、藏起来、再亮一下"。实测每个按键
            // 都会在框下露出一次 11x15 的小白块（用户报的"一直闪烁"）。
            // 分层 alpha=0 之后窗口**照旧可见**（输入法不会再来抢），只是画不出像素。
            ShowWindow(fake_comp, SW_SHOWNOACTIVATE);
            bool layered = false, alpha0 = false;
            for (int i = 0; i < 20; ++i) {
                const LONG_PTR ex = GetWindowLongPtrW(fake_comp, GWL_EXSTYLE);
                layered = (ex & WS_EX_LAYERED) != 0;
                COLORREF key = 0; BYTE a = 255; DWORD flags = 0;
                alpha0 = GetLayeredWindowAttributes(fake_comp, &key, &a, &flags) != 0
                         && (flags & LWA_ALPHA) != 0 && a == 0;
                if (layered && alpha0) break;
                Sleep(50);
            }
            check(layered && alpha0,
                  "I12 浮动窗被设成「分层 + 全透明」（只 SW_HIDE 会跟输入法拉锯 → 闪烁）",
                  std::string("layered=") + std::to_string(int(layered))
                      + " alpha0=" + std::to_string(int(alpha0)));
        } else {
            std::printf("  [skip] I10/I11 无法创建模拟浮动窗\n");
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
                    // st==4 刻意复刻微软拼音那条怪消息：w 只有 COMPREAD*/COMPATTR/COMPCLAUSE，
                    // 没有 GCS_COMPSTR (0x08)，lParam 里也没有 HIMC。
                    if (st == 2)
                        PostMessageW(h, WM_IME_COMPOSITION, GCS_COMPSTR, LPARAM(himc));
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
