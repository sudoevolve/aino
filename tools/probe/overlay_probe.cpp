// 浮层输入闸门 —— 遮罩下的点击 / 悬停 / 光标 / 滚轮 / 键盘都不许穿透
//
// 立即模式里"最后画"的组件视觉上在最上层，但交互是按调用顺序处理的：下层按钮
// 先跑、先把点击吃掉。所谓"穿透"不止点击一种：
//   · 悬停：遮罩下的按钮亮起来、spinner 开始转、tooltip 冒出来
//   · 光标：指针移过去还是变成小手 / I 形
//   · 滚轮：滚动条在遮罩后面悄悄滚动
//   · 键盘：焦点还留在被盖住的输入框里，打的字照样进去
//   · 层级：被面板"几何盖住"的控件，按位置白名单会被放行，照样能点
// 这些都必须一起挡住，只挡点击等于没挡。
//
// 全部离屏（自建 32bpp DIB + 内存 DC），靠直接写 impl->input 驱动，不开窗口、秒级完成。
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
// 一次"点击"必须是两帧：第一帧 pressed（按下），第二帧 released（松开）。
// interact() 的 clicked 只在松开帧成立，所以不能只投一帧。
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

    void wheel(float x, float y, float dz, const std::function<void()>& draw) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        ui.impl->input.mouse = aino::Vec2(x, y);
        ui.impl->input.wheel = dz;
        draw();
        ui.end_frame();
    }

    // 中途要读状态（defer 数量 / cursor / phase）时用这两个，
    // 因为 end_frame 会把本帧的临时状态清掉。
    void begin(float x, float y) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        ui.impl->input.mouse = aino::Vec2(x, y);
    }
    void end() { ui.end_frame(); }

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

// ---------------------------------------------------------------- 1. dialog 遮罩
static void test_dialog_backdrop() {
    std::printf("[1] dialog：遮罩上的点击不穿透\n");
    Surface s(640, 480);
    Harness h(&s);
    Driver d{h.ui, 640, 480};
    aino::Ui& ui = h.ui;

    bool open = true;
    int  under = 0, inner = 0;
    const aino::Rect under_r(20, 300, 120, 36);                       // 下层按钮，在面板外
    const aino::Rect panel(200, 90, 300, 200);
    const aino::Rect inner_r(panel.x + 20, panel.bottom() - 52, 110, 32);   // 面板内按钮

    auto draw = [&]() {
        if (ui.button("下层", under_r)) ++under;
        if (ui.dialog("标题", "正文", &open, panel)) {
            if (ui.button("面板内", inner_r)) ++inner;
        }
    };

    d.idle(draw);                 // 第 1 帧：dialog 登记"可交互区"
    d.idle(draw);                 // 第 2 帧：闸门生效

    d.move(under_r.cx(), under_r.cy(), draw);
    check(ui.impl->next_hot == 0, "遮罩下的按钮不获得 hover",
          "next_hot=" + std::to_string(ui.impl->next_hot));

    d.click(under_r.cx(), under_r.cy(), draw);
    check(under == 0, "点遮罩不穿透到下层按钮", "under=" + std::to_string(under));
    check(!open, "点遮罩关闭 dialog");

    d.idle(draw);
    d.idle(draw);
    d.move(under_r.cx(), under_r.cy(), draw);
    check(ui.impl->next_hot != 0, "dialog 关闭后下层按钮重新获得 hover");

    const int before = under;          // 用增量：前面若真穿透了，这里也要如实反映
    d.click(under_r.cx(), under_r.cy(), draw);
    check(under == before + 1, "关闭后下层按钮恢复可点",
          std::to_string(before) + " -> " + std::to_string(under));
    check(inner == 0, "全程没误触面板内按钮", "inner=" + std::to_string(inner));
}

