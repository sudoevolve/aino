// 布局测量模式 + 滚轮归属的断言
//
// 两件容易被"看起来对"骗过去的事，各钉几条：
//
// 1) 测量模式**一个像素都不许落**。最初只让图元与命中测试早退，sparkline 的
//    面积填充走的是 raster_poly 直连画布，于是"先量高度"那趟在屏幕左上角
//    留下了一块折线残影 —— 页面上看起来像 Chart 页左上角粘了块脏东西。
//    现在 begin_measure 会把 canvas 摘掉，任何忘了判 measure_on 的控件都画不出来。
//    断言方式：清成已知色 → 跑一遍测量 → 逐像素比对，必须完全一致。
//
// 2) 嵌套滚动区里，滚轮只归最内层。只按 box.contains(mouse) 判定的话，
//    鼠标在内层列表上滚，外层内容区会跟着一起滚 —— 两个滚动条同时动。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cstdio>
#include <string>
#include <vector>

int g_pass = 0, g_fail = 0;

static void check(bool ok, const char* what, const std::string& detail = std::string()) {
    if (ok) { ++g_pass; std::printf("  [ ok ] %s\n", what); }
    else    { ++g_fail; std::printf("  [FAIL] %s  %s\n", what, detail.c_str()); }
}

// ---------------------------------------------------------------- 离屏画布
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
    // 整块快照 / 比对
    std::vector<uint8_t> snap() const {
        const size_t n = size_t(cv.stride) * size_t(h);
        return std::vector<uint8_t>((const uint8_t*)bits, (const uint8_t*)bits + n);
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

    void frame(float mx, float my, const std::function<void()>& draw) {
        ui.impl->input = aino::Input{};
        ui.impl->input.mouse = aino::Vec2(mx, my);
        ui.begin_frame(float(surf->w), float(surf->h), 1.0f / 60.0f, 1.0f);
        draw();
        ui.end_frame();
    }
};

// =============================================================================
//  1. 测量模式不落像素
// =============================================================================
static void test_measure_no_pixels() {
    std::printf("[1] 测量模式不落像素\n");
    Surface sf(320, 240);
    Harness h(&sf);
    aino::Ui& ui = h.ui;
    const aino::Color bg = aino::Color(17, 34, 51, 255);

    std::vector<float> vals = {0.2f, 0.9f, 0.4f, 0.75f, 0.1f, -0.3f};
    int star = 3, tab = 1, sel = 0, seg = 0;
    bool check_v = false;
    bool flag = false;
    std::string text = "hello";
    const char* code = "int main() {\n  return 0;\n}";

    auto draw_all = [&]() {
        const aino::Rect box(8, 8, 200, 24);
        ui.label("label", box);
        ui.heading("heading", aino::Rect(8, 40, 200, 28));
        ui.button("btn", aino::Rect(8, 72, 90, 32));
        ui.spinner(aino::Rect(120, 72, 24, 24));
        ui.rating(&star, 5, aino::Rect(8, 110, 120, 22));
        // 这两条是当初漏拦的：sparkline 的面积填充直连 raster_poly，
        // bars 走 fill_rect（图元层已拦）。
        ui.sparkline(vals, aino::Rect(8, 140, 200, 40));
        ui.bars(vals, aino::Rect(8, 186, 200, 32));
        ui.tabs({"a", "b"}, &tab, aino::Rect(220, 8, 90, 30));
        ui.segmented({"a", "b"}, &seg, aino::Rect(220, 44, 90, 30));
        ui.select({"x", "y"}, &sel, aino::Rect(220, 80, 90, 30));
        ui.checkbox(&check_v, "check", aino::Rect(220, 116, 90, 20));
        ui.toggle(&flag, aino::Rect(220, 140, 44, 22));
        ui.input(&text, aino::Rect(220, 170, 90, 32));
        ui.code_block(code, aino::Rect(220, 206, 90, 28), aino::Style().font(9.0f));
        ui.drop_shadow(aino::Rect(12, 200, 60, 30), 6.0f, 9.0f,
                       aino::Color(0, 0, 0, 90), 4.0f);
        bool dlg = true;
        ui.dialog("t", "body", &dlg, aino::Rect(40, 40, 200, 120));
    };

    // 先正经画一帧（让字体、动画等状态就位），再清成已知色
    h.frame(0, 0, draw_all);
    h.frame(0, 0, [&]() { ui.clear(bg); });

    const std::vector<uint8_t> before = sf.snap();
    ui.begin_measure();
    draw_all();
    const aino::Rect used = ui.end_measure();
    const std::vector<uint8_t> after = sf.snap();

    size_t diff = 0;
    for (size_t i = 0; i < before.size(); ++i) if (before[i] != after[i]) ++diff;
    check(diff == 0, "测量期间一个字节都没改动画布", "差异字节 " + std::to_string(diff));
    check(used.w > 0.0f && used.h > 0.0f, "量出来的外接矩形非空",
          std::to_string(int(used.w)) + "x" + std::to_string(int(used.h)));

    // 测量结束后画布必须还回来 —— 否则后面所有绘制都是空操作（画面全白）
    h.frame(0, 0, [&]() { ui.clear(bg); ui.fill_rect(aino::Rect(4, 4, 20, 20),
                                                     aino::Color(255, 0, 0, 255)); });
    const uint8_t* q = sf.cv.px + size_t(10) * size_t(sf.cv.stride) + size_t(10) * 4u;
    check(q[aino::detail::CH_R] > 240 && q[aino::detail::CH_G] < 20,
          "end_measure 之后画布已恢复");
}

