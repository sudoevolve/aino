// 滚动条 —— 它原来是**纯装饰**：只画一个 3px 的滑块，没有任何命中判定，
// 所以除了滚轮以外没法操控（点不动、拖不动）。用户报的"滚动条无法操控"就是这个。
//
// 现在钉死的语义：
//   · 按下滑块拖动 → 按比例改偏移（不是"每像素 1 像素"那种假拖动）
//   · 点轨道 → 滑块中心跳到点击处
//   · 拖动中鼠标离开视口（甚至移出窗口）→ 仍然跟随，直到松手
//   · 松手后不再跟随
//   · 内容没有溢出时不响应（没有滑块就没有拖动）
//   · 两端夹紧，不会滚过头
//
// 全部离屏（自建 DIB），直接写 impl->input 驱动，秒级完成。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <functional>

int g_pass = 0, g_fail = 0;

static void check(bool ok, const char* what, const std::string& detail = std::string()) {
    if (ok) { ++g_pass; }
    else    { ++g_fail; std::printf("  [FAIL] %s  %s\n", what, detail.c_str()); }
}

struct Surface {
    HBITMAP bmp = nullptr;
    HDC dc = nullptr;
    void* bits = nullptr;
    aino::detail::Canvas cv;
    int w = 0, h = 0;

    Surface(int W, int H) : w(W), h(H) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        dc = CreateCompatibleDC(nullptr);
        SelectObject(dc, bmp);
        cv.px = (uint8_t*)bits;
        cv.w = W; cv.h = H; cv.stride = W * 4;
        cv.platform_dc = dc;
    }
    ~Surface() {
        if (dc) DeleteDC(dc);
        if (bmp) DeleteObject(bmp);
    }
};

struct Harness {
    aino::Ui ui;
    aino::detail::GdiTextRenderer tr;
    Surface* surf = nullptr;

    explicit Harness(Surface* s) : surf(s) {
        tr.init(1.0f, std::string(), std::string());
        ui.impl->text = &tr;
        ui.impl->canvas = &surf->cv;
        ui.set_theme(aino::Theme::light());
    }
    ~Harness() { tr.release(); }
};

static const aino::Rect kBox(20.0f, 20.0f, 200.0f, 100.0f);

// 一帧：鼠标状态 + 滚动区（ScrollScope 析构时 end_scroll 里处理交互并画条）
static void frame(aino::Ui& ui, float mx, float my, bool down, bool pressed, bool released,
                  float content_h) {
    ui.begin_frame(260.0f, 160.0f, 1.0f / 60.0f, 1.0f);
    aino::Input& in = ui.impl->input;
    in.down[0] = down;
    in.pressed[0] = pressed;
    in.released[0] = released;
    in.mouse = aino::Vec2(mx, my);
    {
        auto sc = ui.scroll(kBox, content_h);
        (void)sc;
    }
    ui.end_frame();
}