// ---------------------------------------------------------------- 2. dialog 面板内
static void test_dialog_inner() {
    std::printf("[2] dialog：面板内自己画的按钮照常可点\n");
    Surface s(640, 480);
    Harness h(&s);
    Driver d{h.ui, 640, 480};
    aino::Ui& ui = h.ui;

    bool open = true;
    int  inner = 0;
    const aino::Rect panel(200, 90, 300, 200);
    const aino::Rect inner_r(panel.x + 20, panel.bottom() - 52, 110, 32);

    auto draw = [&]() {
        if (ui.dialog("标题", "正文", &open, panel)) {
            if (ui.button("确定", inner_r)) ++inner;
        }
    };

    d.idle(draw);
    d.idle(draw);

    d.click(inner_r.cx(), inner_r.cy(), draw);
    check(inner == 1, "面板内按钮可点", "inner=" + std::to_string(inner));
    check(open, "点面板内部不关闭 dialog");

    // 面板内的按钮点完之后，遮罩仍然挡着（拿下层按钮验）
    d.idle(draw);
    d.idle(draw);
    d.move(500.0f, 380.0f, draw);
    check(ui.impl->next_hot == 0, "面板内点击后遮罩仍在挡");
}

// ---------------------------------------------------------------- 3. menu
static void test_menu() {
    std::printf("[3] menu：点外部关闭且不穿透\n");
    Surface s(640, 480);
    Harness h(&s);
    Driver d{h.ui, 640, 480};
    aino::Ui& ui = h.ui;

    int under = 0, choice = -1;
    const aino::Rect trig(20, 40, 120, 32);
    const aino::Rect other(400, 300, 120, 36);        // 远离触发器和下拉列表

    auto draw = [&]() {
        if (ui.button("下层", other)) ++under;
        ui.menu("菜单", {"一", "二", "三"}, &choice, trig);
    };

    d.click(trig.cx(), trig.cy(), draw);               // 展开
    const uint64_t mid = aino::detail::make_id("menu", trig);
    check(ui.impl->flags[mid] > 0.5f, "菜单已展开");

    d.idle(draw);
    d.idle(draw);
    d.move(other.cx(), other.cy(), draw);
    check(ui.impl->next_hot == 0, "菜单展开时下层按钮不获得 hover");

    d.click(other.cx(), other.cy(), draw);             // 点遮罩压住下层按钮
    check(under == 0, "菜单展开时点外部不穿透", "under=" + std::to_string(under));
    check(ui.impl->flags[mid] < 0.5f, "点外部关闭菜单");
    check(choice == -1, "没有误选中某一项", "choice=" + std::to_string(choice));
}

// ---------------------------------------------------------------- 4. select
static void test_select() {
    std::printf("[4] select：展开时点外部不穿透\n");
    Surface s(640, 480);
    Harness h(&s);
    Driver d{h.ui, 640, 480};
    aino::Ui& ui = h.ui;

    int under = 0, idx = 0;
    const aino::Rect sel(20, 40, 160, 32);
    const aino::Rect other(400, 300, 120, 36);

    auto draw = [&]() {
        if (ui.button("下层", other)) ++under;
        ui.select({"甲", "乙", "丙"}, &idx, sel);
    };

    d.click(sel.cx(), sel.cy(), draw);
    const uint64_t sid = aino::detail::make_id("select", sel);
    check(ui.impl->flags[sid] > 0.5f, "下拉已展开");

    d.idle(draw);
    d.idle(draw);
    d.move(other.cx(), other.cy(), draw);
    check(ui.impl->next_hot == 0, "下拉展开时下层按钮不获得 hover");

    d.click(other.cx(), other.cy(), draw);
    check(under == 0, "下拉展开时点外部不穿透", "under=" + std::to_string(under));
    check(ui.impl->flags[sid] < 0.5f, "点外部收起下拉");
    check(idx == 0, "没有误选中某一项", "idx=" + std::to_string(idx));
}

