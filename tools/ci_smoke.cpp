// =============================================================================
//  CI 冒烟测试 —— 三个平台共用（Windows / Linux / macOS）
//
//  目的很窄：在无头环境里验证「能开窗、能跑满 N 帧、能自己退出」，不测像素。
//  像素断言在 Windows 上另有 tools/verify_gallery.py（依赖 PrintWindow，CI 里跑不稳）。
//
//  用法：
//    Linux   xvfb-run -a ./ci_smoke
//    macOS   ./ci_smoke
//    Windows ci_smoke.exe
//
//  返回：0 = 正常跑满 20 帧；2 = 窗口创建失败；3 = 帧循环没跑满
//
//  作者：sudoevolve <sudoevolve@gmail.com>
//  编译：g++ -std=c++17 -O2 ci_smoke.cpp -o ci_smoke -lX11 -ldl
//        clang++ -std=c++17 -O2 ci_smoke.cpp -o ci_smoke \
//                -framework Cocoa -framework CoreText -framework CoreGraphics
// =============================================================================
#define AINO_IMPLEMENTATION
#include "../aino.hpp"

#include <cstdio>

int main() {
    aino::Window win(480, 320, "aino ci smoke");
    if (!win.ok()) {
        std::printf("[smoke] 窗口创建失败（Linux 上多半是没有 DISPLAY）\n");
        return 2;
    }

    win.impl->need_frame = false;
    win.request_repaint();
    if (!win.impl->need_frame) return 4;
    int frames = 0, rejected_requests = 0;
    win.run([&](aino::Ui& ui) {
        ui.clear(ui.theme().background);
        ui.fill_rect(aino::Rect(20.0f, 20.0f, 120.0f, 60.0f), ui.theme().primary, 8.0f);
        ui.label("ci smoke", aino::Rect(20.0f, 96.0f, 200.0f, 24.0f));

        const bool pending = win.impl->need_frame;
        win.request_repaint();
        if (win.impl->need_frame == pending && !ui.request_next_frame()) ++rejected_requests;

        // 显式动画帧仅用于验证后端帧循环。
        if (++frames >= 20) win.close();
        else                ui.request_animation_frame();
    });

    std::printf("[smoke] frames = %d\n", frames);
    if (frames < 20 || rejected_requests != frames) {
        std::printf("[smoke] 帧循环没跑满 —— 后端没要到帧或者提前退出了\n");
        return 3;
    }
    std::printf("[smoke] OK\n");
    return 0;
}
