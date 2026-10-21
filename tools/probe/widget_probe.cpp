// 0.3.0 新补的 shadcn 控件 —— 行为断言
//
// 为什么单独写一个探针（而不是把断言塞进 demo）：
//   demo 是给人看的，改了布局就得改断言；探针只管"这个控件点下去到底改没改值"，
//   布局换多少次都不影响。两者分工不同。
//
// 全部离屏（自建 32bpp DIB + 内存 DC），靠直接写 impl->input 驱动，
// 不开窗口、秒级完成。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cstdio>
#include <functional>
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
    // 逻辑坐标 → 像素（本探针 scale 恒为 1）
    aino::Color at(float x, float y) const {
        const int ix = int(x), iy = int(y);
        if (ix < 0 || iy < 0 || ix >= w || iy >= h) return aino::Color(0, 0, 0, 0);
        const uint8_t* q = cv.px + size_t(iy) * size_t(cv.stride) + size_t(ix) * 4u;
        return aino::Color(q[aino::detail::CH_R], q[aino::detail::CH_G],
                           q[aino::detail::CH_B], q[aino::detail::CH_A]);
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
// 一次"点击"必须两帧：按下帧 + 松开帧。interact() 的 clicked 只在松开帧成立。
struct Driver {
    aino::Ui& ui;
    int w = 0, h = 0;

    void frame(const std::function<void()>& draw) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        draw();
        ui.end_frame();
    }
    void idle(const std::function<void()>& draw) { frame(draw); }

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

    void right_click(float x, float y, const std::function<void()>& draw) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        ui.impl->input.mouse = aino::Vec2(x, y);
        ui.impl->input.down[1] = true;
        ui.impl->input.pressed[1] = true;
        draw();
        ui.end_frame();

        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        ui.impl->input.mouse = aino::Vec2(x, y);
        ui.impl->input.down[1] = false;
        ui.impl->input.released[1] = true;
        draw();
        ui.end_frame();
    }

    void move(float x, float y, const std::function<void()>& draw) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        ui.impl->input.mouse = aino::Vec2(x, y);
        draw();
        ui.end_frame();
    }

    // 一帧里塞一段键盘输入
    void type(const std::string& text, const std::function<void()>& draw) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        ui.impl->input.text_input = text;
        draw();
        ui.end_frame();
    }

    void key(int k, const std::function<void()>& draw) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        ui.impl->input.key_pressed[k] = true;
        draw();
        ui.end_frame();
    }

    // 拖拽：按下 → 移动（保持 down）→ 抬起
    void drag(float x0, float y0, float x1, float y1, const std::function<void()>& draw) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        ui.impl->input.mouse = aino::Vec2(x0, y0);
        ui.impl->input.down[0] = true;
        ui.impl->input.pressed[0] = true;
        draw();
        ui.end_frame();

        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        ui.impl->input.mouse = aino::Vec2(x1, y1);
        ui.impl->input.down[0] = true;
        draw();
        ui.end_frame();

        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        ui.impl->input.mouse = aino::Vec2(x1, y1);
        ui.impl->input.down[0] = false;
        ui.impl->input.released[0] = true;
        draw();
        ui.end_frame();
    }

    // 读"本帧登记了几个延迟绘制"必须在 begin/end 之间（end_frame 会执行并清空）
    int defers(const std::function<void()>& draw) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        draw();
        const int n = int(ui.impl->defer.size());
        ui.end_frame();
        return n;
    }
};

using aino::Rect;

// ---------------------------------------------------------------- 1. Toggle
static void test_toggle() {
    std::printf("[1] Toggle\n");
    Surface s(360, 200);
    Harness h(&s);
    Driver d{h.ui, 360, 200};
    aino::Ui& ui = h.ui;

    bool v = false;
    const Rect box(20.0f, 20.0f, 100.0f, 32.0f);
    auto draw = [&]() { ui.toggle_button("粗体", &v, box); };

    d.click(box.cx(), box.cy(), draw);
    check(v, "点击后变成 true");

    // 选中态必须有底色：否则"按下去"这个动作在视觉上是无痕的
    d.idle(draw);
    const aino::Color mid = s.at(box.x + 6.0f, box.cy());
    check(mid.r < 250 || mid.g < 250 || mid.b < 250, "选中态有底色（不是纯白）",
          std::to_string(mid.r) + "," + std::to_string(mid.g) + "," + std::to_string(mid.b));

    d.click(box.cx(), box.cy(), draw);
    check(!v, "再点变回 false");

    // 点框外不该翻状态
    d.click(box.right() + 40.0f, box.cy(), draw);
    check(!v, "点框外不改变状态");
}