// ---------------------------------------------------------------- 5. 滚轮
static void test_wheel() {
    std::printf("[5] 浮层打开时滚轮不落到下层滚动区\n");
    Surface s(640, 480);
    Harness h(&s);
    Driver d{h.ui, 640, 480};
    aino::Ui& ui = h.ui;

    bool open = false;
    const aino::Rect area(20, 20, 150, 200);        // 滚动区
    const aino::Rect panel(300, 100, 260, 180);

    auto draw = [&]() {
        auto sc = ui.scroll(area, 800.0f);          // 内容 800 > 可视 200
        (void)sc;
        if (open) ui.dialog("标题", "正文", &open, panel);
    };

    d.idle(draw);
    const float o0 = ui.scroll_offset(area);
    d.wheel(area.cx(), area.cy(), -3.0f, draw);
    const float o1 = ui.scroll_offset(area);
    check(o1 > o0, "无浮层时滚轮生效",
          std::to_string(o0) + " -> " + std::to_string(o1));

    open = true;
    d.idle(draw);
    d.idle(draw);

    const float o2 = ui.scroll_offset(area);
    d.wheel(area.cx(), area.cy(), -3.0f, draw);
    const float o3 = ui.scroll_offset(area);
    check(o3 == o2, "浮层打开时滚轮被挡住",
          std::to_string(o2) + " -> " + std::to_string(o3));
}

// ------------------------------------------------ 6. 层级：面板底下的控件
// 只按位置放行（白名单）时，落在面板矩形里的一切都算"允许" —— 被面板盖住的
// 按钮照样 hover / click。这一节专门打这个洞。
static void test_z_order_under_panel() {
    std::printf("[6] 被面板盖住的控件不许响应（层级，不只是位置）\n");
    Surface s(640, 480);
    Harness h(&s);
    Driver d{h.ui, 640, 480};
    aino::Ui& ui = h.ui;

    bool open = true;
    int  under = 0;
    const aino::Rect panel(160, 90, 320, 240);
    // 完全在面板矩形**内部**：位置白名单会把它当自己人
    const aino::Rect under_r(panel.x + 30, panel.y + 30, 120, 36);

    auto draw = [&]() {
        if (ui.button("被盖住", under_r)) ++under;
        if (ui.dialog("标题", "正文", &open, panel)) {
            if (ui.button("关闭", {panel.x + 20, panel.bottom() - 44, panel.w - 40, 24}))
                open = false;
        }
    };

    d.idle(draw);
    d.idle(draw);

    d.move(under_r.cx(), under_r.cy(), draw);
    check(ui.impl->next_hot == 0, "面板底下的按钮不获得 hover",
          "next_hot=" + std::to_string(ui.impl->next_hot));
    check(ui.impl->cursor == aino::Cursor::Arrow, "面板底下不变小手");

    d.click(under_r.cx(), under_r.cy(), draw);
    check(under == 0, "点面板不穿透到盖住的按钮", "under=" + std::to_string(under));
    check(open, "点面板内部不关闭");

    // 无可见控件的右上角只属于面板，不能触发关闭。
    d.click(panel.right() - 18, panel.y + 18, draw);
    check(open, "对话框右上角没有隐藏关闭热区");
    d.click(panel.cx(), panel.bottom() - 32, draw);
    check(!open, "面板内可见关闭按钮照常可点");
}

// ------------------------------------------------ 7. 悬停类：tooltip / spinner
static void test_hover_side_effects() {
    std::printf("[7] 遮罩下不出 tooltip、spinner 不转\n");
    Surface s(640, 480);
    Harness h(&s);
    Driver d{h.ui, 640, 480};
    aino::Ui& ui = h.ui;

    bool open = false;
    const aino::Rect tip_anchor(30, 320, 90, 30);
    const aino::Rect spin_r(30, 380, 28, 28);
    const aino::Rect panel(300, 80, 260, 200);

    auto draw = [&]() {
        ui.tooltip(tip_anchor, "提示文字");
        ui.spinner(spin_r);                       // 默认只在悬停时转
        if (open) ui.dialog("标题", "正文", &open, panel);
    };
    const uint64_t spin_id = aino::detail::make_id("spinner_phase", spin_r);

    // 基线：没浮层时，悬停会出 tooltip、spinner 会转
    // tooltip 有 0.4 s 悬停延时，所以要先把时钟推过门槛再断言。指针位置跨帧
    // 保留（begin_frame 不动 Input），d.idle 不会把它丢掉。
    d.begin(tip_anchor.cx(), tip_anchor.cy());
    draw();
    check(ui.impl->defer.empty(), "悬停第一帧不出 tooltip（0.4 s 延时）");
    d.end();

    for (int i = 0; i < 30; ++i) d.idle(draw);          // 0.50 s

    d.begin(tip_anchor.cx(), tip_anchor.cy());
    draw();
    check(!ui.impl->defer.empty(), "悬停超过 0.4 s 出 tooltip");
    d.end();

    d.begin(spin_r.cx(), spin_r.cy());
    const float p0 = ui.impl->phase[spin_id];
    draw();
    const float p1 = ui.impl->phase[spin_id];
    check(p1 > p0, "无浮层时 spinner 悬停转动");
    d.end();

    open = true;
    d.idle(draw);
    d.idle(draw);

    d.begin(tip_anchor.cx(), tip_anchor.cy());
    draw();
    check(ui.impl->defer.empty(), "遮罩下不出 tooltip",
          "defer=" + std::to_string(ui.impl->defer.size()));
    d.end();

    d.begin(spin_r.cx(), spin_r.cy());
    const float p2 = ui.impl->phase[spin_id];
    draw();
    const float p3 = ui.impl->phase[spin_id];
    check(p3 == p2, "遮罩下 spinner 不转（也不烧 CPU）",
          std::to_string(p2) + " -> " + std::to_string(p3));
    d.end();
}

