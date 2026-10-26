// 浮层闸门的"绘制顺序"场景 —— 下拉之后再画的控件，不许穿透到弹层上
//
// overlay_probe 里的用例都是"下层控件画在浮层之前"（seq 小），闸门靠 z 比较能挡住。
// 但真实页面经常相反：侧栏顶部先画 select，下面紧接着画列表项。这些列表项的 seq
// 比 select 的 z_first 大，于是闸门认为"你在我上面"，点击直接落到被弹层盖住的列表项上。
//
// 判据：下拉展开后点第二项，sel 必须变成 1，而背后那个按钮一次都不许被点到。
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

#if defined(_WIN32)
#include <windows.h>

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

struct Driver {
    aino::Ui& ui;
    int w = 0, h = 0;
    void frame(const std::function<void()>& draw) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        draw();
        ui.end_frame();
    }
    void idle(const std::function<void()>& draw) { frame(draw); }
    void move(float x, float y, const std::function<void()>& draw) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        ui.impl->input.mouse = aino::Vec2(x, y);
        draw();
        ui.end_frame();
    }
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
};

// 侧栏场景：select 在前，紧跟着一堆列表项，列表项正好落在下拉弹层的位置上。
static void test_select_then_items() {
    std::printf("[1] select 之后再画列表项：点弹层不穿透\n");
    Surface s(640, 480);
    Harness h(&s);
    Driver d{h.ui, 640, 480};
    aino::Ui& ui = h.ui;

    std::vector<std::string> cats = {"曲线", "参数", "极坐标"};
    int cat = 0;
    int under[3] = {0, 0, 0};
    int fired = 0;                 // select 返回 true 的次数（契约：选中当帧就返回）
    const aino::Rect box(20, 20, 180, 32);
    // 弹层：list.y = box.bottom() + 4 = 56，item_h = max(32,32) = 32，pad = 4
    //       item i 的 y = 56 + 4 + 32*i
    aino::Rect item[3];
    for (int i = 0; i < 3; ++i) item[i] = aino::Rect(box.x + 4, 60.0f + 32.0f * float(i), box.w - 8.0f, 32.0f);

    auto draw = [&]() {
        if (ui.select(cats, &cat, box)) ++fired;
        for (int i = 0; i < 3; ++i)
            if (ui.button("行 " + std::to_string(i), item[i])) ++under[i];
    };

    d.idle(draw);
    d.click(box.cx(), box.cy(), draw);         // 展开
    const uint64_t sid = aino::detail::make_id("select", box);
    check(ui.impl->flags[sid] > 0.5f, "下拉已展开");

    d.idle(draw);
    d.idle(draw);

    d.move(item[1].cx(), item[1].cy(), draw);
    check(ui.impl->next_hot != 0, "弹层项获得 hover（不是空 hot）");

    d.click(item[1].cx(), item[1].cy(), draw);  // 点第二项
    const std::string d0 = "sel=" + std::to_string(cat) +
                           " under=" + std::to_string(under[0]) + std::to_string(under[1]) + std::to_string(under[2]);
    check(cat == 1, "点第二项后 sel 变成 1", d0);
    check(fired == 1, "select 当帧返回 true（调用方才能立刻响应）", "fired=" + std::to_string(fired));
    check(under[0] + under[1] + under[2] == 0, "背后的列表项一次都没被点到", d0);
    check(ui.impl->flags[sid] < 0.5f, "选中后收起了下拉");
}

// 反向：弹层项之间也不能互相抢（同一帧里有多个 deferred 时 z 关系要对）
static void test_item_still_clickable() {
    std::printf("[2] 弹层项本身照常可点（第 1/3 项）\n");
    Surface s(640, 480);
    Harness h(&s);
    Driver d{h.ui, 640, 480};
    aino::Ui& ui = h.ui;

    std::vector<std::string> cats = {"曲线", "参数", "极坐标"};
    int cat = 2;
    const aino::Rect box(20, 20, 180, 32);
    aino::Rect item[3];
    for (int i = 0; i < 3; ++i) item[i] = aino::Rect(box.x + 4, 60.0f + 32.0f * float(i), box.w - 8.0f, 32.0f);

    auto draw = [&]() {
        ui.select(cats, &cat, box);
        for (int i = 0; i < 3; ++i) ui.button("行 " + std::to_string(i), item[i]);
    };

    d.idle(draw);
    d.click(box.cx(), box.cy(), draw);
    d.idle(draw); d.idle(draw);
    d.click(item[0].cx(), item[0].cy(), draw);
    check(cat == 0, "点第一项生效", "sel=" + std::to_string(cat));

    // 再开一次，点第三项
    d.idle(draw);
    d.click(box.cx(), box.cy(), draw);
    d.idle(draw); d.idle(draw);
    d.click(item[2].cx(), item[2].cy(), draw);
    check(cat == 2, "点第三项生效", "sel=" + std::to_string(cat));
}

int main() {
    std::printf("=== 浮层闸门：绘制顺序 ===\n");
    test_select_then_items();
    test_item_still_clickable();
    std::printf("\n%s（通过 %d 项，失败 %d 项）\n", g_fail == 0 ? "PASS" : "FAIL", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

#else
int main() { std::printf("（仅 Windows 离屏探针）\n"); return 0; }
#endif