// ---------------------------------------------------------------- 2. Toggle Group
static void test_toggle_group() {
    std::printf("[2] Toggle Group\n");
    Surface s(360, 200);
    Harness h(&s);
    Driver d{h.ui, 360, 200};
    aino::Ui& ui = h.ui;

    int idx = 0;
    const Rect box(20.0f, 20.0f, 240.0f, 34.0f);
    const std::vector<std::string> items = {"日", "周", "月"};
    auto draw = [&]() { ui.toggle_group(items, &idx, box); };

    d.click(box.x + 40.0f, box.cy(), draw);
    check(idx == -1, "点已选中项 → 取消（-1）", "idx=" + std::to_string(idx));

    d.click(box.x + 120.0f, box.cy(), draw);
    check(idx == 1, "点第二项 → 1", "idx=" + std::to_string(idx));

    d.click(box.x + 200.0f, box.cy(), draw);
    check(idx == 2, "点第三项 → 2", "idx=" + std::to_string(idx));
}

// ---------------------------------------------------------------- 3. Button Group
static void test_button_group() {
    std::printf("[3] Button Group\n");
    Surface s(360, 200);
    Harness h(&s);
    Driver d{h.ui, 360, 200};
    aino::Ui& ui = h.ui;

    int hit = -1;
    const Rect box(20.0f, 20.0f, 240.0f, 34.0f);
    const std::vector<std::string> items = {"复制", "粘贴", "剪切"};
    auto draw = [&]() { ui.button_group(items, &hit, box); };

    d.click(box.x + 120.0f, box.cy(), draw);
    check(hit == 1, "点第二格 → 1", "hit=" + std::to_string(hit));

    d.click(box.x + 40.0f, box.cy(), draw);
    check(hit == 0, "点第一格 → 0", "hit=" + std::to_string(hit));
}

// ---------------------------------------------------------------- 4. Input Group
static void test_input_group() {
    std::printf("[4] Input Group\n");
    Surface s(360, 200);
    Harness h(&s);
    Driver d{h.ui, 360, 200};
    aino::Ui& ui = h.ui;

    std::string v;
    const Rect box(20.0f, 20.0f, 240.0f, 36.0f);
    auto draw = [&]() { ui.input_group(&v, "¥", "元", box); };

    d.click(box.cx(), box.cy(), draw);      // 聚焦
    d.type("128", draw);
    check(v == "128", "能输入（前缀后缀不挡字符）", "v=" + v);

    // 前后缀区域不该把点击"吃掉"后什么都不发生 —— 这里断言的是
    // 整块 box 都能点进输入区（shadcn 的 Input Group 就是这个体验）
    d.click(box.right() - 8.0f, box.cy(), draw);
    d.type("9", draw);
    check(v == "1289", "点后缀附近仍然在输入框里", "v=" + v);
}

// ---------------------------------------------------------------- 5. Input OTP
static void test_otp() {
    std::printf("[5] Input OTP\n");
    Surface s(360, 200);
    Harness h(&s);
    Driver d{h.ui, 360, 200};
    aino::Ui& ui = h.ui;

    std::string v;
    const Rect box(20.0f, 20.0f, 300.0f, 44.0f);
    auto draw = [&]() { ui.input_otp(&v, 6, box); };

    d.click(box.cx(), box.cy(), draw);      // 聚焦
    d.type("12a3", draw);
    check(v == "123", "只收数字", "v=" + v);

    d.key(aino::KEY_BACKSPACE, draw);
    check(v == "12", "退格删一位", "v=" + v);

    d.type("4567890", draw);
    check(v.size() == 6, "超过格数被截断", "size=" + std::to_string(v.size()));
    check(v == "124567", "内容按顺序填入、多的丢掉", "v=" + v);
}

