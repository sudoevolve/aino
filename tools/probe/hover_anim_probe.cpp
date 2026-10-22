// 验证「spinner / skeleton 只在鼠标悬停时动」。
//
// 做法：窗口里放一个 spinner 和一个 skeleton(.animate())，程序在三个 1.5s 窗口里
// 数**帧数**（frame 回调被调用的次数 = 真正发生了重绘的次数）：
//   ① 光标在窗口外       → 期望 0 帧
//   ② 光标压在 spinner 上 → 期望 >0 帧
//   ③ 光标再移出窗口      → 期望回到 0 帧
//
// 为什么必须真移光标、不能用 PostMessage(WM_MOUSEMOVE)：库里的
// TrackMouseEvent(TME_LEAVE) 会让系统拿**真实**光标位置校验，光标不在窗口内
// 就回灌 WM_MOUSELEAVE，库随即把坐标置成 -1e6。伪造消息对"持续悬停"无效。
//
// 会短暂占用真实光标，结束前恢复原位置。
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <thread>
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

using namespace aino;

static const char* kTitle = "aino-hover-anim";
static const wchar_t* kTitleW = L"aino-hover-anim";
static const float kLogicalW = 420.0f;
// spinner 画在逻辑 (60,60)+64×64 → 中心 (92,92)
static const float kSpinCx = 92.0f, kSpinCy = 92.0f;

static std::atomic<int> g_frames{0};
static std::atomic<int> g_in_spin{0};   // 库看到的鼠标落在 spinner 内的帧数（诊断用）

int main() {
    Window win(int(kLogicalW), 300, kTitle);
    if (!win.ok()) { std::printf("window failed\n"); return 1; }

    POINT saved{};
    GetCursorPos(&saved);

    std::thread driver([&] {
        Sleep(2500);                                  // 预热：窗口约 0.9s 才静下来，留足余量
        HWND h = FindWindowW(nullptr, kTitleW);
        if (!h) { std::printf("no hwnd\n"); win.close(); return; }
        RECT cr{};
        GetClientRect(h, &cr);
        POINT org{0, 0};
        ClientToScreen(h, &org);
        const float scale = float(cr.right) / kLogicalW;
        // 取屏幕左上角当"窗口外"：它永远在屏幕内，SetCursorPos 不会静默失败。
        // 之前用 org.y-200 这种可能落到屏幕外的坐标，SetCursorPos 失败后光标留在
        // 原处，于是"悬停/离开"两段都会读出错误的帧数。
        const int out_x = 4, out_y = 4;
        const int sp_x  = org.x + int(kSpinCx * scale);
        const int sp_y  = org.y + int(kSpinCy * scale);

        auto move_to = [&](int x, int y) {
            SetCursorPos(x, y);
            POINT got{};
            GetCursorPos(&got);
            if (got.x != x || got.y != y) std::printf("  [warn] SetCursorPos(%d,%d) 实际落在 (%ld,%ld)\n",
                                                      x, y, long(got.x), long(got.y));
        };

        // 窗口必须在最前面：被别的窗口挡住时它收不到 WM_MOUSEMOVE，
        // 悬停一执法就永远是 0 帧。
        ShowWindow(h, SW_RESTORE);
        SetForegroundWindow(h);
        BringWindowToTop(h);
        Sleep(300);

        move_to(out_x, out_y);
        Sleep(500);
        int f = g_frames.load(); Sleep(1500);
        const int idle1 = g_frames.load() - f;

        // 悬停段：真实光标移上去，并且每 50ms 补一条 WM_MOUSEMOVE。
        // 光靠 SetCursorPos 不够 —— TrackMouseEvent 会拿真实光标校验，
        // 一旦窗口不是前台就回灌 WM_MOUSELEAVE，把手里的坐标清成 -1e6。
        move_to(sp_x, sp_y);
        const int cxp = int(kSpinCx * scale), cyp = int(kSpinCy * scale);
        f = g_frames.load();
        for (int i = 0; i < 30; ++i) {
            PostMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(cxp, cyp));
            Sleep(50);
        }
        const int hover = g_frames.load() - f;
        Sleep(200);

        move_to(out_x, out_y);                        // 再离开
        Sleep(500);
        f = g_frames.load(); Sleep(1500);
        const int idle2 = g_frames.load() - f;

        std::printf("[hover] scale=%.2f  spinner@(%d,%d)  idle_before=%d  hover=%d  idle_after=%d  in_spin=%d  visible=%d\n",
                    scale, sp_x, sp_y, idle1, hover, idle2, g_in_spin.load(), IsWindowVisible(h));
        std::printf("[hover] verdict: %s\n",
                    (idle1 == 0 && hover > 10 && idle2 == 0) ? "PASS" : "FAIL");

        SetCursorPos(saved.x, saved.y);
        win.close();
    });

    win.run([](Ui& ui) {
        ++g_frames;
        if (Rect(60.0f, 60.0f, 64.0f, 64.0f).contains(ui.input().mouse.x, ui.input().mouse.y))
            ++g_in_spin;
        ui.clear(ui.theme().background);
        ui.spinner(Rect(60.0f, 60.0f, 64.0f, 64.0f));
        ui.skeleton(Rect(200.0f, 70.0f, 180.0f, 16.0f), Style().animate());
        ui.label("把鼠标压在左边的圈上，它才会转", Rect(30.0f, 200.0f, 360.0f, 24.0f),
                 Style().left());
    });

    driver.join();
    return 0;
}