// =============================================================================
//  2. 测量模式不产生交互副作用
// =============================================================================
static void test_measure_no_side_effects() {
    std::printf("[2] 测量模式不产生副作用\n");
    Surface sf(320, 240);
    Harness h(&sf);
    aino::Ui& ui = h.ui;

    int clicks = 0;
    int idx = 0;
    const aino::Rect btn(20, 20, 100, 32);
    auto draw = [&]() {
        if (ui.button("go", btn)) ++clicks;
        ui.tabs({"a", "b", "c"}, &idx, aino::Rect(20, 60, 240, 32));
    };

    // 每帧都从空 Input 重建，指针压在按钮上
    h.frame(btn.cx(), btn.cy(), draw);
    ui.impl->input = aino::Input{};
    ui.impl->input.mouse = aino::Vec2(btn.cx(), btn.cy());
    ui.impl->input.down[0] = true;
    ui.impl->input.pressed[0] = true;
    ui.begin_frame(320.0f, 240.0f, 1.0f / 60.0f, 1.0f);
    {   // 测量阶段跑同一份代码：不能触发点击、不能改 hot
        ui.begin_measure();
        draw();
        ui.end_measure();
    }
    ui.end_frame();
    check(clicks == 0, "测量阶段点不中按钮", "clicks=" + std::to_string(clicks));

    // 滚轮同理：测量期间不许吃掉滚动距离（否则一次滚轮滚两倍）
    const aino::Rect vp(20, 120, 200, 60);
    float off_measured = -1.0f, off_drawn = -1.0f;
    h.frame(60, 160, [&]() {
        auto sc = ui.scroll(vp, 600.0f);
        off_drawn = sc.offset;
    });
    ui.impl->input = aino::Input{};
    ui.impl->input.mouse = aino::Vec2(60, 160);
    ui.impl->input.wheel = -3.0f;
    ui.begin_frame(320.0f, 240.0f, 1.0f / 60.0f, 1.0f);
    ui.begin_measure();
    {
        auto sc = ui.scroll(vp, 600.0f);
        off_measured = sc.offset;
    }
    ui.end_measure();
    ui.end_frame();
    check(std::fabs(off_measured - off_drawn) < 0.01f, "测量阶段不吃滚轮",
          std::to_string(off_measured) + " vs 绘制 " + std::to_string(off_drawn));
}

// =============================================================================
//  3. 嵌套滚动：滚轮只归最内层
// =============================================================================
static void test_nested_wheel() {
    std::printf("[3] 嵌套滚动的滚轮归属\n");
    Surface sf(400, 300);
    Harness h(&sf);
    aino::Ui& ui = h.ui;

    const aino::Rect outer(10, 10, 380, 280);
    const aino::Rect inner(40, 60, 200, 120);
    float outer_off = 0.0f, inner_off = 0.0f;

    auto draw = [&]() {
        auto a = ui.scroll(outer, 1200.0f);
        outer_off = a.offset;
        ui.fill_rect(outer, aino::Color(200, 200, 200, 255));
        auto b = ui.scroll(inner, 900.0f);
        inner_off = b.offset;
        ui.fill_rect(inner, aino::Color(120, 120, 120, 255));
    };

    h.frame(80, 100, draw);                 // 第一帧：收集滚动区快照
    check(outer_off == 0.0f && inner_off == 0.0f, "初始偏移都是 0");

    // 指针在内层里滚
    for (int i = 0; i < 2; ++i) {
        ui.impl->input = aino::Input{};
        ui.impl->input.mouse = aino::Vec2(80, 100);
        ui.impl->input.wheel = -2.0f;
        ui.begin_frame(400.0f, 300.0f, 1.0f / 60.0f, 1.0f);
        draw();
        ui.end_frame();
    }
    check(inner_off > 1.0f, "内层跟着滚", "inner=" + std::to_string(inner_off));
    check(outer_off == 0.0f, "外层不动（不再一起滚）", "outer=" + std::to_string(outer_off));

    // 指针挪到外层、内层之外：这次该外层滚
    for (int i = 0; i < 2; ++i) {
        ui.impl->input = aino::Input{};
        ui.impl->input.mouse = aino::Vec2(320, 220);
        ui.impl->input.wheel = -2.0f;
        ui.begin_frame(400.0f, 300.0f, 1.0f / 60.0f, 1.0f);
        draw();
        ui.end_frame();
    }
    check(outer_off > 1.0f, "鼠标在外层空白处 → 外层滚",
          "outer=" + std::to_string(outer_off));
}

int main() {
    std::printf("=== 测量模式 / 滚轮归属 ===\n");
    test_measure_no_pixels();
    test_measure_no_side_effects();
    test_nested_wheel();
    std::printf("\n=== 通过 %d 项，失败 %d 项 ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