// ---------------------------------------------------------------- 6. Field
static void test_field() {
    std::printf("[6] Field\n");
    Surface s(360, 240);
    Harness h(&s);
    Driver d{h.ui, 360, 240};
    aino::Ui& ui = h.ui;

    const float ctrl_h = 36.0f;
    const float want = ui.field_height("邮箱", "不会公开", "", ctrl_h);
    float got_h = -1.0f;
    const Rect box(20.0f, 20.0f, 240.0f, want);

    auto draw = [&]() {
        ui.field("邮箱", "不会公开", "", box, [&](const Rect& r) {
            got_h = r.h;
            ui.panel(r, aino::Style().background(aino::Color(240, 240, 240)));
        });
    };
    d.idle(draw);

    check(want > ctrl_h, "field_height 把标签和说明算进去了",
          "want=" + std::to_string(want) + " ctrl=" + std::to_string(ctrl_h));
    check(got_h > 0.0f && std::fabs(got_h - ctrl_h) < 1.5f,
          "控件槽高度 == 传入的 control_h",
          "got=" + std::to_string(got_h) + " want=" + std::to_string(ctrl_h));
}

// ---------------------------------------------------------------- 7. Item
static void test_item() {
    std::printf("[7] Item\n");
    Surface s(360, 200);
    Harness h(&s);
    Driver d{h.ui, 360, 200};
    aino::Ui& ui = h.ui;

    int hits = 0;
    const Rect box(20.0f, 20.0f, 260.0f, 52.0f);
    auto draw = [&]() { if (ui.item("张伟", "a@b.com", "管理员", box)) ++hits; };

    d.click(box.cx(), box.cy(), draw);
    check(hits == 1, "整行可点（不只是文字区）", "hits=" + std::to_string(hits));

    d.click(box.x + 2.0f, box.y + 2.0f, draw);
    check(hits == 2, "左上角也在命中区内", "hits=" + std::to_string(hits));
}

// ---------------------------------------------------------------- 8. Pagination
static void test_pagination() {
    std::printf("[8] Pagination\n");
    Surface s(420, 200);
    Harness h(&s);
    Driver d{h.ui, 420, 200};
    aino::Ui& ui = h.ui;

    int page = 2;
    const Rect box(10.0f, 20.0f, 320.0f, 32.0f);
    auto draw = [&]() { ui.pagination(&page, 20, box); };

    d.idle(draw);
    // total=20、page=2 时槽位固定 7 个：< 1 … 3 … 20 >
    // 整组在 box 里居中，起点要加上 (box.w - 7*sw)/2
    const float sw = 32.0f;
    const float x0 = box.x + (box.w - sw * 7.0f) * 0.5f;
    auto slot = [&](int i) { return x0 + sw * (float(i) + 0.5f); };

    d.click(slot(0), box.cy(), draw);                        // 上一页
    check(page == 1, "上一页 → 1", "page=" + std::to_string(page));

    d.click(slot(1), box.cy(), draw);                        // 页码 1
    check(page == 0, "点第 1 页 → 0（内部 0 基）", "page=" + std::to_string(page));

    d.click(slot(0), box.cy(), draw);                        // 已到第一页
    check(page == 0, "第一页再按上一页不越界", "page=" + std::to_string(page));

    d.click(slot(5), box.cy(), draw);                        // 最后一页（20）
    check(page == 19, "点最后一页 → 19", "page=" + std::to_string(page));

    d.click(slot(6), box.cy(), draw);                        // 下一页
    check(page == 19, "末页再按下一页不越界", "page=" + std::to_string(page));
}

// ---------------------------------------------------------------- 9. Calendar
static void test_calendar() {
    std::printf("[9] Calendar\n");
    // 纯日期运算先单独验：闰年/星期算错的话，格子整体会偏一格
    check(aino::detail::days_in_month(2024, 2) == 29, "2024-02 有 29 天");
    check(aino::detail::days_in_month(2026, 2) == 28, "2026-02 有 28 天");
    check(aino::detail::weekday_of(2026, 10, 22) == 4, "2026-10-22 是周四");
    check(aino::detail::weekday_of(2026, 10, 1) == 4, "2026-10-01 是周四");

    Surface s(320, 260);
    Harness h(&s);
    Driver d{h.ui, 320, 260};
    aino::Ui& ui = h.ui;

    int y = 2026, m = 10, day = 15;
    const Rect box(10.0f, 10.0f, 280.0f, 240.0f);
    auto draw = [&]() { ui.calendar(&y, &m, &day, box); };

    d.idle(draw);
    // 头部高 42，星期行 26 → 网格从 78 起；格 40×27
    const float cw = 280.0f / 7.0f, ch = (240.0f - 68.0f) / 6.0f;
    const float gx = box.x + cw * 4.5f;             // 周四
    const float gy = box.y + 68.0f + ch * 0.5f;     // 第 1 行
    d.click(gx, gy, draw);
    check(day == 1, "点第 1 行第 5 列 → 1 号", "day=" + std::to_string(day));

    // 翻到下个月
    const Rect next_r(box.right() - 42.0f * 0.7f - 2.0f, box.y + 6.0f, 42.0f * 0.7f, 42.0f * 0.7f);
    d.click(next_r.cx(), next_r.cy(), draw);
    check(m == 11, "点右箭头 → 11 月", "m=" + std::to_string(m));

    const Rect prev_r(box.x + 2.0f, box.y + 6.0f, 42.0f * 0.7f, 42.0f * 0.7f);
    d.click(prev_r.cx(), prev_r.cy(), draw);
    check(m == 10, "点左箭头 → 回到 10 月", "m=" + std::to_string(m));

    // 跨年：从 2026-1 往前翻 → 2025-12
    y = 2026; m = 1;
    d.idle(draw);
    d.click(prev_r.cx(), prev_r.cy(), draw);
    check(y == 2025 && m == 12, "1 月往前翻 → 上一年 12 月",
          std::to_string(y) + "-" + std::to_string(m));
}

