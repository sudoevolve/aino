// 探针：把 UI 每帧实际看到的鼠标坐标 / 命中判定写进日志文件。
// 目的：区分"鼠标消息没到/坐标换算错" vs "画面没重绘导致抓到了旧帧"。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cstdio>

static const char* LOG = "C:/Users/admin/WorkBuddy/2026-10-16-13-31-22/.workbuddy/probe/hover.log";

int main() {
    aino::Window win(940, 648, "probe");
    if (!win.ok()) return 1;

    FILE* f = std::fopen(LOG, "w");
    if (f) { std::fprintf(f, "--- start ---\n"); std::fflush(f); }

    float lx = -9999.0f, ly = -9999.0f;
    long frames = 0;
    int  last_in_default = -1, last_in_theme = -1, last_in_secondary = -1;

    win.run([&](aino::Ui& ui) {
        const aino::Input& in = ui.input();
        ++frames;

        // demo 里那三个按钮的逻辑矩形
        const aino::Rect def_box(52, 178, 240, 36);     // Default 按钮
        const aino::Rect sec_box(52, 222, 240, 36);     // Secondary 按钮
        const aino::Rect thm_box(940 - 32 - 36, 30, 36, 36);  // 主题按钮

        const int in_def = def_box.contains(in.mouse.x, in.mouse.y) ? 1 : 0;
        const int in_sec = sec_box.contains(in.mouse.x, in.mouse.y) ? 1 : 0;
        const int in_thm = thm_box.contains(in.mouse.x, in.mouse.y) ? 1 : 0;

        const bool moved = (in.mouse.x != lx || in.mouse.y != ly);
        const bool changed = (in_def != last_in_default || in_sec != last_in_secondary ||
                              in_thm != last_in_theme);
        if (moved || changed) {
            lx = in.mouse.x; ly = in.mouse.y;
            last_in_default = in_def; last_in_secondary = in_sec; last_in_theme = in_thm;
            if (f) {
                std::fprintf(f, "frame=%ld mouse=(%.1f,%.1f) def=%d sec=%d thm=%d down=%d\n",
                             frames, in.mouse.x, in.mouse.y, in_def, in_sec, in_thm,
                             in.down[0] ? 1 : 0);
                std::fflush(f);
            }
        }

        ui.clear(ui.theme().background);
    });

    if (f) { std::fprintf(f, "--- end frames=%ld ---\n", frames); std::fclose(f); }
    return 0;
}
