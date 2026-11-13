#define AINO_IMPLEMENTATION
#include "../aino.hpp"

#include <cstdio>
#include <cstdlib>

using aino::Color;
using aino::Rect;
using aino::Style;
using aino::SystemIcon;

static Rect sample(aino::Ui& ui, const Rect& card, const char* title, const char* caption) {
    ui.label(title, {card.x + 2, card.y + 2, card.w - 4, 20}, Style().font(13).semi_bold());
    ui.panel({card.x, card.y + 32, card.w, card.h - 32}, Style().background(ui.theme().window_bg.luminance() < .5f ? ui.theme().card : ui.theme().control_bg).corner(12));
    ui.muted_text(caption, {card.x + 16, card.bottom() - 30, card.w - 32, 18}, Style().font(11));
    return {card.x + 16, card.y + 48, card.w - 32, card.h - 96};
}

int main() {
    aino::WindowConfig cfg;
    cfg.width = 1000; cfg.height = 780;
    cfg.min_width = 720; cfg.min_height = 520;
    cfg.title = "aino · 基础图元";
    aino::Window win(cfg);
    if (!win.ok()) return 1;
    bool dark = std::getenv("AINO_DARK") != nullptr;
    win.set_theme(dark ? aino::Theme::dark() : aino::Theme::light());
    float radius = 12, width = 2, opacity = .65f;

    win.run([&](aino::Ui& ui) {
        ui.clear((ui.theme().window_bg.luminance() < .5f ? ui.theme().window_bg : aino::Color::hex(0xF5F5F7)));
        ui.heading("基础图元", {24, 15, 200, 30});
        const Rect theme_button(ui.width() - 48, 20, 24, 24);
        if (ui.button("", theme_button, Style().ghost())) {
            dark = !dark;
            win.set_theme(dark ? aino::Theme::dark() : aino::Theme::light());
        }
        ui.system_icon(dark ? SystemIcon::Sun : SystemIcon::Moon, theme_button.inset(4), ui.theme().label);
        const auto& th = ui.theme();
        const float gap = 20, margin = 28;
        const int columns = ui.width() >= 1160 ? 3 : 2;
        const int rows = (8 + columns - 1) / columns;
        const float card_w = (ui.width() - margin * 2 - gap * (columns - 1)) / columns;
        const Rect viewport(0, 64, ui.width(), ui.height() - 64);
        ui.begin_scroll(viewport, 208 + rows * 212.f + 24);
        const float y = viewport.y - ui.scroll_y();
        ui.label("绘制参数", {margin + 2, y + 8, ui.width() - margin * 2, 24}, Style().font(13).semi_bold().left());
        const Rect settings(margin, y + 42, ui.width() - margin * 2, 144);
        ui.panel(settings, Style().background(ui.theme().window_bg.luminance() < .5f ? ui.theme().card : ui.theme().control_bg).corner(12));
        const char* labels[] = {"圆角", "线宽", "不透明度"};
        float* parameters[] = {&radius, &width, &opacity};
        const float low[] = {0, 1, .1f}, high[] = {28, 8, 1};
        for (int i = 0; i < 3; ++i) {
            const Rect row(settings.x, settings.y + i * 48.f, settings.w, 48);
            const float slider_w = std::min(300.f, row.w * .48f);
            const Rect control(row.right() - 16 - slider_w, row.cy() - 12, slider_w, 24);
            ui.label(labels[i], {row.x + 16, row.cy() - 12, control.x - row.x - 96, 24}, Style().font(13));
            ui.slider(parameters[i], low[i], high[i], control);
            char value[24];
            if (i == 2) std::snprintf(value, sizeof(value), "%.0f%%", opacity * 100);
            else std::snprintf(value, sizeof(value), "%.1f", *parameters[i]);
            ui.draw_text(value, {control.x - 62, control.y, 44, 24}, th.label2, Style().font(12).right());
            if (i < 2) ui.separator({row.x + 16, row.bottom() - 1, row.w - 32, 1});
        }
        auto card = [&](int index) {
            return Rect(margin + (index % columns) * (card_w + gap), y + 216 + (index / columns) * 212, card_w, 192);
        };
        Rect r = sample(ui, card(0), "矩形与连续圆角", "填充 · 描边 · 可调圆角");
        ui.fill_rect_s({r.x + 12, r.y, r.w * .43f, r.h}, th.tint, radius, th.corner_smoothing);
        ui.stroke_rect_s({r.x + r.w * .55f, r.y, r.w * .4f, r.h}, th.tint, radius, width, th.corner_smoothing);

        r = sample(ui, card(1), "圆与几何差集", "圆 · 月牙");
        ui.fill_circle(r.x + r.w * .28f, r.cy(), 32, th.tint);
        ui.fill_crescent(r.x + r.w * .73f, r.cy(), 32, r.x + r.w * .73f + 14, r.cy() - 10, 33, th.orange);

        r = sample(ui, card(2), "线段", "圆头端点 · 可调线宽");
        for (int i = 0; i < 3; ++i)
            ui.draw_line(r.x + 18, r.y + 12 + i * 28.f, r.right() - 18, r.y + 24 + i * 22.f, th.tint, width + i);

        r = sample(ui, card(3), "透明合成", "半透明图形叠加");
        ui.fill_rect({r.x + 38, r.y + 4, 110, 70}, th.tint.with_alpha(uint8_t(opacity * 255)), radius);
        ui.fill_circle(r.x + 170, r.cy(), 38, th.pink.with_alpha(uint8_t(opacity * 255)));

        r = sample(ui, card(4), "阴影与渐变", "外阴影 · 内阴影 · 垂直渐变");
        Rect tile(r.x + 20, r.y + 8, r.w - 40, r.h - 16);
        ui.drop_shadow(tile, radius, 9, Color(0, 0, 0, 45), 3);
        ui.fill_rect_grad(tile, th.fill2, th.fill5, radius);
        ui.inner_shadow(tile, Color(0, 0, 0, 30), radius, 3, 1);

        r = sample(ui, card(5), "裁剪与变换", "旋转图形限制在裁剪区域内");
        ui.fill_rect(r, th.fill5, 0);
        ui.push_clip(r);
        aino::Transform transform;
        transform.origin = {r.cx(), r.cy()}; transform.rotate = 18;
        ui.push_transform(transform);
        ui.fill_rect(Rect::centered(r.cx(), r.cy(), r.w * .8f, 66), th.tint, radius);
        ui.pop_transform();
        ui.pop_clip();

        r = sample(ui, card(6), "文字与系统图标", "正文 · 等宽文本 · 平台系统符号");
        ui.draw_text("Aa 你好", {r.x, r.y, r.w, 24}, th.label, Style().font(18));
        ui.draw_mono_text("0123456789", r.x, r.y + 36, th.label2, 13);
        const SystemIcon icons[] = {SystemIcon::Folder, SystemIcon::Settings, SystemIcon::Star, SystemIcon::Info};
        for (int i = 0; i < 4; ++i)
            ui.system_icon(icons[i], {r.x + i * 32.f, r.y + 70, 24, 24}, th.tint);

        r = sample(ui, card(7), "背景模糊", "背景模糊近似材质");
        ui.push_clip(r);
        for (int i = 0; i < 7; ++i)
            ui.fill_circle(r.x + 22 + i * 51.f, r.cy(), 30, i % 2 ? th.pink : th.tint);
        ui.glass({r.x + 24, r.y + 8, r.w - 48, r.h - 16}, radius, Style().blur(12));
        ui.pop_clip();
        ui.end_scroll();
    });
    return 0;
}