// ---------------------------------------------------------------- 10. Popover
static void test_popover() {
    std::printf("[10] Popover\n");
    Surface s(400, 300);
    Harness h(&s);
    Driver d{h.ui, 400, 300};
    aino::Ui& ui = h.ui;

    bool open = false;
    int  content_drawn = 0;
    int  clicks_under = 0;
    const Rect trig(20.0f, 20.0f, 100.0f, 32.0f);
    const Rect under(20.0f, 200.0f, 100.0f, 32.0f);   // 面板外的按钮

    auto draw = [&]() {
        if (ui.button("下层", under)) ++clicks_under;
        ui.popover("排序", &open, trig, aino::Vec2(160.0f, 80.0f), [&](const Rect& c) {
            ++content_drawn;
            ui.label("内容", c);
        });
    };

    d.idle(draw);
    d.click(trig.cx(), trig.cy(), draw);
    check(open, "点触发器 → 打开");

    const int before = content_drawn;
    const int defer_n = d.defers(draw);
    check(content_drawn > before, "内容回调被调用");
    check(defer_n > 0, "面板走 deferred（帧末绘制）", "defers=" + std::to_string(defer_n));

    const int under_before = clicks_under;
    d.click(under.cx(), under.cy(), draw);
    check(!open, "点面板外 → 关闭");
    check(clicks_under == under_before, "关闭的那一下不穿透到下层按钮",
          std::to_string(clicks_under - under_before) + " 次");
}

// ---------------------------------------------------------------- 11. Alert Dialog
static void test_alert_dialog() {
    std::printf("[11] Alert Dialog\n");
    Surface s(500, 360);
    Harness h(&s);
    Driver d{h.ui, 500, 360};
    aino::Ui& ui = h.ui;

    bool open = true;
    bool confirmed = false;
    const Rect box(80.0f, 90.0f, 340.0f, 160.0f);

    auto draw = [&]() {
        confirmed = ui.alert_dialog("删除？", "删了就没了", "删除", "取消", &open, box,
                                    aino::Style().destructive());
    };

    d.idle(draw);
    check(open, "初始是打开态");

    const float pad = 24.0f, btn_h = 36.0f;
    const float ok_w = ui.text_width("删除", 14.0f, 500) + 32.0f;
    const Rect ok_r(box.right() - pad - ok_w, box.bottom() - pad - btn_h, ok_w, btn_h);
    d.click(ok_r.cx(), ok_r.cy(), draw);
    check(confirmed, "点确认 → 返回 true");
    check(!open, "确认后自动关闭");

    // 取消
    open = true;
    auto draw2 = [&]() {
        confirmed = ui.alert_dialog("删除？", "删了就没了", "删除", "取消", &open, box,
                                    aino::Style().destructive());
    };
    d.idle(draw2);
    const float no_w = ui.text_width("取消", 14.0f, 500) + 32.0f;
    const Rect no_r(ok_r.x - 8.0f - no_w, ok_r.y, no_w, btn_h);
    d.click(no_r.cx(), no_r.cy(), draw2);
    check(!confirmed && !open, "点取消 → 不确认，但关闭");

    // 点遮罩
    open = true;
    d.idle(draw2);
    d.click(10.0f, 10.0f, draw2);
    check(!open, "点遮罩 → 关闭");
}

