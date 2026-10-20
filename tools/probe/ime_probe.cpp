// IME 定位探针。
//
// 要证明两件事，缺一不可：
//   ① 底层：win_set_ime_pos() 写进去的组合窗/候选窗位置，系统能原样读回来
//      （结构体是我自己定义的 AinoCompositionFormW，不是 <imm.h> 的 —— 布局错了
//       这里就会读回垃圾，所以必须有这条回读断言）；
//   ② 上层：点进 input 之后，库确实每帧把"光标 x + 输入框下沿"报给平台层。
//
// 组合窗/候选窗**真正画在哪**只能人眼确认（那取决于当前装的输入法），
// 这里测的是"我们交给系统的坐标对不对"。
#include <windows.h>
#include <imm.h>
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <atomic>
#include <cstdio>
#include <thread>

using namespace aino;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* what, const char* detail = "") {
    if (ok) { ++g_pass; std::printf("  [ok  ] %s %s\n", what, detail); }
    else    { ++g_fail; std::printf("  [FAIL] %s %s\n", what, detail); }
}

static const char* kTitle = "aino-ime";
static const wchar_t* kTitleW = L"aino-ime";

// 上层回调捕获
static std::atomic<bool>  g_got{false};
static std::atomic<float> g_x{0}, g_top{0}, g_bottom{0};
static std::string g_text;

int main() {
    std::printf("== IME 定位断言 ==\n");
    Window win(480, 300, kTitle);
    if (!win.ok()) { std::printf("window failed\n"); return 1; }

    // ---- ① 底层回读：结构体布局对不对 ----
    {
        HWND h = FindWindowW(nullptr, kTitleW);
        HIMC himc = h ? ImmGetContext(h) : nullptr;
        bool ok_ctx = himc != nullptr;
        check(ok_ctx, "ImmGetContext 拿到输入法上下文", "");

        if (himc) {
            auto* s = reinterpret_cast<detail::Win32State*>(win.impl->native);
            detail::win_set_ime_pos(s, 123.0f, 40.0f, 76.0f);

            COMPOSITIONFORM cf{};
            cf.dwStyle = CFS_POINT;
            cf.ptCurrentPos.x = -1;
            cf.ptCurrentPos.y = -1;
            const BOOL got_c = ImmGetCompositionWindow(himc, &cf);
            char d[160];
            std::snprintf(d, sizeof(d), "(组合窗读回 %ld,%ld)", long(cf.ptCurrentPos.x),
                          long(cf.ptCurrentPos.y));
            check(got_c && cf.ptCurrentPos.x == 123 && cf.ptCurrentPos.y == 76,
                  "组合窗位置设进去能原样读回", d);

            CANDIDATEFORM cadf{};
            cadf.dwIndex = 0;
            cadf.dwStyle = CFS_CANDIDATEPOS;
            cadf.ptCurrentPos.x = -1;
            cadf.ptCurrentPos.y = -1;
            const BOOL got_d = ImmGetCandidateWindow(himc, 0, &cadf);
            char d2[160];
            std::snprintf(d2, sizeof(d2), "(候选窗读回 %ld,%ld，应 = 下沿+2)",
                          long(cadf.ptCurrentPos.x), long(cadf.ptCurrentPos.y));
            check(got_d && cadf.ptCurrentPos.x == 123 && cadf.ptCurrentPos.y == 78,
                  "候选窗位置设进去能原样读回", d2);

            ImmReleaseContext(h, himc);
        }
    }

    // ---- ② 上层：input 聚焦后每帧上报光标 ----
    POINT saved{};
    GetCursorPos(&saved);
    std::thread driver([&] {
        Sleep(900);
        HWND h = FindWindowW(nullptr, kTitleW);
        if (!h) { win.close(); return; }
        POINT org{0, 0};
        ClientToScreen(h, &org);
        RECT cr{};
        GetClientRect(h, &cr);
        const float scale = float(cr.right) / 480.0f;
        // input 在逻辑 (40,100,300,36) → 点它的中间
        const int cx = org.x + int(180 * scale);
        const int cy = org.y + int(118 * scale);
        // 背靠背投递只有 1/7 生效，留间隔
        PostMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(cx - org.x, cy - org.y));
        Sleep(80);
        PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cx - org.x, cy - org.y));
        Sleep(80);
        PostMessageW(h, WM_LBUTTONUP, 0, MAKELPARAM(cx - org.x, cy - org.y));
        Sleep(600);

        const bool got = g_got.load();
        const float x = g_x.load(), top = g_top.load(), bottom = g_bottom.load();
        char d[200];
        std::snprintf(d, sizeof(d), "(收到 x=%.0f top=%.0f bottom=%.0f，期望 bottom≈%.0f)",
                      x, top, bottom, 136.0 * scale);
        check(got, "点进 input 后库上报了 IME 位置", d);
        // 输入框逻辑 y=100..136 → 物理 150..204；下沿必须是 204
        check(got && bottom > 190.0f && bottom < 215.0f, "上报的下沿 = 输入框底部", "");
        check(got && x >= 60.0f && x <= 520.0f, "上报的 x 在输入框内", "");

        SetCursorPos(saved.x, saved.y);
        win.close();
    });

    bool first = true;
    win.run([&](Ui& ui) {
        if (first) {
            first = false;
            auto orig = ui.impl->ime_pos;
            ui.impl->ime_pos = [orig](float x, float top, float bottom) {
                g_x = x; g_top = top; g_bottom = bottom;
                g_got = true;
                if (orig) orig(x, top, bottom);
            };
        }
        ui.clear(ui.theme().background);
        ui.label("点一下下面的输入框，然后试着打中文", Rect(40.0f, 60.0f, 400.0f, 24.0f),
                 Style().left());
        ui.input(&g_text, Rect(40.0f, 100.0f, 300.0f, 36.0f), Style().hint("点这里再打中文"));
    });

    driver.join();
    std::printf("\n通过 %d 项，失败 %d 项\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
