// 点击归属 —— 谁能吃到这一次点击、谁绝对不能吃
//
// 三个问题都是"点错东西"的变种，而且都不是浮层闸门能盖住的：
//   1) 滚动视口**外面**的行仍然参与命中：导航滚到底之后，页脚按钮那一片
//      下面压着十几行看不见的列表项，点主题按钮会顺带把页面切走。
//      裁剪只发生在绘制阶段，命中阶段根本不看 clip。
//   2) 按下和松开落在同一帧（快速点击、或帧还没出来两个消息就都到了）时，
//      interact() 里第一个控件处理完会把 active 清成 0，第二个控件接着把
//      "还留在输入里的 pressed" 又消费一遍 —— 一次点击被两个控件各认领一次。
//   3) 上面两条叠在 gallery 的页脚上：主题按钮可点，但页面也跟着跳。
//
// 全部离屏（自建 DIB + 内存 DC），直接写 impl->input 驱动，不开窗口。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

int g_pass = 0, g_fail = 0;

static void check(bool ok, const char* what, const std::string& detail = std::string()) {
    if (ok) { ++g_pass; }
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

// ---------------------------------------------------------------- 帧驱动
struct Driver {
    aino::Ui& ui;
    int w = 0, h = 0;

    void frame(const std::function<void()>& draw) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        draw();
        ui.end_frame();
    }
    void move(float x, float y, const std::function<void()>& draw) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        ui.impl->input.mouse = aino::Vec2(x, y);
        draw();
        ui.end_frame();
    }
    // 按下、松开各一帧（正常鼠标操作的样子）
    void click(float x, float y, const std::function<void()>& draw) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        ui.impl->input.mouse = aino::Vec2(x, y);
        ui.impl->input.down[0] = true;
        ui.impl->input.pressed[0] = true;
        draw();
        ui.end_frame();

        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        ui.impl->input.mouse = aino::Vec2(x, y);
        ui.impl->input.down[0] = false;
        ui.impl->input.released[0] = true;
        draw();
        ui.end_frame();
    }
    // 按下 + 松开塞进同一帧（消息成批到达、或者帧率跟不上手速）
    void click_same_frame(float x, float y, const std::function<void()>& draw) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        ui.impl->input.mouse = aino::Vec2(x, y);
        ui.impl->input.down[0] = false;
        ui.impl->input.pressed[0] = true;
        ui.impl->input.released[0] = true;
        draw();
        ui.end_frame();
    }
};

// ---------------------------------------------------------------- 场景：gallery 的页脚 + 滚动导航
// 几何照抄 examples/gallery.cpp：导航宽 232、页脚 52 高、行高 30、列表从 y=78 起。
struct NavScene {
    static constexpr float kW = 1180.0f, kH = 760.0f;
    static constexpr float kNavW = 232.0f;
    static constexpr float kFootH = 52.0f;
    static constexpr float kItemH = 30.0f;
    static constexpr int   kRows = 40;

    int theme_clicks = 0;
    int last_row = -1;

    aino::Rect list() const { return aino::Rect(0, 78.0f, kNavW, kH - kFootH - 78.0f); }
    aino::Rect theme_btn() const { return aino::Rect(16.0f, kH - kFootH + 9.0f, 34.0f, 34.0f); }

    void draw(aino::Ui& ui) {
        ui.clear(ui.theme().background);
        // 页脚先画 —— 和 gallery 一样，主题按钮在列表之前
        if (ui.icon_button("", theme_btn(), aino::Style().outline())) ++theme_clicks;

        auto sc = ui.scroll(list(), kItemH * float(kRows));
        (void)sc;
        for (int i = 0; i < kRows; ++i) {
            const aino::Rect r = ui.row(kItemH);
            if (ui.nav_item("item " + std::to_string(i), false,
                            aino::Rect(r.x + 10.0f, r.y, r.w - 20.0f, r.h))) {
                last_row = i;
            }
        }
    }
};

