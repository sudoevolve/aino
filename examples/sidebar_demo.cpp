// =============================================================================
//  aino 最小示例 —— 侧边栏切换
//
//  展示侧栏导航与通知设置。
//
//  作者：sudoevolve <sudoevolve@gmail.com>
//  编译：g++ -std=c++17 -O2 -static -s sidebar_demo.cpp -o sidebar_demo.exe
//              -mwindows -luser32 -lgdi32 -lmsimg32
// =============================================================================
#define AINO_IMPLEMENTATION
#include "../aino.hpp"

using aino::Rect;
using aino::Style;

static void sidebar_brand(aino::Ui& ui, const char* title, const char* subtitle, Rect box, aino::SystemIcon icon) {
    const Rect mark(box.x, box.y + 2, 32, 32);
    ui.fill_rect(mark, ui.theme().tint.scaled_alpha(.13f), 8);
    ui.system_icon(icon, mark.inset(7), ui.theme().tint);
    ui.label(title, {box.x + 44, box.y, box.w - 44, 23}, Style().font(16).semi_bold().left());
    ui.draw_text(subtitle, {box.x + 44, box.y + 25, box.w - 44, 17}, ui.theme().label2, Style().font(11));
}

static Rect settings_row(aino::Ui& ui, Rect box, const char* title, float control_width,
                         const char* detail = nullptr, float control_height = 24) {
    const Rect control(box.right() - 16 - control_width, box.cy() - control_height * .5f, control_width, control_height);
    const float text_width = control.x - box.x - 32;
    const float y = detail ? box.cy() - 22 : box.cy() - 12;
    ui.label(title, {box.x + 16, y, text_width, 24}, Style().font(13).left());
    if (detail) ui.draw_text(detail, {box.x + 16, y + 24, text_width, 18}, ui.theme().label2, Style().font(11).left());
    return control;
}

struct SidebarApp {
    int page = 0, appearance = 0;
    bool notify = true, sound = true, preview = true;
};