// ---------------------------------------------------------------- 12. Drawer
static void test_drawer() {
    std::printf("[12] Drawer\n");
    Surface s(500, 360);
    Harness h(&s);
    Driver d{h.ui, 500, 360};
    aino::Ui& ui = h.ui;

    bool open = true;
    int  drawn = 0;
    const Rect panel(200.0f, 0.0f, 300.0f, 360.0f);

    auto draw = [&]() {
        ui.drawer("详情", &open, 0, panel, [&](const Rect& c) {
            ++drawn;
            ui.label("内容", c);
        });
    };

    d.idle(draw);
    check(open, "打开态");
    check(drawn > 0, "内容回调被调用");

    // 点遮罩（面板左边）关闭
    d.click(40.0f, 180.0f, draw);
    check(!open, "点遮罩 → 关闭");

    // 关闭后不再调内容
    const int before = drawn;
    d.idle(draw);
    check(drawn == before, "关闭后不再绘制内容");
}

// ---------------------------------------------------------------- 13. Context Menu
static void test_context_menu() {
    std::printf("[13] Context Menu\n");
    Surface s(400, 300);
    Harness h(&s);
    Driver d{h.ui, 400, 300};
    aino::Ui& ui = h.ui;

    int chosen = -1;
    const Rect area(40.0f, 40.0f, 200.0f, 120.0f);
    const std::vector<std::string> items = {"标记已读", "归档", "删除"};
    auto draw = [&]() { ui.context_menu(area, items, &chosen, aino::Style()); };

    d.idle(draw);
    check(chosen == -1, "初始没有选中");

    // 区域外右键不该弹
    d.right_click(10.0f, 10.0f, draw);
    const int flag_off = int(ui.impl->flags[aino::detail::make_id("ctxmenu", area, uint64_t(items.size()))] + 0.5f);
    check(flag_off == 0, "区域外右键不弹出");

    d.right_click(100.0f, 100.0f, draw);
    const int flag_on = int(ui.impl->flags[aino::detail::make_id("ctxmenu", area, uint64_t(items.size()))] + 0.5f);
    check(flag_on == 1, "区域内右键 → 弹出");

    // 列表从点击处开始，item_h = max(fs+16, 32) = 32，内边距 4
    const float ih = 32.0f;
    d.click(100.0f + 60.0f, 100.0f + 4.0f + ih * 1.5f, draw);
    check(chosen == 1, "点第二项 → 1", "chosen=" + std::to_string(chosen));
}

// ---------------------------------------------------------------- 14. Combobox
static void test_combobox() {
    std::printf("[14] Combobox\n");
    Surface s(400, 320);
    Harness h(&s);
    Driver d{h.ui, 400, 320};
    aino::Ui& ui = h.ui;

    int idx = -1;
    const Rect box(20.0f, 20.0f, 200.0f, 36.0f);
    const std::vector<std::string> items = {"C++", "Rust", "Go", "Zig"};
    auto draw = [&]() { ui.combobox(items, &idx, box); };

    d.idle(draw);
    d.click(box.cx(), box.cy(), draw);
    const int opened = int(ui.impl->flags[aino::detail::make_id("combobox", box)] + 0.5f);
    check(opened == 1, "点触发器 → 展开");

    // 打字过滤到只剩 Rust / (Go 含 'u'? 不) —— "Ru" 只匹配 Rust
    d.type("Ru", draw);
    const std::string q = ui.impl->strings[aino::detail::make_id("combobox", box)];
    check(q == "Ru", "搜索词记下来了", "q=" + q);

    d.key(aino::KEY_ENTER, draw);
    check(idx == 1, "回车选中过滤后的第一项（Rust）", "idx=" + std::to_string(idx));
    const int closed = int(ui.impl->flags[aino::detail::make_id("combobox", box)] + 0.5f);
    check(closed == 0, "选中后自动收起");
    const std::string q2 = ui.impl->strings[aino::detail::make_id("combobox", box)];
    check(q2.empty(), "收起后搜索词被清空", "q=" + q2);
}

