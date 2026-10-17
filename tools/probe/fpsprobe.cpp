// 探针：实测空闲时的帧率，用来验证"事件驱动 / 空闲 0% CPU"这个设计目标。
// 只画一屏静态内容，不点击、不悬停 —— 之后窗口应该停在 WaitMessage 上，帧率掉到 0。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <chrono>
#include <cstdio>

static const char* LOG = "C:/Users/admin/WorkBuddy/2026-10-16-13-31-22/.workbuddy/probe/fps.log";

int main() {
    aino::Window win(940, 648, "fps");
    if (!win.ok()) return 1;

    FILE* f = std::fopen(LOG, "w");
    long frames = 0, prev_frames = 0;
    auto t0 = std::chrono::steady_clock::now();
    double prev_el = 0.0, last_report = 0.0;

    win.run([&](aino::Ui& ui) {
        ++frames;
        const auto now = std::chrono::steady_clock::now();
        const double el = std::chrono::duration<double>(now - t0).count();

        if (el - last_report >= 0.5) {
            const double df = double(frames - prev_frames);
            const double dt = el - prev_el;
            if (f) {
                std::fprintf(f, "t=%5.2f  frames=%-6ld  本窗口fps=%.1f\n", el, frames, df / dt);
                std::fflush(f);
            }
            prev_frames = frames; prev_el = el; last_report = el;
        }

        // 静态内容，没有任何动画/焦点/悬停
        ui.clear(ui.theme().background);
        ui.panel({20, 20, 300, 180});
        ui.heading("空闲测试", {40, 44, 260, 30}, aino::Style().left());
        ui.muted_text("窗口不动鼠标时，这里不应该有任何重绘", {40, 90, 260, 20}, aino::Style().left());
        ui.button("按钮", {40, 130, 120, 36});
        ui.separator({20, 230, 880, 1});
        ui.badge("静态", {40, 250, 60, 22});

        if (el > 4.0) win.close();
    });

    if (f) { std::fprintf(f, "--- 结束: 总计 %ld 帧 ---\n", frames); std::fclose(f); }
    return 0;
}
