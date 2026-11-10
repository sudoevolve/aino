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
using aino::Stack;
using aino::Style;

int main() {
    aino::WindowConfig cfg;
    cfg.width = cfg.min_width = 640;
    cfg.height = cfg.min_height = 420;
    cfg.title = "aino · 侧边栏切换";
    aino::Window win(cfg);
    if (!win.ok()) return 1;

    int page = 0;
    bool notify = true;

    const char* kItems[3] = { "概览", "设置", "关于" };
    const char* kDesc[3]  = {
        "当前应用状态",
        "管理通知偏好",
        "版本与许可信息",
    };

    win.run([&](aino::Ui& ui) {
        ui.clear(ui.theme().background);

        // ---------------- 左：侧边栏 ----------------
        const float nav_w = 176.0f;
        const Rect  side(0.0f, 0.0f, nav_w, ui.height());
        ui.fill_rect(side, ui.theme().sidebar_bg);
        ui.fill_rect(Rect(side.right() - 1.0f, 0.0f, 1.0f, side.h), ui.theme().separator);

        Stack nav = ui.vbox(side, 1.0f, 10.0f);
        ui.muted_text("我的应用", nav.next(22.0f), Style().left().semi_bold().font(11.0f));
        nav.skip(4.0f);

        for (int i = 0; i < 3; ++i) {
            if (ui.nav_item(kItems[i], page == i, nav.next(26.0f), Style().font(13.0f)))
                page = i;
        }

        // ---------------- 右：内容区 ----------------
        Stack body = ui.vbox(Rect(nav_w, 0.0f, ui.width() - nav_w, ui.height()), 10.0f, 28.0f);
        ui.heading(kItems[page], body.next(32.0f), Style().left());
        ui.muted_text(kDesc[page], body.next(18.0f), Style().left());
        body.skip(8.0f);
        ui.separator(body.next(1.0f));
        body.skip(14.0f);

        const Rect area = body.fill();
        ui.panel(area);
        Stack in = ui.vbox(area, 12.0f, 24.0f);

        switch (page) {
        case 0:
            ui.label("通知状态", in.next(22.0f), Style().left());
            ui.muted_text(notify ? "通知已开启" : "通知已关闭", in.next(18.0f), Style().left());
            ui.muted_text("可在设置中调整通知偏好。", in.next(18.0f), Style().left());
            break;

        case 1: {
            ui.label("通知", in.next(22.0f), Style().left());
            const Rect row = in.next(26.0f);
            ui.toggle(&notify, Rect(row.x, row.y + 3.0f, 44.0f, 20.0f));
            ui.label(notify ? "已开启" : "已关闭",
                     Rect(row.x + 54.0f, row.y + 3.0f, 200.0f, 20.0f),
                     Style().left().font(14.0f));
            break;
        }

        case 2:
            ui.label("aino · 单头文件 GUI 库", in.next(22.0f), Style().left());
            ui.muted_text("版本 " AINO_VERSION " · Apache-2.0 许可", in.next(18.0f), Style().left());
            break;
        }
    });

    return 0;
}