// ---------------------------------------------------------------- 15. Split Pane
static void test_split_pane() {
    std::printf("[15] Split Pane\n");
    Surface s(400, 240);
    Harness h(&s);
    Driver d{h.ui, 400, 240};
    aino::Ui& ui = h.ui;

    float ratio = 0.5f;
    Rect a, b;
    const Rect box(20.0f, 20.0f, 320.0f, 160.0f);
    auto draw = [&]() { ui.split_pane(&ratio, box, &a, &b, true, 0.2f); };

    d.idle(draw);
    check(std::fabs(a.w + b.w + 7.0f - box.w) < 1.0f, "左右两块 + 分隔条 == 总宽",
          std::to_string(a.w) + "+" + std::to_string(b.w));

    d.drag(box.x + box.w * 0.5f, box.cy(), box.x + box.w * 0.75f, box.cy(), draw);
    check(std::fabs(ratio - 0.75f) < 0.02f, "拖动后 ratio ≈ 0.75",
          "ratio=" + std::to_string(ratio));

    // 拖到最左也不许越界（min_ratio=0.2）
    d.drag(box.x + box.w * ratio, box.cy(), box.x - 100.0f, box.cy(), draw);
    check(ratio >= 0.199f, "拖出左边界被夹在 min_ratio",
          "ratio=" + std::to_string(ratio));
}

// ---------------------------------------------------------------- 15.5 Hover Card
static void test_hover_card() {
    std::printf("[15.5] Hover Card\n");
    Surface s(400, 300);
    Harness h(&s);
    Driver d{h.ui, 400, 300};
    aino::Ui& ui = h.ui;

    int drawn = 0;
    const Rect anchor(40.0f, 40.0f, 160.0f, 52.0f);
    auto draw = [&]() {
        ui.button("锚点", anchor);
        ui.hover_card(anchor, aino::Vec2(120.0f, 60.0f), [&](const Rect& c) {
            ++drawn;
            ui.label("卡片", c);
        });
    };

    d.idle(draw);
    check(drawn == 0, "鼠标不在锚点上 → 不画");

    d.move(anchor.cx(), anchor.cy(), draw);
    check(drawn > 0, "悬停 → 调用内容回调", "drawn=" + std::to_string(drawn));

    const int n = d.defers(draw);      // 这一帧鼠标还在锚点上
    check(n > 0, "卡片走 deferred", "defers=" + std::to_string(n));

    // 移开就消失
    const int before = drawn;
    d.move(350.0f, 260.0f, draw);
    d.idle(draw);
    check(drawn == before, "鼠标移开 → 不再画");
}

// ---------------------------------------------------------------- 16. 不越界
// 新控件最怕的 bug 是"画到 box 外面"：肉眼在空背景上看不出来，
// 一旦放进卡片/侧栏就会糊到邻居身上。这里对三个控件各验一次。
static void test_no_overflow() {
    std::printf("[16] 绘制不越界\n");
    Surface s(420, 260);
    Harness h(&s);
    Driver d{h.ui, 420, 260};
    aino::Ui& ui = h.ui;

    const Rect box(60.0f, 60.0f, 280.0f, 120.0f);
    const aino::Color white(255, 255, 255, 255);
    auto blank = [&](const std::function<void()>& draw) {
        ui.clear(white);
        draw();
    };

    auto outside_clean = [&](const char* what) {
        // 只看 box 右侧 3px 外的一条竖线，左/上/下同理省略（对称）
        bool clean = true;
        for (float y = box.y; y < box.bottom(); y += 4.0f) {
            const aino::Color c = s.at(box.right() + 2.0f, y);
            if (c.r != 255 || c.g != 255 || c.b != 255) { clean = false; break; }
        }
        check(clean, what, "box 右侧 2px 处被画到了");
    };

    int idx = 0;
    const std::vector<std::string> items = {"日", "周", "月"};

    d.idle([&]() { blank([&]() { ui.toggle_group(items, &idx, box); }); });
    outside_clean("toggle_group 不越界");

    int page = 3;
    d.idle([&]() { blank([&]() { ui.pagination(&page, 20, box); }); });
    outside_clean("pagination 不越界");

    d.idle([&]() { blank([&]() { ui.empty_state("空的", "没有内容", box); }); });
    outside_clean("empty_state 不越界");
}

int main() {
    std::printf("== 0.3.0 新控件行为断言 ==\n");
    test_toggle();
    test_toggle_group();
    test_button_group();
    test_input_group();
    test_otp();
    test_field();
    test_item();
    test_pagination();
    test_calendar();
    test_popover();
    test_alert_dialog();
    test_drawer();
    test_context_menu();
    test_combobox();
    test_split_pane();
    test_hover_card();
    test_no_overflow();
    std::printf("=== 通过 %d 项，失败 %d 项 ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