int main() {
    Surface surf(260, 160);

    // ---------------------------------------------------------------- 基础几何
    {
        Harness h(&surf);
        aino::Ui& ui = h.ui;
        const float content = 400.0f;                    // max_off = 300
        const auto g = aino::detail::scrollbar_geom(kBox, 0.0f, content - kBox.h);
        check(std::fabs(g.travel - (kBox.h - g.thumb.h)) < 0.01f, "S0 几何：travel = 视口高 - 滑块高",
              "thumb=" + std::to_string(g.thumb.h) + " travel=" + std::to_string(g.travel));
        check(g.thumb.w <= 3.5f && g.track.w >= 10.0f, "S1 视觉 3px / 热区 11px（细条也要点得中）");
        check(std::fabs(g.thumb.y - kBox.y) < 0.01f, "S2 offset=0 时滑块贴顶");

        // 滑块中心（一开始在顶端）
        const float thumb_cy = g.thumb.cy();
        const float thumb_grab = thumb_cy - g.thumb.y;   // 约 thumb.h/2

        // ------------------------------------------------------------ 拖动
        frame(ui, kBox.right() - 4.0f, thumb_cy, true, true, false, content);
        check(std::fabs(ui.scroll_offset(kBox)) < 0.5f, "S3 按下滑块不跳变");

        const float y2 = kBox.y + thumb_grab + g.travel * 0.5f;   // 拖到一半
        frame(ui, kBox.right() - 4.0f, y2, true, false, false, content);
        const float half = ui.scroll_offset(kBox);
        check(std::fabs(half - (content - kBox.h) * 0.5f) < 2.5f, "S4 拖到行程一半 → 偏移一半",
              "off=" + std::to_string(half));

        // 拖出视口下沿：仍然跟随，并且夹在 max_offset
        frame(ui, kBox.right() - 4.0f, kBox.bottom() + 200.0f, true, false, false, content);
        check(std::fabs(ui.scroll_offset(kBox) - (content - kBox.h)) < 0.5f,
              "S5 拖到视口外仍跟随，并夹在 max_offset");
        check(ui.impl->active != 0, "S6 拖动期间 active 被滚动条占住（下层控件抢不走）");

        // 松手
        frame(ui, kBox.right() - 4.0f, kBox.bottom() + 200.0f, false, false, true, content);
        const float after_release = ui.scroll_offset(kBox);
        check(ui.impl->active == 0, "S7 松手后释放 active");
        frame(ui, kBox.right() - 4.0f, kBox.y + 5.0f, false, false, false, content);
        check(std::fabs(ui.scroll_offset(kBox) - after_release) < 0.01f, "S8 松手后再移动鼠标不再滚动");

        // ------------------------------------------------------------ 点轨道
        ui.scroll_to_index(kBox, 0, 1.0f, 1);            // 回顶部
        frame(ui, kBox.right() - 4.0f, kBox.y, false, false, false, content);
        const float ty = kBox.y + kBox.h * 0.8f;         // 轨道下方（滑块只有顶端一小段）
        frame(ui, kBox.right() - 4.0f, ty, true, true, false, content);
        const float hit = ui.scroll_offset(kBox);
        check(hit > (content - kBox.h) * 0.6f, "S9 点轨道下方 → 滑块跳到点击处",
              "off=" + std::to_string(hit));
        frame(ui, kBox.right() - 4.0f, ty, false, false, true, content);
        frame(ui, kBox.right() - 4.0f, ty + 1.0f, false, false, false, content);
        check(std::fabs(ui.scroll_offset(kBox) - hit) < 0.5f, "S10 点轨道后松手不回弹");
    }

    // ---------------------------------------------------------------- 无溢出 / 边界
    {
        Harness h(&surf);
        aino::Ui& ui = h.ui;
        // 内容比视口矮：没有滑块，右侧点击不许改变任何东西
        frame(ui, kBox.right() - 4.0f, kBox.y + 50.0f, true, true, false, 60.0f);
        frame(ui, kBox.right() - 4.0f, kBox.y + 90.0f, true, false, false, 60.0f);
        check(std::fabs(ui.scroll_offset(kBox)) < 0.01f, "S11 内容没有溢出时滚动条不响应");
        check(ui.impl->active == 0, "S12 没有溢出时不会抢 active");

        // 拖到顶端：夹在 0
        const float content = 400.0f;
        const auto g = aino::detail::scrollbar_geom(kBox, 0.0f, content - kBox.h);
        frame(ui, kBox.right() - 4.0f, g.thumb.cy(), true, true, false, content);
        frame(ui, kBox.right() - 4.0f, kBox.y - 300.0f, true, false, false, content);
        check(std::fabs(ui.scroll_offset(kBox)) < 0.5f, "S13 拖出视口上沿 → 夹在 0");

        // 滚轮仍然能用（别把旧通道弄坏）
        frame(ui, kBox.cx(), kBox.cy(), false, false, false, content);
        ui.begin_frame(260.0f, 160.0f, 1.0f / 60.0f, 1.0f);
        ui.impl->input.mouse = aino::Vec2(kBox.cx(), kBox.cy());
        ui.impl->input.wheel = -2.0f;                    // 向下滚
        { auto sc = ui.scroll(kBox, content); (void)sc; }
        ui.end_frame();
        check(ui.scroll_offset(kBox) > 1.0f, "S14 滚轮通道仍然有效",
              "off=" + std::to_string(ui.scroll_offset(kBox)));
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    std::printf("[scrollbar] verdict: %s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
