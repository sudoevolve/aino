// =============================================================================
//  aino 组件画廊 —— 展示全部预设组件
//  编译：
//    MSVC : cl /std:c++17 /EHsc /O2 main.cpp user32.lib gdi32.lib
//    GCC  : g++ -std=c++17 -O2 main.cpp -lX11
//    Clang: clang++ -std=c++17 -O2 main.cpp -framework Cocoa -framework CoreText
// =============================================================================
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cstdio>
#include <cstdlib>

#include <cmath>
#include <string>
#include <vector>

using aino::Color;
using aino::Rect;
using aino::Style;
using aino::Ui;

// -----------------------------------------------------------------------------
//  应用状态 —— 立即模式里，状态由调用方持有（这正是它体积小的原因）
// -----------------------------------------------------------------------------
struct AppState {
    bool dark = false;

    std::string email = "";
    std::string search = "";
    bool remember = true;
    bool notifications = false;
    bool autosave = true;
    float volume = 0.62f;
    int  plan = 1;
    int  tab = 0;
    int  range = 0;

    int clicks = 0;
    std::string last_action = "（还没点过任何按钮）";
};

int main() {
    aino::Window win(940, 648, "aino \xE2\x80\x94 \xE5\x8D\x95\xE5\xA4\xB4\xE6\x96\x87\xE4\xBB\xB6 GUI");
    if (!win.ok()) return 1;

    Ui& ui = win.ui();
    AppState app;

    const std::vector<std::string> plans = {"Starter", "Pro", "Enterprise"};
    const std::vector<std::string> ranges = {"今天", "本周", "本月"};

        const int __COLS = std::getenv("AINO_COLS") ? std::atoi(std::getenv("AINO_COLS")) : 7;
win.run([&](Ui& ui) {
        static long __f = 0; ++__f;
        { static FILE* __fp = std::fopen("C:/Users/admin/WorkBuddy/2026-10-16-13-31-22/.workbuddy/probe/demo_frames.log", "w");
          if (__fp) { std::fprintf(__fp, "%ld ", __f); std::fflush(__fp); } }
        // ---- 主题切换（先处理，这样清屏用的就是新主题）----
        const Rect theme_btn(940 - 32 - 36, 30, 36, 36);
        if (ui.icon_button("", theme_btn, Style().ghost().corner(8.0f))) app.dark = !app.dark;
        win.set_theme(app.dark ? aino::Theme::dark() : aino::Theme::light());
        const aino::Theme& th = ui.theme();

        ui.clear(th.background);

        // 图标自绘：太阳 / 月亮。用字体里的 ☀ ☾ 会因缺字形变成方块。
        {
            const Color ic = th.foreground;
            const float bx = theme_btn.cx(), by = theme_btn.cy();
            if (app.dark) {
                ui.fill_circle(bx, by, 5.6f, ic);
                for (int k = 0; k < 8; ++k) {
                    const float a = float(k) * 3.14159265f / 4.0f;
                    ui.draw_line(bx + std::cos(a) * 8.4f, by + std::sin(a) * 8.4f,
                                 bx + std::cos(a) * 11.2f, by + std::sin(a) * 11.2f, ic, 1.8f);
                }
            } else {
                ui.fill_circle(bx - 1.2f, by - 0.6f, 8.4f, ic);
                ui.fill_circle(bx + 3.6f, by - 3.4f, 7.6f, th.background);
            }
        }

        // =====================================================================
        //  顶栏
        // =====================================================================
        ui.heading("组件画廊",  {32, 28, 300, 34}, Style().left());
        ui.muted_text("aino 0.1.0 \xC2\xB7 \xE5\x8D\x95\xE5\xA4\xB4\xE6\x96\x87\xE4\xBB\xB6 \xC2\xB7 \xE7\xAB\x8B\xE5\x8D\xB3\xE6\xA8\xA1\xE5\xBC\x8F \xC2\xB7 shadcn \xE9\xA3\x8E\xE6\xA0\xBC",
                      {32, 64, 460, 20}, Style().left());

        ui.separator({32, 96, 940 - 64, 1});

        // =====================================================================
        //  三栏
        // =====================================================================
        const float cy = 120.0f, ch = 500.0f;
        const float cw = 280.0f;
        const float cx0 = 32.0f, cx1 = cx0 + cw + 24.0f, cx2 = cx1 + cw + 24.0f;

        // 列 1：按钮
        if (__COLS & 1) {
            const float x = cx0, y = cy, w = cw, h = ch;
            ui.panel({x, y, w, h});
            const float px = x + 20.0f, pw = w - 40.0f;
            float yy = y + 20.0f;

            ui.subheading("\xE6\x8C\x89\xE9\x92\xAE", {px, yy, pw, 24}, Style().left());
            yy += 38.0f;

            const aino::Style::Variant variants[] = {
                Style::Default, Style::Secondary, Style::Outline, Style::Ghost,
                Style::Destructive, Style::Link
            };
            const char* names[] = {
                "Default", "Secondary", "Outline", "Ghost", "Destructive", "Link"
            };
            for (int i = 0; i < 6; ++i) {
                Style s;
                s.variant = variants[i];
                if (ui.button(names[i], {px, yy, pw, 36}, s)) {
                    app.clicks++;
                    app.last_action = names[i];
                }
                yy += 44.0f;
            }

            ui.separator({px, yy + 2.0f, pw, 1});
            yy += 20.0f;

            ui.muted_text("\xE7\x8A\xB6\xE6\x80\x81", {px, yy, pw, 18}, Style().left());
            yy += 26.0f;

            Style dis;
            dis.disabled = true;
            ui.button("\xE7\xA6\x81\xE7\x94\xA8", {px, yy, (pw - 12.0f) * 0.5f, 36}, dis);

            Style pressy;
            pressy.press_scale = 0.96f;
            pressy.variant = Style::Secondary;
            if (ui.button("\xE6\x8C\x89\xE4\xB8\x8B\xE7\xBC\xA9\xE6\x94\xBE", {px + (pw - 12.0f) * 0.5f + 12.0f, yy, (pw - 12.0f) * 0.5f, 36}, pressy))
                app.clicks++;
            yy += 52.0f;

            ui.muted_text("Badge", {px, yy, pw, 18}, Style().left());
            yy += 24.0f;
            ui.badge("New",       {px, yy, 54, 22});
            ui.badge("\xE5\xB7\xB2\xE9\xAA\x8C\xE8\xAF\x81", {px + 62, yy, 62, 22});
            Style red; red.variant = Style::Destructive;
            ui.badge("\xE5\xA4\xB1\xE8\xB4\xA5", {px + 132, yy, 54, 22}, red);
        }

        // 列 2：表单
        if (__COLS & 2) {
            const float x = cx1, y = cy, w = cw, h = ch;
            ui.panel({x, y, w, h});
            const float px = x + 20.0f, pw = w - 40.0f;
            float yy = y + 20.0f;

            ui.subheading("\xE8\xA1\xA8\xE5\x8D\x95", {px, yy, pw, 24}, Style().left());
            yy += 40.0f;

            ui.label("\xE9\x82\xAE\xE7\xAE\xB1", {px, yy, pw, 18}, Style().left());
            yy += 22.0f;
            ui.input(&app.email, {px, yy, pw, 36}, Style().hint("you@example.com"));
            yy += 50.0f;

            ui.label("\xE6\x90\x9C\xE7\xB4\xA2", {px, yy, pw, 18}, Style().left());
            yy += 22.0f;
            ui.input(&app.search, {px, yy, pw, 36}, Style().hint("\xE8\xBE\x93\xE5\x85\xA5\xE5\x85\xB3\xE9\x94\xAE\xE8\xAF\x8D\xE2\x80\xA6"));
            yy += 50.0f;

            if (ui.checkbox(&app.remember, "\xE8\xAE\xB0\xE4\xBD\x8F\xE8\xBF\x99\xE5\x8F\xB0\xE8\xAE\xBE\xE5\xA4\x87", {px, yy, pw, 22})) {}
            yy += 30.0f;
            if (ui.checkbox(&app.autosave, "\xE8\x87\xAA\xE5\x8A\xA8\xE4\xBF\x9D\xE5\xAD\x98", {px, yy, pw, 22})) {}
            yy += 38.0f;

            ui.separator({px, yy, pw, 1});
            yy += 18.0f;

            ui.label("\xE9\x80\x9A\xE7\x9F\xA5", {px, yy, pw - 60.0f, 24}, Style().left());
            ui.toggle(&app.notifications, {x + w - 20.0f - 44.0f, yy - 1.0f, 44.0f, 26.0f});
            yy += 40.0f;

            ui.label("\xE9\x9F\xB3\xE9\x87\x8F", {px, yy, pw, 18}, Style().left());
            yy += 24.0f;
            ui.slider(&app.volume, 0.0f, 1.0f, {px, yy, pw, 22});
            yy += 44.0f;

            ui.label("\xE5\xA5\x97\xE9\xA4\x90", {px, yy, pw, 18}, Style().left());
            yy += 22.0f;
            ui.select(plans, &app.plan, {px, yy, pw, 36}, Style().hint("\xE9\x80\x89\xE6\x8B\xA9\xE5\xA5\x97\xE9\xA4\x90"));

            // 底部：把状态贴到底
            ui.muted_text("\xE7\x82\xB9\xE5\x87\xBB\xE6\xAC\xA1\xE6\x95\xB0\xEF\xBC\x9A" + std::to_string(app.clicks),
                          {px, y + h - 30.0f, pw, 18}, Style().left());
        }

        // 列 3：展示
        if (__COLS & 4) {
            const float x = cx2, y = cy, w = cw, h = ch;
            ui.panel({x, y, w, h});
            const float px = x + 20.0f, pw = w - 40.0f;
            float yy = y + 20.0f;

            ui.subheading("\xE5\xB1\x95\xE7\xA4\xBA", {px, yy, pw, 24}, Style().left());
            yy += 40.0f;

            const char* tabs[] = {"\xE6\xA6\x82\xE8\xA7\x88", "\xE5\x88\x86\xE6\x9E\x90", "\xE8\xAE\xBE\xE7\xBD\xAE"};
            std::vector<std::string> tab_labels(tabs, tabs + 3);
            ui.tabs(tab_labels, &app.tab, {px, yy, pw, 38});
            yy += 52.0f;

            ui.muted_text("\xE6\x97\xB6\xE9\x97\xB4\xE8\x8C\x83\xE5\x9B\xB4", {px, yy, pw, 18}, Style().left());
            yy += 22.0f;
            ui.segmented(ranges, &app.range, {px, yy, pw, 32});
            yy += 50.0f;

            ui.separator({px, yy, pw, 1});
            yy += 18.0f;

            ui.label("\xE5\xAD\x98\xE5\x82\xA8\xE7\xA9\xBA\xE9\x97\xB4", {px, yy, pw, 18}, Style().left());
            yy += 24.0f;
            ui.progress(0.68f, {px, yy, pw, 8});
            yy += 34.0f;

            ui.label("\xE7\x89\x88\xE6\x9C\xAC", {px, yy, pw, 18}, Style().left());
            yy += 24.0f;
            ui.code_text("aino 0.1.0", {px, yy, pw, 28}, Style().font(13.0f));
            yy += 46.0f;

            ui.muted_text("\xE6\x9C\x80\xE8\xBF\x91\xE6\x93\x8D\xE4\xBD\x9C", {px, yy, pw, 18}, Style().left());
            yy += 24.0f;
            ui.code_text(app.last_action, {px, yy, pw, 28}, Style().font(13.0f));
            yy += 44.0f;

            if (ui.button("\xE9\x87\x8D\xE7\xBD\xAE", {px, yy, pw, 34}, Style().outline()))
                app.last_action = "\xE9\x87\x8D\xE7\xBD\xAE";
        }
    });

    return 0;
}
