// 多窗口（同进程、各自一条消息循环跑在自己线程里）是否会互相卡死。
//
// 背景：画廊的"运行最小示例"是 std::thread + 第二个 Window。点了之后整个 UI
// 冻住。这里把那个场景缩到最小：两个 Window、两个线程、都持续请求重绘，
// 于是两边都会频繁走到"补足帧预算"那一步。
//
// 判据不用"看着卡"：每个窗口自己数帧，主线程每秒看一次增量。
// 两个都在涨 = 活；某一个连续 1.5 秒不涨 = 卡死。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace aino;

static std::atomic<long> g_frames[2];
static std::atomic<bool> g_alive[2];

static void run_win(int idx, int w, int h, const char* title) {
    aino::WindowConfig cfg;
    cfg.width  = w;
    cfg.height = h;
    cfg.title  = title;
    cfg.center = false;                 // 别让两个窗口叠在一起
    aino::Window win(cfg);
    if (!win.ok()) { std::printf("[%d] window 创建失败\n", idx); return; }
    std::printf("[%d] 已创建\n", idx);
    std::fflush(stdout);
    g_alive[idx] = true;

    win.run([&](aino::Ui& ui) {
        ui.clear(ui.theme().background);
        ui.panel(Rect(0.0f, 0.0f, ui.width(), ui.height()));
        ui.label(std::string("win ") + std::to_string(idx),
                 Rect(20.0f, 20.0f, 260.0f, 24.0f), Style().font(13.0f));
        // 每帧都请求下一帧 → 两边都在跑动画，都会走"补预算"的分支
        ui.set_animating(true);
        g_frames[idx].fetch_add(1, std::memory_order_relaxed);
    });
    g_alive[idx] = false;
}

int main() {
    std::printf("== 双窗口并发测试（子窗口在主窗口运行中创建，模拟点击按钮）==\n");
    // 子窗口不在启动时建，而是在主窗口已经跑了 20 帧之后从**帧回调里**起线程建 ——
    // 这才是"点一下按钮弹出演示窗口"的真实时序：此刻主线程正处在消息循环 + 绘制中。
    std::thread t1([&] {
        aino::WindowConfig cfg;
        cfg.width = 640; cfg.height = 420; cfg.title = "main"; cfg.center = false;
        aino::Window win(cfg);
        if (!win.ok()) { std::printf("[0] window 创建失败\n"); return; }
        g_alive[0] = true;
        bool spawned = false;
        win.run([&](aino::Ui& ui) {
            ui.clear(ui.theme().background);
            ui.panel(Rect(0.0f, 0.0f, ui.width(), ui.height()));
            ui.label(std::string("win 0"), Rect(20.0f, 20.0f, 260.0f, 24.0f),
                     Style().font(13.0f));
            ui.set_animating(true);
            const long n = g_frames[0].fetch_add(1, std::memory_order_relaxed);
            if (!spawned && n == 20) {
                spawned = true;
                std::printf("[0] 第 20 帧：创建子窗口\n");
                std::fflush(stdout);
                std::thread(run_win, 1, 420, 300, "child").detach();
            }
        });
        g_alive[0] = false;
    });

    long prev0 = 0, prev1 = 0;
    int stall0 = 0, stall1 = 0;
    bool frozen = false;

    for (int i = 0; i < 12; ++i) {              // 12 × 500ms = 6s
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const long f0 = g_frames[0].load(), f1 = g_frames[1].load();
        const long d0 = f0 - prev0, d1 = f1 - prev1;
        std::printf("  t=%4.1fs  win0 %6ld (+%4ld)   win1 %6ld (+%4ld)\n",
                    (i + 1) * 0.5, f0, d0, f1, d1);
        std::fflush(stdout);
        stall0 = (d0 == 0) ? stall0 + 1 : 0;
        stall1 = (d1 == 0) ? stall1 + 1 : 0;
        prev0 = f0; prev1 = f1;
        if (stall0 >= 3 || stall1 >= 3) {       // 单个窗口 1.5s 不动
            frozen = true;
            break;
        }
    }

    if (frozen) {
        std::printf("\n[FAIL] 有窗口停止出帧：win0 %s、win1 %s\n",
                    stall0 >= 3 ? "卡死" : "正常", stall1 >= 3 ? "卡死" : "正常");
    } else {
        std::printf("\n[ ok ] 双窗口 6 秒内都持续出帧（win0 %ld，win1 %ld）\n",
                    g_frames[0].load(), g_frames[1].load());
    }
    std::fflush(stdout);

    // 别等 run() 自己退出（卡死状态下退不出），直接结束进程
    std::_Exit(frozen ? 1 : 0);
}