static void draw_sidebar_app(aino::Ui& ui, SidebarApp& app) {
    const auto& th = ui.theme();
    const float nav_w = 200;
    const char* titles[] = {"概览", "设置", "关于"};
    const char* descriptions[] = {"你的应用，一切就绪。", "让应用按照你的习惯工作。", "轻巧的界面，熟悉的体验。"};
    const aino::SystemIcon icons[] = {aino::SystemIcon::Card, aino::SystemIcon::Settings, aino::SystemIcon::Info};
    ui.clear((th.window_bg.luminance() < .5f ? th.window_bg : aino::Color::hex(0xF5F5F7)));
    ui.fill_rect({0, 0, nav_w, ui.height()}, th.sidebar_bg);
    ui.separator({nav_w - 1, 0, 1, ui.height()});
    sidebar_brand(ui, "我的应用", "aino workspace", {20, 24, nav_w - 32, 44}, aino::SystemIcon::Navigation);
    ui.draw_text("工作空间", {20, 96, nav_w - 40, 20}, th.label2, Style().font(11).semi_bold());
    for (int i = 0; i < 3; ++i) {
        const Rect row(10, 124.f + i * 36, nav_w - 20, 32);
        if (ui.nav_item(titles[i], app.page == i, row, Style().font(13).padding(36))) app.page = i;
        ui.system_icon(icons[i], {22, row.cy() - 8, 16, 16}, app.page == i ? th.tint : th.label2);
    }
    ui.draw_text("aino · " AINO_VERSION, {20, ui.height() - 36, nav_w - 40, 20}, th.label2, Style().font(11));

    const Rect view(nav_w, 0, ui.width() - nav_w, ui.height());
    const float column_w = std::min(680.f, view.w - 56);
    const Rect col(view.cx() - column_w * .5f, view.y, column_w, view.h);
    auto scroll = ui.scroll(view, app.page == 1 ? 604.f : 450.f);
    const float y = 34 - scroll.offset;
    ui.heading(titles[app.page], {col.x, y, col.w, 36}, Style().font(26).bold().left());
    ui.draw_text(descriptions[app.page], {col.x, y + 42, col.w, 22}, th.label2, Style().font(12).left());

    if (app.page == 0) {
        ui.label("应用状态", {col.x + 2, y + 102, col.w - 4, 24}, Style().font(13).semi_bold().left());
        const Rect group(col.x, y + 136, col.w, 128);
        ui.panel(group, Style().background(ui.theme().window_bg.luminance() < .5f ? ui.theme().card : ui.theme().control_bg).corner(12));
        const Rect first(group.x, group.y, group.w, 64);
        settings_row(ui, first, "通知", 92, "及时接收来自应用的新消息");
        ui.draw_text(app.notify ? "已开启" : "已关闭", {first.right() - 108, first.cy() - 12, 92, 24},
                     app.notify ? th.green : th.label2, Style().font(12).right());
        ui.separator({first.x + 16, first.bottom() - 1, first.w - 32, 1});
        const Rect second(group.x, group.y + 64, group.w, 64);
        settings_row(ui, second, "通知偏好", 92, "声音、预览和外观");
        if (ui.button("管理…", {second.right() - 108, second.cy() - 12, 92, 24}, Style().standard())) app.page = 1;
        ui.draw_text("你的偏好会在切换页面后保留。", {col.x + 2, group.bottom() + 14, col.w - 4, 22},
                     th.label2, Style().font(11));
    } else if (app.page == 1) {
        ui.label("外观", {col.x + 2, y + 102, col.w - 4, 24}, Style().font(13).semi_bold().left());
        const Rect appearance(col.x, y + 136, col.w, 64);
        ui.panel(appearance, Style().background(ui.theme().window_bg.luminance() < .5f ? ui.theme().card : ui.theme().control_bg).corner(12));
        const Rect control = settings_row(ui, appearance, "应用外观", std::min(176.f, col.w * .44f));
        ui.segmented({"浅色", "深色"}, &app.appearance, control, Style().font(12));

        ui.label("通知", {col.x + 2, y + 230, col.w - 4, 24}, Style().font(13).semi_bold().left());
        const Rect notifications(col.x, y + 264, col.w, 192);
        ui.panel(notifications, Style().background(ui.theme().window_bg.luminance() < .5f ? ui.theme().card : ui.theme().control_bg).corner(12));
        const char* labels[] = {"允许通知", "播放提示音", "显示消息预览"};
        const char* details[] = {"接收应用的重要消息", "新消息到达时播放声音", "在通知中显示消息内容"};
        bool* values[] = {&app.notify, &app.sound, &app.preview};
        for (int i = 0; i < 3; ++i) {
            const Rect row(notifications.x, notifications.y + i * 64.f, notifications.w, 64);
            ui.toggle(values[i], settings_row(ui, row, labels[i], 44, details[i], 20));
            if (i < 2) ui.separator({row.x + 16, row.bottom() - 1, row.w - 32, 1});
        }
        ui.draw_text("关闭通知后，声音与预览偏好仍会保留。", {col.x + 2, notifications.bottom() + 14, col.w - 4, 22},
                     th.label2, Style().font(11));
    } else {
        ui.label("关于应用", {col.x + 2, y + 102, col.w - 4, 24}, Style().font(13).semi_bold().left());
        const Rect group(col.x, y + 136, col.w, 144);
        ui.panel(group, Style().background(ui.theme().window_bg.luminance() < .5f ? ui.theme().card : ui.theme().control_bg).corner(12));
        sidebar_brand(ui, "aino", "单头文件 C++17 桌面 UI 库", {group.x + 20, group.y + 20, group.w - 40, 44}, aino::SystemIcon::Navigation);
        ui.separator({group.x + 16, group.y + 85, group.w - 32, 1});
        ui.label("版本 " AINO_VERSION, {group.x + 20, group.y + 103, 120, 22}, Style().font(12));
        ui.draw_text("Apache-2.0", {group.right() - 148, group.y + 103, 128, 22}, th.label2, Style().font(12).right());
    }
}

int main() {
    aino::WindowConfig cfg;
    cfg.width = 900; cfg.min_width = 680;
    cfg.height = 680; cfg.min_height = 480;
    cfg.title = "aino · 侧边栏切换";
    aino::Window win(cfg);
    if (!win.ok()) return 1;

    SidebarApp app;
    if (const char* value = std::getenv("AINO_DARK")) app.appearance = std::strcmp(value, "0") != 0 ? 1 : 0;
    win.set_theme(app.appearance ? aino::Theme::dark() : aino::Theme::light());

    win.run([&](aino::Ui& ui) {
        draw_sidebar_app(ui, app);
        win.set_theme(app.appearance ? aino::Theme::dark() : aino::Theme::light());
    });

    return 0;
}
