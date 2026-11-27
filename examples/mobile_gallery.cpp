// =============================================================================
//  aino 移动端示例 —— Gallery（手机竖屏）
//
//  Android 移植的参考示例：同一个源文件在桌面端开一个手机比例的窗口预览，
//  在 Android 上由 aino::android_run 驱动（见 android/ 目录的打包工程）。
//  布局遵循 AGENTS.md 的分组卡片约定，触控目标 ≥ 44 逻辑像素。
//
//  构建（桌面预览）：cmake --build build --target aino_mobile_gallery
//  构建（Android）：  python3 android/build_apk.py
// =============================================================================
#define AINO_IMPLEMENTATION
#include "../aino.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>

using aino::Rect;
using aino::Style;

namespace {

constexpr int kPageControls = 0, kPageList = 1, kPageAbout = 2;

struct ListItem {
    const char* name;
    const char* detail;
    aino::SystemIcon icon;
    unsigned color;
};

constexpr ListItem kItems[] = {
    {"无线局域网", "已连接 · aino-5G", aino::SystemIcon::Network, 0x34C759},
    {"蓝牙", "2 台设备", aino::SystemIcon::Toggle, 0x0084F7},
    {"通知", "12 条待查看", aino::SystemIcon::Bell, 0xFF3B30},
    {"声音与触感", "铃声：开场", aino::SystemIcon::Pause, 0xFF2D55},
    {"专注模式", "工作期间静音", aino::SystemIcon::Moon, 0x5E5CE6},
    {"屏幕使用时间", "昨日 3 小时 12 分", aino::SystemIcon::Timer, 0x6155F5},
    {"通用", "关于本机", aino::SystemIcon::Settings, 0x8E8E93},
    {"控制中心", "自定义控件", aino::SystemIcon::List, 0x8E8E93},
    {"显示", "深浅外观、字号", aino::SystemIcon::Eye, 0x0084F7},
    {"主屏幕", "页面布局", aino::SystemIcon::Card, 0x64D2FF},
    {"辅助功能", "视觉、听觉", aino::SystemIcon::People, 0x0084F7},
    {"墙纸", "动态与静态", aino::SystemIcon::Image, 0x30D158},
    {"Siri 与搜索", "听写与建议", aino::SystemIcon::More, 0xBF5AF2},
    {"电池", "健康度 92%", aino::SystemIcon::Info, 0x34C759},
    {"隐私与安全性", "定位权限", aino::SystemIcon::Check, 0x0084F7},
    {"软件更新", "已是最新版本", aino::SystemIcon::Upload, 0x8E8E93},
    {"存储空间", "已用 86 GB", aino::SystemIcon::Download, 0x8E8E93},
    {"日期与时间", "自动设置", aino::SystemIcon::Calendar, 0x8E8E93},
    {"键盘", "第三方键盘", aino::SystemIcon::Edit, 0x8E8E93},
    {"语言与地区", "简体中文", aino::SystemIcon::Text, 0x8E8E93},
};
constexpr int kItemsCount = int(sizeof(kItems) / sizeof(kItems[0]));

struct MobileApp {
    int  page = kPageControls;
    int  appearance = 0;            // 0 浅色 / 1 深色
    bool wifi = true, bluetooth = false, dnd = true;
    float brightness = 0.72f, volume = 0.35f, text_size = 0.5f;
    float step = 2.0f;
    int   seg = 0;
    bool  remember = true;
    std::string project, email;
    // 弹层
    bool toast_on = false, dialog_open = false, alert_open = false;
    float toast_until = 0.0f;
    // 滚动偏移（每页独立，切页不丢）
    float scroll[3] = {};
    // 帧统计（关于页展示）
    float frame_ms = 0.0f, callback_ms = 0.0f, dpi_scale = 1.0f;
    std::chrono::steady_clock::time_point last_frame{};
};

// 组外分组标题（13 半粗）
void group_title(aino::Ui& ui, const char* text, Rect box) {
    ui.label(text, {box.x + 2, box.y, box.w - 4, 24},
             Style().font(13).semi_bold().left().color(ui.theme().label2));
}

// 圆角卡片容器（浅色 control_bg / 深色 card_bg）
Rect card(aino::Ui& ui, Rect box) {
    const auto& th = ui.theme();
    ui.panel(box, Style().background(th.window_bg.luminance() < .5f ? th.card_bg : th.control_bg)
                      .corner(12));
    return box;
}

// 设置行：左标题+可选说明，右控件区返回给调用方放开关/按钮
Rect row(aino::Ui& ui, Rect box, const char* title, float control_w, const char* detail = nullptr) {
    const Rect control(box.right() - 16 - control_w, box.cy() - 12, control_w, 24);
    const float text_w = control.x - box.x - 16;
    const float y = detail ? box.cy() - 22 : box.cy() - 12;
    ui.label(title, {box.x + 16, y, text_w, 24}, Style().font(14).left());
    if (detail)
        ui.draw_text(detail, {box.x + 16, y + 24, text_w, 18}, ui.theme().label2,
                     Style().font(11).left());
    return control;
}

void separator(aino::Ui& ui, const Rect& group, const Rect& r) {
    ui.separator({r.x + 16, r.bottom() - 1, group.w - 32, 1});
}

// ---- 页面 1：组件 ----
void draw_controls_page(aino::Ui& ui, MobileApp& a, const Rect& view, float offset) {
    const float x = view.x, w = view.w;
    float y = view.y - offset;

    group_title(ui, "开关", {x, y, w, 24});
    y += 28;
    {
        const Rect g = card(ui, {x, y, w, 192});
        bool* vals[] = {&a.wifi, &a.bluetooth, &a.dnd};
        const char* names[] = {"无线局域网", "蓝牙", "专注模式"};
        const char* tips[] = {"连接到可用网络", "附近可配对的设备", "静音通知，保持专注"};
        for (int i = 0; i < 3; ++i) {
            const Rect r(g.x, g.y + i * 64.f, g.w, 64);
            ui.toggle(vals[i], row(ui, r, names[i], 51, tips[i]));
            if (i < 2) separator(ui, g, r);
        }
        y += g.h + 26;
    }

    group_title(ui, "滑块", {x, y, w, 24});
    y += 28;
    {
        const Rect g = card(ui, {x, y, w, 192});
        const char* names[] = {"亮度", "音量", "文字大小"};
        float* vals[] = {&a.brightness, &a.volume, &a.text_size};
        for (int i = 0; i < 3; ++i) {
            const Rect r(g.x, g.y + i * 64.f, g.w, 64);
            ui.label(names[i], {r.x + 16, r.y + 10, r.w - 32, 22}, Style().font(14).left());
            ui.slider(vals[i], 0.f, 1.f, {r.x + 16, r.bottom() - 30, r.w - 32, 24});
            if (i < 2) separator(ui, g, r);
        }
        y += g.h + 26;
    }

    group_title(ui, "按钮与步进", {x, y, w, 24});
    y += 28;
    {
        const Rect g = card(ui, {x, y, w, 64});
        const float half = (g.w - 60) * 0.5f;
        if (ui.button("弹出 Toast", {g.x + 16, g.cy() - 18, half, 36}))
            { a.toast_on = true; a.toast_until = ui.time() + 2.0f; }
        if (ui.button("普通弹窗", {g.x + 32 + half, g.cy() - 18, half, 36}, Style().secondary()))
            a.dialog_open = true;
        y += g.h + 14;
        const Rect g2 = card(ui, {x, y, w, 64});
        ui.stepper(&a.step, 1.f, 0.f, 999.f, row(ui, g2, "数量", std::min(150.f, g2.w * 0.44f)));
        y += g2.h + 26;
    }

    group_title(ui, "选择器", {x, y, w, 24});
    y += 28;
    {
        const Rect g = card(ui, {x, y, w, 128});
        const Rect seg_row(g.x, g.y, g.w, 64);
        ui.segmented({"浅色", "深色", "自动"}, &a.seg,
                     row(ui, seg_row, "外观", std::min(200.f, g.w * 0.52f)));
        separator(ui, g, seg_row);
        ui.checkbox(&a.remember, "记住这些设置", {g.x + 16, g.y + 78, g.w - 32, 30}, Style().font(14));
        y += g.h + 26;
    }

    group_title(ui, "输入（点击唤出软键盘，支持中文）", {x, y, w, 24});
    y += 28;
    {
        const Rect g = card(ui, {x, y, w, 128});
        ui.input(&a.project, {g.x + 16, g.y + 16, g.w - 32, 36}, Style().font(14).hint("项目名称"));
        ui.input(&a.email, {g.x + 16, g.y + 76, g.w - 32, 36}, Style().font(14).hint("name@company.com"));
        y += g.h + 26;
    }

    group_title(ui, "指示器", {x, y, w, 24});
    y += 28;
    {
        const Rect g = card(ui, {x, y, w, 96});
        const auto& th = ui.theme();
        ui.progress_ring(a.brightness, {g.x + 40, g.y + 20, 56, 56},
                         Style().stroke(5, aino::Color::none()));
        ui.spinner({g.x + w * 0.5f - 24, g.y + 36, 20, 20}, Style().color(th.tint).animate(true));
        ui.badge("进行中", {g.right() - 120, g.y + 34, 100, 28},
                 Style().secondary().font(12).corner(14));
        y += g.h + 26;
    }

    group_title(ui, "弹层", {x, y, w, 24});
    y += 28;
    {
        const Rect g = card(ui, {x, y, w, 64});
        const float half = (g.w - 60) * 0.5f;
        if (ui.button("警示弹窗", {g.x + 16, g.cy() - 18, half, 36}, Style().destructive()))
            a.alert_open = true;
        if (ui.button("普通弹窗", {g.x + 32 + half, g.cy() - 18, half, 36})) a.dialog_open = true;
        y += g.h + 30;
    }
}

// ---- 页面 2：长列表（测触摸滚动）----
void draw_list_page(aino::Ui& ui, const Rect& view, float offset) {
    const auto& th = ui.theme();
    const float x = view.x, w = view.w;
    float y = view.y - offset;
    group_title(ui, "设置项（拖动滚动）", {x, y, w, 24});
    y += 28;
    const float row_h = 76.0f;
    const Rect g = card(ui, {x, y, w, float(kItemsCount) * row_h});
    for (int i = 0; i < kItemsCount; ++i) {
        const Rect r(g.x, g.y + i * row_h, g.w, row_h);
        const Rect mark(r.x + 14, r.cy() - 14, 28, 28);
        const aino::Color tint = aino::Color::hex(kItems[i].color);
        ui.fill_rect(mark, tint.scaled_alpha(.16f), 7);
        ui.system_icon(kItems[i].icon, mark.inset(6), tint);
        const float text_x = mark.right() + 12;
        const float text_w = w - (text_x - r.x) - 40;
        ui.label(kItems[i].name, {text_x, r.y + 14, text_w, 22}, Style().font(15).left());
        ui.draw_text(kItems[i].detail, {text_x, r.y + 40, text_w, 18}, th.label2,
                     Style().font(11).left());
        // 右侧箭头
        ui.system_icon(aino::SystemIcon::Next, {r.right() - 26, r.cy() - 7, 14, 14}, th.label3);
        if (i + 1 < kItemsCount) ui.separator({r.x + 54, r.bottom() - 1, r.w - 54, 1});
    }
}

// ---- 页面 3：关于 ----
void draw_about_page(aino::Ui& ui, MobileApp& a, const Rect& view, float offset) {
    const auto& th = ui.theme();
    const float x = view.x, w = view.w;
    float y = view.y - offset;

    // 品牌头
    {
        const Rect mark(x + w / 2 - 26, y + 10, 52, 52);
        ui.fill_rect(mark, th.tint.scaled_alpha(.14f), 13);
        ui.system_icon(aino::SystemIcon::Card, mark.inset(12), th.tint);
        ui.label("aino Gallery", {x, y + 70, w, 28}, Style().font(20).bold().center());
        ui.draw_text("单头文件 C++17 UI 库 · Android 移植预览", {x, y + 102, w, 20}, th.label2,
                     Style().font(12).center());
        y += 140;
    }

    group_title(ui, "运行环境", {x, y, w, 24});
    y += 28;
    {
        const Rect g = card(ui, {x, y, w, 192});
        char scale_text[64], fps_text[64], cb_text[64], size_text[64];
        std::snprintf(scale_text, sizeof scale_text, "%.2f ×", a.dpi_scale);
        std::snprintf(fps_text, sizeof fps_text, "%.1f ms / 帧", a.frame_ms);
        std::snprintf(cb_text, sizeof cb_text, "%.2f ms", a.callback_ms);
        std::snprintf(size_text, sizeof size_text, "%d × %d", int(ui.width()), int(ui.height()));
        struct InfoRow { const char* name; const char* value; };
#if defined(AINO_PLATFORM_ANDROID)
        const char* platform_text = "Android (NativeActivity)";
#else
        const char* platform_text = "桌面预览";
#endif
        const InfoRow rows[] = {
            {"界面库", "aino " AINO_VERSION},
            {"平台", platform_text},
            {"DPI 缩放", scale_text},
            {"逻辑尺寸", size_text},
            {"帧间隔", fps_text},
            {"回调耗时", cb_text},
        };
        for (int i = 0; i < 6; ++i) {
            const Rect r(g.x, g.y + i * 32.f, g.w, 32);
            ui.label(rows[i].name, {r.x + 16, r.cy() - 11, w * 0.4f, 22}, Style().font(13).left());
            ui.draw_text(rows[i].value, {r.x + w * 0.4f, r.cy() - 11, w * 0.6f - 32, 22},
                         th.label2, Style().font(13).right());
            if (i < 5) ui.separator({r.x + 16, r.bottom() - 1, r.w - 32, 1});
        }
        y += g.h + 26;
    }

    group_title(ui, "外观", {x, y, w, 24});
    y += 28;
    {
        const Rect g = card(ui, {x, y, w, 64});
        ui.segmented({"浅色", "深色"}, &a.appearance, row(ui, g, "主题", std::min(150.f, g.w * 0.4f)));
        y += g.h + 30;
    }
}

void draw_gallery(aino::Ui& ui, MobileApp& a) {
    const auto& th = ui.theme();
    ui.clear((th.window_bg.luminance() < .5f) ? th.window_bg : aino::Color::hex(0xF5F5F7));
    const float w = ui.width(), h = ui.height();

    // ---- 顶部固定区：标题 + 页面分段 ----
    const float top_h = 104.f;
    ui.label("Gallery", {24, 18, w - 24, 34}, Style().font(26).bold().left());
    const Rect nav(16, 58, std::min(w - 32, 300.f), 32);
    ui.segmented({"组件", "列表", "关于"}, &a.page, nav, Style().font(13));

    // ---- 滚动内容区 ----
    const Rect view(16, top_h, w - 32, h - top_h);
    // 内容高度按各组实测累加（末组底部留 40 余量，保证末项可达）
    float content_h = 1492.f + 40.f;
    if (a.page == kPageList) content_h = 200.f + float(kItemsCount) * 76.f;
    else if (a.page == kPageAbout) content_h = 660.f;
    auto sc = ui.scroll(view, content_h);
    if (a.page == kPageControls) draw_controls_page(ui, a, view, sc.offset);
    else if (a.page == kPageList) draw_list_page(ui, view, sc.offset);
    else draw_about_page(ui, a, view, sc.offset);

    // ---- 弹层 ----
    if (a.toast_on) {
        if (ui.time() >= a.toast_until) a.toast_on = false;
        else {
            const Rect r{std::max(14.f, w - 324), h - 96, std::min(296.f, w - 28), 54};
            ui.toast("", r);
            ui.fill_circle(r.x + 18, r.cy(), 4, th.green);
            ui.label("已保存设置", {r.x + 34, r.y + 7, r.w - 44, 40}, Style().font(12).left());
        }
    }
    if (a.dialog_open)
        ui.dialog("完成设置", "这些演示状态只保存在内存里，重启应用后会恢复默认。", &a.dialog_open,
                  {w * 0.5f - 130, h * 0.5f - 70, 260, 140});
    if (a.alert_open)
        ui.alert_dialog("清除演示数据", "将把所有开关与滑块恢复为默认值。", "清除", "取消",
                        &a.alert_open, {w * 0.5f - 130, h * 0.5f - 70, 260, 150});
}

// 深浅主题（次级灰阶固定，避免卡片和底色改变文字层次 —— 同 sidebar_demo）
aino::Theme app_theme(bool dark) {
    aino::Theme t = dark ? aino::Theme::dark() : aino::Theme::light();
    t.label2 = dark ? aino::Color(0xA0, 0xA0, 0xA6, 255) : aino::Color(0x6E, 0x6E, 0x73, 255);
    t.label3 = dark ? aino::Color(0x6E, 0x6E, 0x73, 255) : aino::Color(0x98, 0x98, 0x9D, 255);
    return t;
}

// AINO_SHOT_BMP=<路径> 把当前后备缓冲存成 24bit BMP —— 与 gallery.cpp / tools
// 回归探针同一套协议，供无桌面环境抓渲染结果；写完即退出。
void dump_canvas_bmp(aino::Window& win, const char* path) {
    aino::Canvas& cv = win.impl->canvas;
    if (!cv.px || cv.w <= 0 || cv.h <= 0 || cv.stride <= 0) return;
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    const int w = cv.w, h = cv.h;
    const int pad = (4 - (w * 3) % 4) % 4;
    const int row = w * 3 + pad;
    const uint32_t data = uint32_t(row) * uint32_t(h);
    const uint32_t file = 54u + data, off = 54u, bisize = 40u;
    const uint16_t planes = 1, bpp = 24;
    uint8_t hd[54] = {};
    hd[0] = 'B';
    hd[1] = 'M';
    std::memcpy(&hd[2], &file, 4);
    std::memcpy(&hd[10], &off, 4);
    std::memcpy(&hd[14], &bisize, 4);
    std::memcpy(&hd[18], &w, 4);
    std::memcpy(&hd[22], &h, 4);
    std::memcpy(&hd[26], &planes, 2);
    std::memcpy(&hd[28], &bpp, 2);
    std::memcpy(&hd[34], &data, 4);
    std::fwrite(hd, 1, 54, f);
    std::vector<uint8_t> line(size_t(row), 0);
    for (int y = h - 1; y >= 0; --y) {   // BMP 自下而上
        const uint8_t* src = &cv.px[size_t(y) * size_t(cv.stride)];
        for (int x = 0; x < w; ++x) {
            line[size_t(x) * 3u + 0u] = src[size_t(x) * 4u + aino::detail::CH_B];
            line[size_t(x) * 3u + 1u] = src[size_t(x) * 4u + aino::detail::CH_G];
            line[size_t(x) * 3u + 2u] = src[size_t(x) * 4u + aino::detail::CH_R];
        }
        std::fwrite(line.data(), 1, line.size(), f);
    }
    std::fclose(f);
    std::fprintf(stderr, "[mobile_gallery] 已写出 %s（%dx%d）\n", path, w, h);
}

// 共享主流程：桌面与 Android 都走这里
int run_gallery(aino::WindowConfig& cfg) {
    aino::Window win(cfg);
    if (!win.ok()) return 1;

    MobileApp app;
    app.dpi_scale = win.dpi_scale();
    if (const char* value = std::getenv("AINO_DARK"))
        app.appearance = std::strcmp(value, "0") != 0 ? 1 : 0;
    if (const char* value = std::getenv("AINO_PAGE")) {
        if (std::strcmp(value, "list") == 0) app.page = kPageList;
        else if (std::strcmp(value, "about") == 0) app.page = kPageAbout;
    }
    win.set_theme(app_theme(app.appearance != 0));

    int frames = 0;
    bool shot_pending = false;
    bool shot_done = false;
    const char* shot = std::getenv("AINO_SHOT_BMP");

    win.run([&](aino::Ui& ui) {
        const auto t0 = std::chrono::steady_clock::now();
        if (app.last_frame.time_since_epoch().count() != 0) {
            const float gap = std::chrono::duration<float, std::milli>(t0 - app.last_frame).count();
            // 平滑一点，关于页读数更稳定
            app.frame_ms = app.frame_ms > 0.0f ? app.frame_ms * 0.9f + gap * 0.1f : gap;
        }
        app.last_frame = t0;

        // 截图：此刻读到的后备缓冲是上一帧的成品（含帧末浮层）。
        // 静态页面没有常驻动画，只画一帧就去空闲了 —— 所以帧末要主动排一次
        // 定时重绘，让"下一帧"真的到来。
        if (shot && shot_pending) {
            dump_canvas_bmp(win, shot);
            shot_pending = false;
            shot_done = true;
            win.close();
        }

        draw_gallery(ui, app);
        win.set_theme(app_theme(app.appearance != 0));

        const auto t1 = std::chrono::steady_clock::now();
        app.callback_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();

        if (shot && !shot_done && ++frames >= 1) {
            shot_pending = true;
            ui.request_frame_in(0.05);   // 排出下一帧
        }
    });
    return 0;
}

} // namespace

#ifdef AINO_PLATFORM_ANDROID
// ---- Android（NativeActivity）：窗口由系统给，android_run 等窗口就绪后回调 ----
static void android_gallery_body(struct android_app* app) {
    aino::WindowConfig cfg;
    cfg.native_surface = app->window;
    cfg.title = "aino gallery";
    cfg.render_scale = 0.0f;   // 跟满窗口物理分辨率（转屏/分屏自动重分配）
    run_gallery(cfg);
}
AINO_ANDROID_MAIN(android_gallery_body)
#else
// ---- 桌面预览：开一个手机比例窗口，调样式不用每次都装到手机 ----
int main() {
    aino::WindowConfig cfg;
    cfg.width = 390;
    cfg.height = 844;
    cfg.min_width = 320;
    cfg.min_height = 560;
    cfg.title = "aino · mobile gallery";
#if !defined(AINO_PLATFORM_WINDOWS)
    cfg.icon_path = aino::executable_directory() + "aino_icon.png";
#endif
    return run_gallery(cfg);
}
#endif
