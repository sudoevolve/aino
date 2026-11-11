// 文档片段的编译检查；运行时不创建窗口。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <string>
#include <vector>

namespace {

using aino::Color;
using aino::Rect;
using aino::Stack;
using aino::Style;
using aino::Ui;

// ---- 各篇文档里用到的应用状态（拼成一个）----
struct AppState {
    std::string email, password, search, note, api_key;
    bool remember = true, autosave = false, notify = true, dry_run = false;
    bool advanced_open = false, confirm_open = true;
    float zoom = 1.0f, count = 12.0f, ratio = 0.5f;
    int range = 0, tab = 0, choice = 0, idx = 0, stars = 3, clicks = 0;
    float toast_left = 0.0f;
    std::string last_action;
    void save() {}
};

// ---- 01-quickstart：最小程序 ----
void docs_01() {
    aino::Window win(880, 560, "aino 示例");
    if (!win.ok()) return;

    std::string email;
    bool notify = true;

    win.run([&](aino::Ui& ui) {
        ui.panel({24, 24, 400, 240});
        aino::Stack col = ui.vbox({24, 24, 400, 240}, 10.0f, 18.0f);
        ui.heading("设置", col.next(34.0f), aino::Style().left());
        ui.input(&email, col.next(24.0f), aino::Style().hint("you@example.com"));
        ui.checkbox(&notify, "邮件通知", col.next(22.0f));
        if (ui.button("保存", col.next(24.0f), aino::Style().primary())) { /* 点了保存 */ }
    });
}

// ---- 01-quickstart：窗口控制与主题 ----
void docs_01_window() {
    aino::Window win(1120, 720, "标题");
    win.ok(); win.ui(); win.set_title("新标题"); win.set_size(1280, 800);
    (void)win.width(); (void)win.height(); (void)win.dpi_scale();
    win.set_anim_fps(30.0f);
    win.request_repaint();

    aino::Theme t = aino::Theme::light();
    t.set_tint(t.purple);
    t.radius = 4.0f;
    win.set_theme(t);

    win.run([&](aino::Ui& ui) {
        const aino::Theme& th = ui.theme();
        ui.fill_rect({0, 0, ui.width(), ui.height()}, th.background);
    });
    win.close();
}

// ---- 02：Color / Rect ----
void docs_02() {
    Color a = Color::hex(0x18181B);
    Color b = Color::rgba(24, 24, 27, 200);
    (void)Color::none(); (void)Color::white(); (void)Color::black(); (void)Color::transparent();
    (void)a.is_none(); (void)a.with_alpha(128); (void)a.scaled_alpha(0.55f); (void)a.alpha255(200);
    (void)a.mix(b, 0.5f); (void)a.lighten(0.1f); (void)a.darken(0.1f);
    (void)a.luminance(); (void)a.rgb24(); (void)(a == b); (void)(a != b);

    Rect r(10, 20, 100, 40);
    (void)Rect::from_ltrb(0, 0, 10, 10); (void)Rect::centered(5, 5, 10, 10);
    (void)r.left(); (void)r.top(); (void)r.right(); (void)r.bottom();
    (void)r.cx(); (void)r.cy(); (void)r.empty();
    (void)r.contains(1.0f, 2.0f); (void)r.contains(aino::Vec2(1, 2));
    (void)r.intersects(Rect()); (void)r.inset(2.0f); (void)r.inset(2.0f, 3.0f);
    (void)r.offset(1.0f, 1.0f); (void)r.offset(aino::Vec2(1, 1)); (void)r.expand(2.0f);
    (void)r.pad(1.0f, 1.0f);
    Rect m = r;
    (void)m.cut_left(5.0f); (void)m.cut_right(5.0f); (void)m.cut_top(5.0f); (void)m.cut_bottom(5.0f);
}

// ---- 03：布局 ----
void docs_03(Ui& ui, AppState& s) {
    const Rect page = {292, 92, ui.width() - 292.0f - 32.0f, ui.height() - 92.0f - 24.0f};
    ui.panel(page);

    Stack col = ui.vbox(page, 14.0f, 24.0f);
    ui.heading("标题", col.next(34.0f), Style().left());
    ui.muted_text("说明文字", col.next(20.0f), Style().left());
    ui.separator(col.next(1.0f));

    Rect left = col.part(0, 2);
    Rect right = col.part(1, 2);
    Stack lc = ui.vbox(left, 10.0f);
    Stack rc = ui.vbox(right, 10.0f);
    ui.input(&s.email, lc.next(36.0f));
    ui.toggle(&s.notify, rc.next(26.0f));

    ui.button("保存", col.fill(), Style().primary());

    // 嵌套写法
    Stack root = ui.hbox({0, 0, 800, 500}, 0.0f);
    Stack side = root.vbox(200.0f);
    ui.nav_item("概览", true, side.next(34.0f));
    Stack main = root.vbox(0.0f);
    ui.heading("标题", main.next(40.0f));

    // 滚动
    Rect viewport = {24, 24, 300, 200};
    ui.begin_scroll(viewport, 800.0f);
    Stack sc = ui.vbox({viewport.x, viewport.y, viewport.w, 800.0f}, 10.0f);
    for (int i = 0; i < 20; ++i) ui.label("行", sc.next(28.0f));
    ui.end_scroll();
    (void)ui.scroll_y();
    ui.scroll_to(0.0f);

    // 滚动（统一写法：作用域即视口，析构自动收尾）
    {
        auto sc = ui.scroll(viewport, 800.0f);
        Stack c2 = ui.vbox({viewport.x, viewport.y, viewport.w, 800.0f}, 10.0f);
        for (int i = 0; i < 20; ++i) ui.label("行", c2.next(28.0f));
        (void)sc.y();
        sc.to(0.0f);
        sc.content(900.0f);
        (void)sc.offset;
    }

    // 裁剪
    ui.push_clip(viewport);
    ui.pop_clip();

    // 高度量法
    std::string long_text = "很长的一段中文，用来验证折行后的高度测量。";
    float h = ui.text_height(long_text, 380.0f, Style().wrap_text());
    ui.label(long_text, col.next(h), Style().wrap_text());
}

// ---- 04：文本 ----
void docs_04(Ui& ui, Stack& col) {
    ui.label("正文内容", {24, 24, 320, 22}, Style().left());
    ui.label("次要说明", {24, 50, 320, 20}, Style().muted_text());
    ui.heading("概览", col.next(34.0f), Style().left());
    ui.subheading("文本", col.next(26.0f), Style().left());
    ui.muted_text("单头文件", col.next(18.0f), Style().font(11.0f).left());
    ui.code_text("aino.hpp · 0.1.0", col.next(30.0f), Style().font(13.0f));
    ui.code_text(std::to_string(42), col.next(30.0f));
}

// ---- 05：容器 ----
void statCard(Ui& ui, const Rect& r, const char* title, const char* value) {
    ui.panel(r);
    Stack c = ui.vbox(r, 8.0f, 16.0f);
    ui.muted_text(title, c.next(18.0f), Style().font(12.0f).left());
    ui.heading(value, c.next(30.0f), Style().font(22.0f).left());
    ui.progress(0.62f, c.fill());
}

void docs_05(Ui& ui, Stack& r) {
    ui.panel({24, 24, 400, 240});
    ui.panel({24, 24, 400, 60}, Style().muted_text());
    ui.panel({24, 24, 400, 60}, Style().corner(0).stroke(0, Color::none()));
    ui.card({24, 24, 400, 240});
    ui.separator(r.next(1.0f));
    ui.separator({300, 24, 1, 200});
    ui.badge("New", r.next(52.0f));
    ui.badge("失败", r.next(52.0f), Style().destructive());
    ui.progress(0.68f, r.next(8.0f));
    ui.progress(0.32f, r.next(8.0f), Style().background(Color::hex(0xEF4444)));
    ui.skeleton(r.next(16.0f));
    ui.skeleton(r.next(16.0f), Style().animate());
    statCard(ui, {24, 300, 180, 120}, "渲染帧", "6.4ms");
}

// ---- 06：交互 ----
void docs_06(Ui& ui, AppState& s, Stack& col) {
    ui.button("保存", col.next(24.0f), Style().primary());
    ui.button("取消", col.next(24.0f), Style().outline());
    ui.button("删除", col.next(24.0f), Style().destructive());
    ui.button("按我看看", col.next(24.0f), Style().primary().scale_on_press(0.97f));
    ui.icon_button(aino::icons::close, {100, 100, 32, 32});
    ui.icon_button("+", {140, 100, 32, 32}, Style().outline());
    if (ui.nav_item("导航", s.tab == 0, col.next(34.0f))) s.tab = 0;

    ui.input(&s.email, col.next(24.0f), Style().hint("you@example.com"));
    ui.checkbox(&s.remember, "记住这台设备", col.next(22.0f));
    Rect row = col.next(26.0f);
    ui.label("桌面通知", row, Style().left());
    ui.toggle(&s.notify, {row.right() - 44.0f, row.y, 44.0f, 26.0f});

    ui.slider(&s.zoom, 0.5f, 2.0f, col.next(20.0f));
    ui.tabs({"概览", "按钮", "表单"}, &s.tab, col.next(38.0f));
    ui.segmented({"日", "周", "月"}, &s.range, col.next(34.0f));
    ui.select({"一", "二", "三"}, &s.idx, col.next(24.0f));
    ui.radio_group({"选项一", "选项二", "选项三"}, &s.choice, col.next(100.0f));
    ui.stepper(&s.count, 1.0f, 0.0f, 99.0f, col.next(24.0f));
    ui.textarea(&s.note, col.next(92.0f), Style().font(13.0f));
    ui.rating(&s.stars, 5, col.next(28.0f));
}

// ---- 07：展示 ----
void metricRow(Ui& ui, const Rect& area, const std::vector<float>& trend) {
    Stack h = ui.hbox(area, 16.0f);
    const Rect left  = h.next(240.0f);
    const Rect mid   = h.next(200.0f);
    const Rect right = h.fill();

    ui.avatar("张", {left.x, left.y, 40, 40});
    Stack t = ui.vbox({left.x + 52.0f, left.y, 188.0f, 40.0f}, 4.0f);
    ui.label("周活跃用户", t.next(20.0f), Style().left());
    ui.breadcrumb({"分析", "用户", "周活"}, t.next(18.0f));

    Stack b = ui.vbox({mid.x, mid.y, 180.0f, 40.0f}, 2.0f);
    ui.heading("1,284", b.next(24.0f), Style().font(20.0f).left());
    ui.bars(trend, b.fill());

    ui.badge("+12.4%", {right.x, right.y + 8.0f, 70.0f, 24.0f});
}

void docs_07(Ui& ui, AppState& s, Stack& col) {
    ui.avatar("张伟", {24, 24, 48, 48});
    ui.avatar("李", {24, 24, 48, 48}, Style().stroke(2, Color::hex(0xFFFFFF)));
    ui.spinner({100, 100, 20, 20});
    ui.spinner({130, 100, 20, 20}, Style().background(Color::hex(0x16A34A)));

    const char* t = "提示";
    const char* d = "这是一条比较长的说明文字，用来验证正文在给定宽度下的折行与裁剪行为。";
    const Style st = Style().font(13.0f);
    ui.alert(t, d, col.next(ui.alert_height(t, d, 380.0f, st)), st);
    ui.alert("提示", "次要样式", col.next(60.0f), Style().secondary());
    ui.alert("错误", "危险样式", col.next(60.0f), Style().destructive());

    ui.breadcrumb({"首页", "组件", "展示补充"}, col.next(20.0f));
    ui.kbd("Ctrl", {24, 24, 44, 26});
    ui.kbd("Esc",  {74, 24, 36, 26});

    if (ui.accordion("高级设置", &s.advanced_open, col.next(44.0f))) {}
    if (s.advanced_open) {
        Stack inner = ui.vbox(col.next(80.0f), 10.0f, 12.0f);
        ui.input(&s.api_key, inner.next(36.0f), Style().hint("API Key"));
        ui.checkbox(&s.dry_run, "试运行", inner.next(22.0f));
    }

    std::vector<std::string> heads = {"项目", "数值", "状态"};
    std::vector<std::vector<std::string>> rows = {
        {"渲染帧", "6.4 ms", "正常"}, {"空闲 CPU", "0.00%", "达标"}};
    ui.table(heads, rows, col.next(160.0f), Style().font(12.0f));
    ui.table(rows[0], rows, col.next(160.0f));       // headers 来自 vector

    std::vector<float> trend = {12, 30, 22, 48, -18, 36};
    ui.sparkline(trend, col.next(120.0f));
    ui.sparkline(trend, col.next(120.0f), Style().ghost().background(Color::hex(0x2563EB)));
    ui.bars(trend, col.next(120.0f));
    metricRow(ui, {24, 400, 600, 60}, trend);
}

// ---- 08：浮层 ----
void docs_08(Ui& ui, AppState& s) {
    Rect box = {24, 24, 160, 40};
    ui.button("删除", box, Style().destructive());
    ui.tooltip(box, "永久删除，不可恢复");

    int picked = -1;
    if (ui.menu("选择操作", {"重命名", "复制", "删除"}, &picked, {24, 80, 200, 36})) {
        s.last_action = std::to_string(picked);
    }

    Rect panel = {60, 70, 340, 170};
    if (ui.dialog("确认", "确定要删除吗？", &s.confirm_open, panel)) {
        if (ui.button("确定", {panel.x + 24, panel.bottom() - 60, 100, 36}, Style().destructive())) {
            s.confirm_open = false;
        }
        if (ui.button("取消", {panel.x + 136, panel.bottom() - 60, 100, 36}, Style().outline())) {
            s.confirm_open = false;
        }
    }

    if (s.toast_left > 0.0f) {
        ui.toast("已保存", {ui.width() - 200.0f, ui.height() - 60.0f, 180.0f, 44.0f});
        s.toast_left -= ui.frame_dt();
        ui.request_animation_frame();
    }
    ui.toast("危险", {24, 300, 180, 44}, Style().destructive());
}

// ---- 09：图元 ----
void docs_09(Ui& ui, Stack& col) {
    ui.clear(ui.theme().background);
    ui.fill_rect({24, 24, 200, 60}, Color::hex(0x18181B), 12.0f);
    ui.stroke_rect({24, 100, 200, 60}, Color::hex(0xE4E4E7), 8.0f, 1.0f);
    ui.fill_circle(300, 60, 20, Color::hex(0x2563EB));
    ui.draw_line(24, 180, 400, 180, Color::hex(0xE4E4E7), 1.0f);
    ui.draw_text("居中文字", {24, 200, 200, 40}, Color::hex(0x09090B), Style().center().middle());
    ui.drop_shadow({100, 260, 260, 80}, 12.0f, 22.0f, Color(0, 0, 0, 46), 8.0f);

    (void)ui.text_width("确定", 14.0f, 500);
    (void)ui.line_height(14.0f);
    Stack row = ui.hbox({24, 360, 400, 36}, 8.0f);
    ui.label("确定", row.next(ui.text_width("确定", 14.0f) + 16.0f));
    ui.label("确定", row.next_text("确定", 16.0f));
    ui.fill_rect_s({24, 24, 120, 64}, ui.theme().tint, 12, .6f);
    ui.stroke_rect_s({160, 24, 120, 64}, ui.theme().tint, 12, 2, .6f);
    ui.fill_rect_grad({24, 100, 120, 64}, ui.theme().fill2, ui.theme().fill5, 12);
    ui.inner_shadow({24, 100, 120, 64}, Color(0, 0, 0, 20), 12, 3, 1);
    ui.backdrop_blur({24, 100, 120, 64}, 12, 8);
    ui.glass({24, 100, 120, 64}, 12, Style().blur(12));
    ui.system_icon(aino::SystemIcon::Close, {24, 24, 16, 16}, ui.theme().label);
    ui.draw_mono_text("0123456789", 24, 200, ui.theme().label, 13);
    ui.progress_ring(.68f, {24, 240, 36, 36});
    (void)ui.mono_advance(13);
    (void)ui.mono_ascent(13);
    (void)ui.mono_width("012", 13);
    (void)col;
}

// ---- 11-motion：动画与变换 ----
void docs_11(Ui& ui, Stack& col) {
    const Rect box = col.next(120.0f);
    const uint64_t id = aino::detail::make_id("docs11", box);
    const bool hovered = box.contains(ui.input().mouse.x, ui.input().mouse.y);

    // 补间 + 缓动
    const float t = ui.tween(id, hovered ? 1.0f : 0.0f, 0.24f, aino::easing::out_cubic);
    ui.fill_rect(Rect(box.x, box.y, 240.0f * t, 3.0f), ui.theme().primary, 1.5f);

    // 颜色补间
    const Color bg = ui.tween_color(id ^ 7u, hovered ? ui.theme().accent : ui.theme().background, 0.18f);
    ui.panel(box, Style().background(bg));

    // 显隐
    const float a = ui.fade(id ^ 9u, true, 0.18f);
    if (a > 0.01f) {
        aino::Transform tint;
        tint.translate = aino::Vec2(0.0f, 8.0f * (1.0f - a));
        ui.push_transform(tint);
        ui.panel(box, Style().background(ui.theme().card.alpha255(int(255 * a))));
        ui.pop_transform();
    }

    // 纯函数平滑
    float x = 0.0f;
    x = ui.smooth(x, ui.input().mouse.x, 14.0f);
    (void)x;

    // 变换：平移 / 缩放 / 旋转 / 嵌套
    aino::Transform tf;
    tf.scale  = 1.0f + 0.03f * t;
    tf.rotate = -1.5f * t;
    tf.origin = aino::Vec2(box.cx(), box.cy());
    ui.push_transform(tf);
    ui.panel(box, Style().background(ui.theme().card));
    ui.label("卡片", box, Style().center());
    ui.pop_transform();

    aino::Transform slide;
    slide.translate = aino::Vec2(-280.0f * (1.0f - t), 0.0f);
    ui.push_transform(slide);
    ui.fill_rect(box, ui.theme().accent, 6.0f);
    ui.pop_transform();
    (void)ui.transform();
    (void)ui.transformed();

    // 虚拟滚动
    const Rect list = col.fill();
    const float row_h = 26.0f;
    const int   n = 100000;
    int drawn = ui.scroll(list, n, row_h, [&](int i, const Rect& row) {
        ui.label("第 " + std::to_string(i + 1) + " 行", row);
    });
    int first = 0, last = -1;
    (void)ui.scroll_visible(list, n, row_h, &first, &last);
    ui.scroll_bar(list, n * row_h);
    ui.scroll_to_index(list, n - 1, row_h, n);
    (void)ui.scroll_offset(list);
    (void)drawn;
}

} // namespace

// 各文档片段的入口（只保证能编过；运行时由调用方决定画哪一段）
void docs_all(Ui& ui, AppState& s, Stack& col) {
    docs_02();
    docs_03(ui, s);
    docs_04(ui, col);
    docs_05(ui, col);
    docs_06(ui, s, col);
    docs_07(ui, s, col);
    docs_08(ui, s);
    docs_09(ui, col);
    docs_11(ui, col);
}

int main() {
    // 纯编译期校验：不建窗口、不跑帧回调。
    // docs_01/docs_01_window 里有 Window 构造，这里不调用（那需要真实窗口）。
    (void)&docs_01;
    (void)&docs_01_window;
    (void)&docs_all;
    return 0;
}
