// 顶栏增量探针：把 demo 顶部那几行调用一个个加进来，看是哪一句让窗口开始
// 每帧都要重绘（正常应该只有 1~2 帧）。
// 用环境变量 AINO_BITS 控制（位掩码）：
//   1=clear  2=icon_button  4=set_theme  8=月亮圆  16=heading  32=muted_text  64=separator
// 每帧把累计帧数写进 topbar_frames.log，外部 2 秒后 kill，取最后一个数。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using aino::Style;
using aino::Rect;

static const char* LOG = "C:/Users/admin/WorkBuddy/2026-10-16-13-31-22/.workbuddy/probe/topbar_frames.log";

int main() {
    const int BITS = std::getenv("AINO_BITS") ? std::atoi(std::getenv("AINO_BITS")) : 127;
    bool dark = false;

    aino::Window win(940, 648, "topbar");
    if (!win.ok()) return 1;

    win.run([&](aino::Ui& ui) {
        static long f = 0; ++f;
        { static FILE* fp = std::fopen(LOG, "w");
          if (fp) { std::fprintf(fp, "%ld ", f); std::fflush(fp); } }

        const Rect theme_btn(940 - 32 - 36, 30, 36, 36);

        if ((BITS & 2) && ui.icon_button("", theme_btn, Style().ghost().corner(8.0f)))
            dark = !dark;
        if (BITS & 4) win.set_theme(dark ? aino::Theme::dark() : aino::Theme::light());
        const aino::Theme& th = ui.theme();

        if (BITS & 1) ui.clear(th.background);

        if (BITS & 8) {
            const aino::Color ic = th.foreground;
            const float bx = theme_btn.cx(), by = theme_btn.cy();
            ui.fill_circle(bx - 1.2f, by - 0.6f, 8.4f, ic);
            ui.fill_circle(bx + 3.6f, by - 3.4f, 7.6f, th.background);
        }

        if (BITS & 16)
            ui.heading("组件画廊", {32, 28, 300, 34}, Style().left());
        if (BITS & 32)
            ui.muted_text("aino 0.1.0 · 单头文件 · 立即模式 · shadcn 风格", {32, 64, 460, 20}, Style().left());
        if (BITS & 64)
            ui.separator({32, 96, 940 - 64, 1});
    });
    return 0;
}
