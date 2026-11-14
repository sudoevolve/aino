// textarea 每帧成本随行数增长（离屏，无窗口）。
//
// 台架而非断言：时间是环境相关的，进 CI 只会制造抖动。它是"切行缓存还在不在生效"
// 的现场证据 —— 20,000 行应当在 1 ms 量级，回到 6 ms 说明缓存没命中。
//
// 注意：没有交互桌面时 GDI 取不到 DC，文字量宽/绘制是空转，因此本台架只反映
// 切行与簿记成本；在真实桌面上再加上可见行数的绘制开销（约 0.2 ms 量级）。
//
//   g++ -std=c++17 -O2 -o textarea_bench.exe tools/probe/textarea_bench.cpp -lgdi32 -luser32 -static
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cstdio>
#include <chrono>
#include <vector>
#include <string>

static double now_ms() {
    using namespace std::chrono;
    return double(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count()) / 1000.0;
}

int main() {
    const int W = 800, H = 400;
    std::vector<uint8_t> px(size_t(W) * H * 4, 255);
    aino::Canvas cv; cv.px = px.data(); cv.w = W; cv.h = H; cv.stride = W * 4;

    aino::Ui ui;
    aino::detail::GdiTextRenderer tr;
    tr.init(1.0f, "", "");
    ui.impl->text = &tr;
    ui.impl->canvas = &cv;

    for (int lines : {100, 1000, 5000, 20000}) {
        std::string doc;
        for (int i = 0; i < lines; ++i) {
            doc += "第 " + std::to_string(i) + " 行：一些中文内容 abcdef\n";
        }
        ui.begin_frame(W, H, 1.0f / 60.0f, 1.0f);
        const aino::Rect box(20, 20, 700, 40);
        ui.textarea(&doc, box);           // 预热（首次会建字体）
        ui.end_frame();

        // 无选区、未聚焦
        double t0 = now_ms();
        for (int k = 0; k < 20; ++k) {
            ui.begin_frame(W, H, 1.0f / 60.0f, 1.0f);
            ui.textarea(&doc, box);
            ui.end_frame();
        }
        const double idle = (now_ms() - t0) / 20;

        // 聚焦 + 全选（走选区逐行路径）
        ui.impl->focus = reinterpret_cast<void*>(1);
        ui.impl->sel_anchor = 0;
        ui.impl->caret = aino::utf8_count(doc);
        t0 = now_ms();
        for (int k = 0; k < 10; ++k) {
            ui.begin_frame(W, H, 1.0f / 60.0f, 1.0f);
            ui.textarea(&doc, box);
            ui.end_frame();
        }
        const double sel = (now_ms() - t0) / 10;
        printf("%6d 行 / %7zu 字节：无选区 %7.2f ms   全选 %7.2f ms\n",
               lines, doc.size(), idle, sel);
        ui.impl->focus = nullptr;
        ui.impl->sel_anchor = -1;
    }
    return 0;
}
