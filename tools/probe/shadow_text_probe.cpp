// =============================================================================
//  阴影 / 文本溢出的【无窗口数值探针】
//
//  两个回归点：
//    · 阴影：旧实现是"叠 N 层放大的圆角矩形"，blend 累积之后边缘的实际 alpha
//      远高于调用方传进来的值（传 46 渲出 ~148）—— 这就是"阴影太严重"的根因。
//      现在的实现是距离场衰减，必须保证 peak <= 传入的 alpha。
//    · 文本：draw_text 只画一行且不裁剪，对话框正文一长就横着爬出面板。
//      现在换行 + 裁到 box，必须保证文字墨迹不越过内边距。
//
//  编译：cl /std:c++17 /EHsc /O2 /utf-8 shadow_text_probe.cpp user32.lib gdi32.lib
// =============================================================================
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using aino::Color;
using aino::Rect;
using aino::Style;
using aino::Ui;
using aino::Vec2;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* what) {
    if (ok) { ++g_pass; }
    else { ++g_fail; std::printf("  FAIL  %s\n", what); }
}

static const float W = 720.0f, H = 520.0f;

struct Harness {
    Ui ui;
    aino::detail::GdiTextRenderer tr;
    aino::Canvas cv;
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;

    Harness() {
        // 文字是交给 GDI 画的，而 GDI 画进的是 DIB section —— 不是我们的内存缓冲。
        // 所以无窗口探针也必须自己建一个 DIB + 内存 DC，否则 draw_text 会静默跳过
        // （GdiTextRenderer::draw 开头就是 `if (!dc) return;`，什么都不报）。
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = int(W);
        bmi.bmiHeader.biHeight      = -int(H);        // 负 = top-down
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        HDC screen = GetDC(nullptr);
        dc  = CreateCompatibleDC(screen);
        void* bits = nullptr;
        bmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        SelectObject(dc, bmp);
        ReleaseDC(nullptr, screen);

        cv.px = reinterpret_cast<uint8_t*>(bits);
        cv.w = int(W);
        cv.h = int(H);
        cv.stride = int(W) * 4;
        cv.platform_dc = dc;

        tr.init(1.0f, "", "");
        ui.begin_frame(W, H, 1.0f / 60.0f, 1.0f);
        ui.impl->canvas = &cv;
        ui.impl->text = &tr;
    }
    ~Harness() {
        if (bmp) DeleteObject(bmp);
        if (dc)  DeleteDC(dc);
    }

    Color px(int x, int y) const {
        const uint8_t* p = cv.px + size_t(y) * size_t(cv.stride) + size_t(x) * 4u;
        return Color(p[aino::detail::CH_R], p[aino::detail::CH_G], p[aino::detail::CH_B], p[aino::detail::CH_A]);
    }
    // 黑阴影压在纯白上：像素值 = 255*(1-a/255)，反推有效 alpha
    int alpha_at(int x, int y) const { return 255 - int(px(x, y).r); }
};

// -----------------------------------------------------------------------------
//  1. 阴影：峰值不许超过传入的 alpha，且必须随距离单调衰减
// -----------------------------------------------------------------------------
static void test_shadow() {
    std::printf("== 阴影（距离场衰减）==\n");
    Harness h;
    h.ui.fill_rect(Rect(0, 0, W, H), Color(255, 255, 255), 0.0f);

    const Rect box(160, 120, 260, 150);
    const float blur = 22.0f, dy = 8.0f;
    const int nominal = 46;                       // 和 dialog 的实际参数一致
    h.ui.drop_shadow(box, 8.0f, blur, Color(0, 0, 0, nominal), dy);

    // 沿盒子中线向左采样，看离边缘 1..blur+4 像素的 alpha 剖面
    const int y = int(box.cy() + dy);
    int peak = 0;
    int prev = 1 << 20;
    bool mono = true;
    std::printf("  离边缘像素: alpha 剖面  ");
    for (int d = 0; d <= int(blur) + 6; ++d) {
        const int x = int(box.x) - 1 - d;
        const int a = h.alpha_at(x, y);
        if (d <= int(blur)) {
            std::printf("%d:%d ", d, a);
            if (a > peak) peak = a;
            if (a > prev + 1) mono = false;
            prev = a;
        }
    }
    std::printf("\n");
    std::printf("  峰值 alpha = %d（传入 %d）\n", peak, nominal);

    check(peak <= nominal, "阴影峰值不得超过调用方传入的 alpha");
    check(mono, "阴影必须随距离单调衰减（不能出现脏环）");
    // 高斯语义下轮廓处的覆盖率是 0.5，拖尾要铺到约 1.35×blur 才归零
    // （Figma 的 blur 就是高斯 σ = blur/2）。所以判"完全透明"的点取 1.4×blur。
    check(h.alpha_at(int(box.x) - 1 - int(blur * 1.4f) - 2, y) == 0,
          "1.4×blur 之外必须完全透明（高斯拖尾）");
    check(peak >= 12, "阴影又不能太弱到看不见");
    // 旧实现在同样参数下是这个量级 —— 留一条明确的"别退回去"的线
    check(peak < 80, "峰值必须远低于旧实现的 ~148");
}