// ------------------------------------------------ 8. 键盘：焦点也要被挡住
static void test_keyboard() {
    std::printf("[8] 被盖住的输入框不吃键盘\n");
    Surface s(640, 480);
    Harness h(&s);
    Driver d{h.ui, 640, 480};
    aino::Ui& ui = h.ui;

    bool open = false;
    std::string text;
    const aino::Rect inp(180, 120, 200, 34);
    const aino::Rect panel(160, 90, 320, 240);   // 盖住输入框

    auto draw = [&]() {
        ui.input(&text, inp);
        if (open) ui.dialog("标题", "正文", &open, panel);
    };

    d.click(inp.cx(), inp.cy(), draw);           // 先聚焦
    const uint64_t editor_id = ui.impl->widget_id("input", inp, 0, uint64_t(reinterpret_cast<uintptr_t>(&text)));
    check(aino::detail::is_focused(ui.impl, editor_id),
          "点击后取得焦点");

    auto type = [&](const char* s) {
        ui.begin_frame(640, 480, 1.0f / 60.0f, 1.0f);
        ui.impl->input.text_input = s;
        draw();
        ui.end_frame();
    };
    type("a");
    check(text == "a", "无浮层时能打字", text);

    open = true;
    d.idle(draw);
    d.idle(draw);
    const std::string before = text;
    type("b");
    check(text == before, "浮层盖住后打字被忽略", before + " -> " + text);
    check(!aino::detail::is_focused(ui.impl, editor_id),
          "浮层盖住后焦点被交回");
}

// ------------------------------------------------ 9. 滚轮在面板范围内
static void test_wheel_inside_panel() {
    std::printf("[9] 滚轮落在面板上也不滚后面的列表\n");
    Surface s(640, 480);
    Harness h(&s);
    Driver d{h.ui, 640, 480};
    aino::Ui& ui = h.ui;

    bool open = false;
    const aino::Rect area(160, 90, 320, 240);     // 滚动区正好被面板盖住
    const aino::Rect panel(180, 110, 280, 200);

    auto draw = [&]() {
        auto sc = ui.scroll(area, 900.0f);
        (void)sc;
        if (open) ui.dialog("标题", "正文", &open, panel);
    };

    d.idle(draw);
    open = true;
    d.idle(draw);
    d.idle(draw);

    const float o0 = ui.scroll_offset(area);
    d.wheel(panel.cx(), panel.cy(), -3.0f, draw);
    const float o1 = ui.scroll_offset(area);
    check(o1 == o0, "面板上的滚轮不落到下层滚动区",
          std::to_string(o0) + " -> " + std::to_string(o1));
}

int main() {
    test_dialog_backdrop();
    test_dialog_inner();
    test_menu();
    test_select();
    test_wheel();
    test_z_order_under_panel();
    test_hover_side_effects();
    test_keyboard();
    test_wheel_inside_panel();
    std::printf("通过 %d 项，失败 %d 项\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
