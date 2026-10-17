// 二分探针：逐个组件单独渲染 1.5 秒，统计帧数。
// 正常情况（无动画）应该只有 1~2 帧；如果某个组件持续要帧，帧数会到几百。
// 用法: bisect.exe <组件编号>   → 结果写到 .workbuddy/probe/bisect_<编号>.log
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using aino::Style;
using aino::Rect;

int main(int argc, char** argv) {
    const int id = argc > 1 ? std::atoi(argv[1]) : 0;

    char path[512];
    std::snprintf(path, sizeof(path),
                  "C:/Users/admin/WorkBuddy/2026-10-16-13-31-22/.workbuddy/probe/bisect_%d.log", id);
    FILE* f = std::fopen(path, "w");

    aino::Window win(600, 400, "bisect");
    if (!win.ok()) return 1;

    // 各组件的可变状态
    std::string text;
    bool cb = true, tog = false;
    float slider_v = 0.5f;
    int idx = 1, tab = 0, seg = 0;
    const std::vector<std::string> items = {"Starter", "Pro", "Enterprise"};

    long frames = 0;
    const auto t0 = std::chrono::steady_clock::now();

    win.run([&](aino::Ui& ui) {
        ++frames;
        const double el = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();
        ui.clear(ui.theme().background);

        // 每帧都记一行：窗口睡着后不会再有帧，所以"最后一行"就是最终帧数。
        // 由外部 2 秒后 kill，不依赖帧回调里的关闭条件（睡着的窗口等不到下一次回调）。
        if (f) { std::fprintf(f, "%.3f %ld\n", el, frames); std::fflush(f); }

        const Rect box(20, 20, 240, 36);
        switch (id) {
            case 0:  ui.panel({20, 20, 300, 180});
                     ui.heading("居中", {30, 30, 200, 30}, Style().left());
                     ui.separator({20, 60, 200, 1});
                     ui.muted_text("静", {30, 70, 200, 18}, Style().left()); break;
            case 1:  ui.button("按钮", box); break;
            case 2:  ui.icon_button("", box, Style().ghost().corner(8.0f)); break;
            case 3:  ui.input(&text, box, Style().hint("hint")); break;
            case 4:  ui.checkbox(&cb, "勾选", box); break;
            case 5:  ui.toggle(&tog, box); break;
            case 6:  ui.slider(&slider_v, 0.0f, 1.0f, {20, 20, 240, 22}); break;
            case 7:  ui.select(items, &idx, box, Style().hint("选")); break;
            case 8:  ui.tabs(items, &tab, {20, 20, 240, 38}); break;
            case 9:  ui.segmented(items, &seg, {20, 20, 240, 32}); break;
            case 10: ui.progress(0.68f, {20, 20, 240, 8}); break;
            case 11: ui.badge("New", {20, 20, 54, 22}); break;
            case 12: ui.code_text("aino 0.1.0", {20, 20, 240, 28}, Style().font(13.0f)); break;
            case 13: ui.skeleton({20, 20, 240, 20}); break;   // 预期：动画，持续要帧
            default: break;
        }

        if (el > 1.2) win.close();
    });
    if (f) std::fclose(f);
    return 0;
}