// -----------------------------------------------------------------------------
//  2. 文本：\n 生效、按宽换行生效、且墨迹不越过内边距
// -----------------------------------------------------------------------------
static void test_text_wrap() {
    std::printf("== 文本换行与裁剪 ==\n");

    // --- 2a. '\n' 一定要换行 ---
    {
        Harness h;
        h.ui.fill_rect(Rect(0, 0, W, H), Color(255, 255, 255), 0.0f);
        h.ui.label("第一行\n第二行", Rect(20, 20, 300, 60), Style().left().top().font(14.0f));
        auto row_has_ink = [&](int y) {
            for (int x = 20; x < 300; ++x) if (h.px(x, y).r < 200) return true;
            return false;
        };
        int rows = 0;
        for (int y = 20; y < 80; ++y) if (row_has_ink(y)) ++rows;
        std::printf("  '\\n' 两行文字占 %d 个像素行\n", rows);
        check(rows >= 16, "'\\n' 必须产生两行（旧实现会画成一行）");
        check(rows < 44, "两行不该超过两行的高度");
    }

    // --- 2b. dialog 正文：长文必须换行，且不许爬出内边距 ---
    {
        Harness h;
        h.ui.fill_rect(Rect(0, 0, W, H), Color(255, 255, 255), 0.0f);   // 先铺白底
        bool open = true;
        const Rect box(40.0f, 40.0f, 420.0f, 240.0f);
        const float pad = 24.0f;
        const std::string body =
            "删除后无法恢复。这个对话框的遮罩、面板、关闭按钮都由 dialog() 负责，"
            "下面两个按钮是调用方画的 —— 正文一长就必须自己折行，"
            "否则会一直向右爬出面板，压在半透明遮罩上。";
        h.ui.dialog("确认删除", body, &open, box);
        h.ui.end_frame();

        const int y0 = int(box.y + pad) + 30, y1 = int(box.bottom() - pad);
        auto ink = [&](int x, int y) { return h.px(x, y).r < 205; };

        int ink_rows = 0, max_ink_x = -1;
        for (int y = y0; y < y1; ++y) {
            bool any = false;
            for (int x = int(box.x); x < int(box.right()); ++x)
                if (ink(x, y)) { any = true; if (x > max_ink_x) max_ink_x = x; }
            if (any) ++ink_rows;
        }
        const int limit = int(box.right() - pad);   // 正文列的右边界
        std::printf("  正文墨迹行数 %d，最右墨迹 x=%d（正文列右界 %d）\n", ink_rows, max_ink_x, limit);

        check(ink_rows >= 30, "长正文必须折成多行（旧实现只有一行）");
        check(max_ink_x <= limit, "正文墨迹不得越过内边距右界（横向溢出的根因）");
        check(max_ink_x >= 0, "正文确实画出来了");

        // 阴影侧写：白底 + 遮罩之上，面板外一圈只该有"很浅"的影子。
        // alpha_at 是"相对纯白的变暗量"；遮罩的贡献**按 token 算**，不能写死 ——
        // 之前写 110，后来 overlay 换成 rgba(171,171,171,153)（白底只压暗 50），
        // 这条就假红了。
        const aino::Color mask_c = aino::Theme::light().overlay.over(aino::Color(255, 255, 255));
        const int base = 255 - int(mask_c.r);
        const int a_just = h.alpha_at(int(box.right()) + 3, int(box.cy()));
        const int a_far  = h.alpha_at(int(box.right()) + 34, int(box.cy()));
        std::printf("  面板右侧变暗量：+3px=%d，+34px=%d（遮罩基线 %d）\n", a_just, a_far, base);
        // +34px 相对 90px 的模糊半径只有 0.4σ，影子还剩一点是**对的**
        // （Primary 投影就是 "0 30 90"）。这里要的是"明显变淡"，不是"归零"。
        check(a_far < a_just && a_far <= base + 20, "影子随距离变淡，34px 处已经很浅");
        check(a_just <= base + 70, "面板右侧不该叠出深色脏边（旧实现这里会接近 255）");
        check(a_just > base + 3, "面板右侧确实有影子（不能淡到没有）");
    }

    // --- 2c. 单行仍然是单行（快路径没被改坏）---
    {
        Harness h;
        h.ui.fill_rect(Rect(0, 0, W, H), Color(255, 255, 255), 0.0f);
        h.ui.label("短", Rect(20, 20, 300, 60), Style().left().top().font(14.0f));
        int rows = 0;
        for (int y = 20; y < 80; ++y) {
            for (int x = 20; x < 300; ++x) if (h.px(x, y).r < 200) { ++rows; break; }
        }
        std::printf("  单行文字占 %d 个像素行\n", rows);
        check(rows >= 8 && rows <= 20, "单行文字应只占一行");
    }
}

