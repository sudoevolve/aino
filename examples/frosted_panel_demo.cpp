#define AINO_IMPLEMENTATION
#include "../aino.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

using aino::Color;
using aino::Rect;
using aino::Style;

static aino::Theme app_theme(bool dark) {
    aino::Theme theme = dark ? aino::Theme::dark() : aino::Theme::light();
    // Use opaque secondary text so GDI and the raster backends have matching contrast.
    theme.label2 = dark ? Color(0xA0, 0xA0, 0xA6) : Color(0x6E, 0x6E, 0x73);
    theme.muted_foreground = theme.label2;
    return theme;
}

int main() {
    aino::WindowConfig config;
    config.width = 900;
    config.height = 660;
    config.min_width = 680;
    config.min_height = 520;
    config.title = "aino · 磨砂面板";
    aino::Window window(config);
    if (!window.ok()) return 1;

    bool dark = std::getenv("AINO_DARK") != nullptr;
    window.set_theme(app_theme(dark));

    float blur = 16.0f;
    float opacity = 0.38f;
    float corner = 24.0f;
    aino::GlassPanel panel_component(72.0f);

    window.run([&](aino::Ui& ui) {
        const float width = ui.width();
        const float height = ui.height();
        const auto& theme = ui.theme();

        // A vivid wallpaper makes the backdrop blur easy to see.
        ui.fill_rect_grad({0, 0, width, height}, Color::hex(0xE98D9E), Color::hex(0x344D9A));
        ui.fill_circle(width * 0.16f, height * 0.22f, height * 0.28f, Color::hex(0xFFB454));
        ui.fill_circle(width * 0.83f, height * 0.24f, height * 0.34f, Color::hex(0x43C6C8));
        ui.fill_circle(width * 0.78f, height * 0.83f, height * 0.31f, Color::hex(0xD85FB7));
        ui.fill_circle(width * 0.17f, height * 0.88f, height * 0.30f, Color::hex(0x596CE0));
        ui.fill_rect_s({width * 0.32f, height * 0.10f, width * 0.32f, 26},
                       Color(255, 255, 255, 42), 13, theme.corner_smoothing);
        ui.fill_rect_s({width * 0.52f, height * 0.78f, width * 0.28f, 18},
                       Color(255, 255, 255, 38), 9, theme.corner_smoothing);

        const float panel_w = std::min(440.0f, width - 48.0f);
        const float panel_h = 366.0f;
        const Rect panel_bounds((width - panel_w) * 0.5f, (height - panel_h) * 0.5f,
                                panel_w, panel_h);
        const Color material = dark
            ? Color(25, 28, 42, static_cast<uint8_t>(opacity * 255.0f))
            : Color(255, 255, 255, static_cast<uint8_t>(opacity * 255.0f));
        const Rect moved_panel = panel_component.draw(
            ui, panel_bounds, corner, Style().blur(blur).background(material));

        ui.draw_text("磨砂面板", {moved_panel.x + 24, moved_panel.y + 16, moved_panel.w - 48, 24},
                     theme.label, Style().font(17).semi_bold());
        ui.draw_text("拖动顶部区域移动 · 调整下方材质参数",
                     {moved_panel.x + 24, moved_panel.y + 43, moved_panel.w - 48, 18},
                     theme.label2, Style().font(11));
        ui.separator({moved_panel.x + 20, moved_panel.y + 72, moved_panel.w - 40, 1});

        const char* labels[] = {"模糊半径", "材质浓度", "圆角半径"};
        float* values[] = {&blur, &opacity, &corner};
        const float minimum[] = {1.0f, 0.12f, 8.0f};
        const float maximum[] = {30.0f, 0.72f, 32.0f};
        for (int i = 0; i < 3; ++i) {
            const float row_y = moved_panel.y + 88.0f + i * 58.0f;
            ui.draw_text(labels[i], {moved_panel.x + 24, row_y, moved_panel.w - 104, 20},
                         theme.label, Style().font(12.5f));

            char value[24];
            if (i == 1) std::snprintf(value, sizeof(value), "%.0f%%", opacity * 100.0f);
            else std::snprintf(value, sizeof(value), "%.0f", *values[i]);
            ui.draw_text(value, {moved_panel.right() - 70, row_y, 46, 20},
                         theme.label2, Style().font(11).right());
            ui.slider(values[i], minimum[i], maximum[i],
                      {moved_panel.x + 24, row_y + 25, moved_panel.w - 48, 22});
        }

        const Rect hint(moved_panel.x + 20, moved_panel.bottom() - 48, moved_panel.w - 40, 30);
        ui.fill_rect_s(hint, Color(255, 255, 255, dark ? 18 : 76), 9, theme.corner_smoothing);
        ui.draw_text("aino  ·  BACKDROP MATERIAL",
                     {hint.x + 10, hint.y, hint.w - 20, hint.h},
                     theme.label2, Style().font(10).semi_bold().middle());
    });
    return 0;
}
