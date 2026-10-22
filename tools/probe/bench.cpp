// 光栅化基准：把"文字 vs 形状 vs 图形"的开销分开量。
// 一直 repaint()，靠 AINO_FRAME_LOG 每 120 帧打印一次分段耗时。
// 用法：bench.exe <mode>
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using aino::Rect; using aino::Style; using aino::Ui;

int main(int argc, char** argv) {
    const int mode = argc > 1 ? std::atoi(argv[1]) : 0;
    aino::Window win(1080, 700, "bench");
    if (!win.ok()) return 1;

    std::string s = "解析配置 12 ms";
    std::vector<float> wave(24);
    for (size_t i = 0; i < wave.size(); ++i) wave[i] = std::sin(float(i) * 0.55f) * 0.5f + 0.5f;
    std::vector<std::string> heads = { "模块", "状态", "耗时" };
    std::vector<std::vector<std::string>> rows(7);
    for (int r = 0; r < 7; ++r) rows[size_t(r)] = { "解析配置", "完成", "12 ms" };

    win.run([&](Ui& ui) {
        ui.repaint();                        // 强制每帧都重画
        ui.clear(ui.theme().background);
        if (mode == 0) {                     // 只清屏
        } else if (mode == 1) {              // 文字：60 个 label
            for (int i = 0; i < 60; ++i)
                ui.label(s, Rect(20.0f + float(i % 4) * 260.0f, 20.0f + float(i / 4) * 22.0f, 220.0f, 18.0f), Style().left());
        } else if (mode == 2) {              // 形状：3 大面板 + 12 小条
            for (int i = 0; i < 3; ++i)
                ui.panel(Rect(16.0f + float(i) * 352.0f, 16.0f, 336.0f, 660.0f));
            for (int i = 0; i < 12; ++i)
                ui.panel(Rect(40.0f, 40.0f + float(i) * 50.0f, 300.0f, 40.0f), Style().background(ui.theme().muted));
        } else if (mode == 3) {              // 描边：40 个 outline 圆角矩形
            for (int i = 0; i < 40; ++i)
                ui.panel(Rect(20.0f + float(i % 4) * 260.0f, 20.0f + float(i / 4) * 60.0f, 240.0f, 48.0f), Style().outline());
        } else if (mode == 4) {              // 图形：sparkline + bars
            ui.sparkline(wave, Rect(20.0f, 20.0f, 1000.0f, 300.0f), Style());
            ui.bars(wave, Rect(20.0f, 340.0f, 1000.0f, 300.0f), Style());
        } else if (mode == 5) {              // 表格 7×3
            ui.table(heads, rows, Rect(20.0f, 20.0f, 1000.0f, 400.0f), Style());
        } else if (mode == 6) {              // 60 次同一段 ASCII（量每次调用的固定开销）
            for (int i = 0; i < 60; ++i)
                ui.label("Parse config 12 ms", Rect(20.0f + float(i % 4) * 260.0f, 20.0f + float(i / 4) * 22.0f, 220.0f, 18.0f), Style().left());
        } else if (mode == 7) {              // 60 次同一段中文（量字形光栅化的边际成本）
            for (int i = 0; i < 60; ++i)
                ui.label(s, Rect(20.0f + float(i % 4) * 260.0f, 20.0f + float(i / 4) * 22.0f, 220.0f, 18.0f), Style().left());
        } else if (mode == 8) {              // 60 个各不相同的中文串（量字形缓存是否真的生效）
            for (int i = 0; i < 60; ++i) {
                std::string t = "解析配置" + std::to_string(i) + " 项耗时 12 ms";
                ui.label(t, Rect(20.0f + float(i % 4) * 260.0f, 20.0f + float(i / 4) * 22.0f, 220.0f, 18.0f), Style().left());
            }
        } else if (mode == 9) {              // 60 个空 label（量"不画文字"的框架开销）
            for (int i = 0; i < 60; ++i)
                ui.label("", Rect(20.0f + float(i % 4) * 260.0f, 20.0f + float(i / 4) * 22.0f, 220.0f, 18.0f), Style().left());
        }
    });
    return 0;
}
