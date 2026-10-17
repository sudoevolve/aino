// 文字宽度诊断 —— 确认 text_width 的单位与 DPI 行为
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cstdio>

static const char* kOut = "C:/Users/admin/WorkBuddy/2026-10-16-13-31-22/.workbuddy/probe/tw_dump.txt";

int main() {
    aino::Window win(420, 200, "probe");
    if (!win.ok()) return 1;

    bool done = false;
    win.run([&](aino::Ui& ui) {
        if (done) return;
        done = true;

        FILE* f = std::fopen(kOut, "w");
        if (!f) return;
        std::fprintf(f, "dpi_scale = %.4f\n", ui.dpi_scale());
        std::fprintf(f, "theme.font_size = %.2f\n", ui.theme().font_size);
        std::fprintf(f, "window logical = %.1f x %.1f\n\n", ui.width(), ui.height());

        const char* words[] = {"好", "取消", "保存并关闭", "A", "AB", "ABCDE"};
        for (int i = 0; i < 6; ++i) {
            std::fprintf(f, "%-16s  fs14 = %7.2f   fs28 = %7.2f\n", words[i],
                         ui.text_width(words[i], 14.0f, 400),
                         ui.text_width(words[i], 28.0f, 400));
        }
        std::fclose(f);
    });
    return 0;
}