// -----------------------------------------------------------------------------
//  3. nav_item：点击能返回 true，选中态会画底色
// -----------------------------------------------------------------------------
static void test_nav_item() {
    std::printf("== nav_item ==\n");
    Harness h;
    h.ui.fill_rect(Rect(0, 0, W, H), Color(255, 255, 255), 0.0f);
    const Rect item(20, 20, 200, 34);

    h.ui.impl->input.mouse = Vec2(item.cx(), item.cy());
    h.ui.impl->input.down[0] = true;
    h.ui.impl->input.pressed[0] = true;
    h.ui.nav_item("概览", true, item);
    // interact() 的 clicked 是"在盒子里按下 + 抬起" —— 只按不抬永远不返回 true
    h.ui.impl->input.down[0] = false;
    h.ui.impl->input.released[0] = true;
    const bool clicked = h.ui.nav_item("概览", true, item);
    check(clicked, "nav_item 按下再抬起应返回 true");

    // 选中态：盒子左侧（竖条）与中部（底色）都不该是纯白
    const bool bar = h.px(int(item.x) + 4, int(item.cy())).r < 250;
    check(bar, "选中态应画出左侧指示条");
}

// -----------------------------------------------------------------------------
//  4. text_height / alert_height：量出来的高度必须真的够放下画出来的墨迹
//
//     "库不做高度自适应"是刻意的设计，但"高度不能量"是个假命题 —— 折行之后
//     行数乘行高就是高度，而折行规则和执行绘制用的是同一段代码。这里就验证
//     text_height 与实测墨迹高度一致，并验证 alert_height 给的高度刚好放得下。
// -----------------------------------------------------------------------------
static void test_measured_height() {
    std::printf("== text_height / alert_height ==\n");

    Style st;
    st.font_size = 14.0f;
    st.wrap = true;

    // --- 4a. 折行文本：量出来 vs 画出来 ---
    {
        Harness h;
        h.ui.fill_rect(Rect(0, 0, W, H), Color(255, 255, 255), 0.0f);
        const float w = 300.0f;
        const std::string s =
            "这是一段需要折行的长文本，用来验证 text_height 量出来的高度和实际画出来的"
            "墨迹高度是否一致 —— 差太多的话，调用方按它留的高度就会裁字。";
        const float th = h.ui.text_height(s, w, st);
        const float lh = h.ui.line_height(14.0f);

        h.ui.label(s, Rect(20, 20, w, 400), Style().font(14.0f).left().top().wrap_text());
        int first = -1, last = -1;
        for (int y = 20; y < 420; ++y) {
            bool any = false;
            for (int x = 20; x < int(20 + w); ++x) if (h.px(x, y).r < 205) { any = true; break; }
            if (any) { if (first < 0) first = y; last = y; }
        }
        const int ink_h = last - first + 1;
        std::printf("  text_height=%.1f 行高=%.1f 墨迹高=%d（约 %.1f 行）\n",
                    th, lh, ink_h, th / lh);
        check(th > lh * 2.5f, "长文本必须量出多行高度");
        check(ink_h <= th + 2.0f, "墨迹高度不得超过 text_height（否则调用方留不够）");
        // text_height 返回的是"行框"总高，而字形不会填满整个行框（行距是富余的），
        // 所以墨迹必然比它矮一点 —— 但差距不该到一个行高以上。
        check(ink_h >= th - lh, "墨迹不该比 text_height 矮整整一行以上");
    }

    // --- 4b. '\n' 的行数与高度成正比 ---
    {
        Harness h;
        const float lh = h.ui.line_height(14.0f);
        const float t3 = h.ui.text_height("甲\n乙\n丙", 300.0f, Style().font(14.0f));
        std::printf("  3 个逻辑行 = %.1f（行高 %.1f）\n", t3, lh);
        check(std::fabs(t3 - 3.0f * lh) < 0.7f, "3 个逻辑行应量出 3 倍行高");
    }

    // --- 4c. alert_height 给的高度必须刚好放得下正文 ---
    {
        const float fs = 14.0f, pad = 14.0f, d = fs + 8.0f;
        const std::string title = "删除后无法恢复";
        const std::string body =
            "这条提示条的正文会按宽度自动折行，所以高度必须能量出来 —— "
            "拍一个 74 上去，正文一折行就会被裁掉。";

        auto last_ink_row = [&](float box_h) {
            Harness h;
            h.ui.fill_rect(Rect(0, 0, W, H), Color(255, 255, 255), 0.0f);
            const Rect box(40.0f, 40.0f, 420.0f, box_h);
            h.ui.alert(title, body, box);
            const int tx = int(box.x + pad + d + 10.0f);
            const int tw = int(box.w - pad * 2.0f - d - 10.0f);
            int last = -1;
            for (int y = int(box.y); y < int(box.bottom()); ++y) {
                for (int x = tx; x < tx + tw; ++x) {
                    if (h.px(x, y).r < 205) { last = y; break; }
                }
            }
            return last;
        };

        Harness hm;
        const float H_ok = hm.ui.alert_height(title, body, 420.0f, Style().font(fs));
        const int box_lo = int(40.0f + H_ok - pad);          // 正文允许画到的下界
        const int ink_ok = last_ink_row(H_ok);
        std::printf("  alert_height=%.1f，账算高度下最后墨迹行=%d（下界 %d）\n",
                    H_ok, ink_ok, box_lo - 1);
        check(H_ok > 74.0f, "折行正文需要的高度应大于拍脑袋的 74");
        check(ink_ok >= 0 && ink_ok <= box_lo - 1, "按 alert_height 给高度，正文必须完整放得下");

        const int ink_small = last_ink_row(H_ok - 14.0f);
        const int small_lo = int(40.0f + (H_ok - 14.0f) - pad);
        std::printf("  故意少给 14px 后最后墨迹行=%d（正文框底 %d → 被裁到底）\n",
                    ink_small, small_lo);
        check(ink_small < ink_ok, "少给高度时正文会被截短");
        check(ink_small >= small_lo - 2, "被裁的墨迹会一直顶到正文框底部（不是自然结束）");
    }
}

int main() {
    test_shadow();
    test_text_wrap();
    test_nav_item();
    test_measured_height();
    std::printf("\n通过 %d 项，失败 %d 项\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