// 1. 视口外的行不许吃点击
static void test_row_outside_viewport() {
    std::printf("[1] 滚动视口外的行不参与命中\n");
    Surface s(int(NavScene::kW), int(NavScene::kH));
    Harness h(&s);
    Driver d{h.ui, int(NavScene::kW), int(NavScene::kH)};
    NavScene scene;

    auto draw = [&]() { scene.draw(h.ui); };
    d.frame(draw);
    d.frame(draw);

    // 列表视口下边界
    const aino::Rect lb = scene.list();
    check(lb.bottom() == 708.0f, "列表视口下边界 708", std::to_string(lb.bottom()));

    // 点第一行（可见）：必须切页
    d.click(60.0f, 78.0f + 15.0f, draw);
    check(scene.last_row == 0, "视口内的行可点", "row=" + std::to_string(scene.last_row));

    // 点视口下方（页脚区域里、行 21 的位置）：行绝不能认领
    scene.last_row = -1;
    const float y_in_row21 = 78.0f + NavScene::kItemH * 21.0f + 15.0f;   // 735
    check(y_in_row21 > lb.bottom(), "y=735 确实在视口外", std::to_string(y_in_row21));
    d.move(120.0f, y_in_row21, draw);
    check(h.ui.impl->next_hot == 0, "视口外的行不获得 hover",
          "next_hot=" + std::to_string(h.ui.impl->next_hot));
    d.click(120.0f, y_in_row21, draw);
    check(scene.last_row == -1, "视口外的行不吃点击", "row=" + std::to_string(scene.last_row));
}

// 2. 同一帧按下+松开：一次点击只归一个控件
static void test_same_frame_press() {
    std::printf("[2] 同一帧内按下+松开只点亮一个控件\n");
    Surface s(400, 300);
    Harness h(&s);
    Driver d{h.ui, 400, 300};
    aino::Ui& ui = h.ui;

    int a_cnt = 0, b_cnt = 0;
    // 两个重叠的按钮：a 先画（在下面），b 后画（在上面）
    const aino::Rect common(80.0f, 80.0f, 160.0f, 40.0f);
    auto draw = [&]() {
        if (ui.button("A", common)) ++a_cnt;
        if (ui.button("B", common)) ++b_cnt;
    };

    d.frame(draw);
    d.frame(draw);

    d.click_same_frame(common.cx(), common.cy(), draw);
    check(a_cnt + b_cnt == 1, "同帧点击只触发一个按钮",
          "A=" + std::to_string(a_cnt) + " B=" + std::to_string(b_cnt));

    a_cnt = b_cnt = 0;
    d.click(common.cx(), common.cy(), draw);
    check(a_cnt + b_cnt == 1, "分帧点击也只触发一个按钮",
          "A=" + std::to_string(a_cnt) + " B=" + std::to_string(b_cnt));
}

// 3. gallery 现状回归：点主题按钮不能顺带切页
static void test_theme_button_vs_rows() {
    std::printf("[3] 页脚主题按钮：只切主题，不切页\n");
    Surface s(int(NavScene::kW), int(NavScene::kH));
    Harness h(&s);
    Driver d{h.ui, int(NavScene::kW), int(NavScene::kH)};
    NavScene scene;

    auto draw = [&]() { scene.draw(h.ui); };
    d.frame(draw);
    d.frame(draw);

    const aino::Rect tb = scene.theme_btn();
    // 行 21 恰好压在按钮下面（708..738）
    d.click(tb.cx(), tb.cy(), draw);
    check(scene.theme_clicks == 1, "主题按钮被点到一次",
          "clicks=" + std::to_string(scene.theme_clicks));
    check(scene.last_row == -1, "没顺带切页", "row=" + std::to_string(scene.last_row));

    scene.theme_clicks = 0;
    scene.last_row = -1;
    d.click_same_frame(tb.cx(), tb.cy(), draw);
    check(scene.theme_clicks == 1, "同帧点击：主题按钮仍然只触发一次",
          "clicks=" + std::to_string(scene.theme_clicks));
    check(scene.last_row == -1, "同帧点击：仍然不切页",
          "row=" + std::to_string(scene.last_row));
}

int main() {
    test_row_outside_viewport();
    test_same_frame_press();
    test_theme_button_vs_rows();
    std::printf("=== 通过 %d 项，失败 %d 项 ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
