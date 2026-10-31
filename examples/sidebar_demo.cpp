// =============================================================================
//  aino 最小示例 —— 侧边栏切换
//
//  新手只需要看三件事：
//    1. 整个界面只有一个状态：int page（当前在第几页）；
//    2. 左边循环画三个 nav_item，点中哪个就把 page 改掉；
//    3. 右边用 switch (page) 决定画什么。
//
//  没有路由表、没有控件树、没有回调注册。按钮在"点下去那一帧"就把 page 改了，
//  而界面每帧从头重建，所以下一帧右边自然就是新页 —— 不用通知，也不用刷新。
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
    aino::Window win(640, 420, "aino · 侧边栏切换");
    if (!win.ok()) return 1;

    int  page   = 0;            // ← 全部状态：当前是第几页
    bool notify = true;         // 设置页那个开关的状态

    const char* kItems[3] = { "概览", "设置", "关于" };
    const char* kDesc[3]  = {
        "点左边的条目换页，右边整块内容跟着换",
        "一个 bool 就是一个开关的全部状态",
        "没有路由表，只有 switch",
    };

    win.run([&](aino::Ui& ui) {
        ui.clear(ui.theme().background);

        // ---------------- 左：侧边栏 ----------------
        const float nav_w = 176.0f;
        const Rect  side(0.0f, 0.0f, nav_w, ui.height());
        // 侧边栏用 Materials/Controls/Sidebar 这个 token 本身（#EAEAEA / #1E1E1E）。
        // 别写 `background.mix(muted, 0.6f)`：muted 是带 alpha 的填充色
        // rgba(0,0,0,13)，和不透明色 mix 出来是中灰 + 半透明，压在窗口上是 #BBBBBB。
        ui.fill_rect(side, ui.theme().sidebar_bg);
        ui.fill_rect(Rect(side.right() - 1.0f, 0.0f, 1.0f, side.h), ui.theme().separator);

        Stack nav = ui.vbox(side, 1.0f, 10.0f);
        // 侧边栏里不摆 App 名：那是标题栏的事。这里用 11 Semibold 的分组标题行。
        ui.muted_text("我的应用", nav.next(22.0f), Style().left().semi_bold().font(11.0f));
        nav.skip(4.0f);

        for (int i = 0; i < 3; ++i) {
            // 第二个参数 = 是否选中（选中 = 强调色 23% 的圆角块）
            // 返回 true = 这一帧刚被点了一下
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

        // 剩下的空间全给面板；面板里放什么由 switch 决定
        const Rect area = body.fill();
        ui.panel(area);
        Stack in = ui.vbox(area, 12.0f, 24.0f);

        switch (page) {
        case 0:
            ui.label("这是概览页。", in.next(22.0f), Style().left());
            ui.muted_text("左边高亮的那一条，就是 page 的值。", in.next(18.0f), Style().left());
            ui.muted_text("没有通知、没有订阅、没有状态容器。", in.next(18.0f), Style().left());
            ui.muted_text("改一个 int，下一帧就是新界面。", in.next(18.0f), Style().left());
            break;

        case 1: {
            ui.label("通知", in.next(22.0f), Style().left());
            // 开关的状态也只是一个 bool，点一下它自己翻
            const Rect row = in.next(26.0f);
            if (ui.toggle(&notify, Rect(row.x, row.y, 44.0f, 26.0f))) {
                // 值已经在 toggle 里翻过了，这里可以顺手存盘 / 发命令
            }
            ui.label(notify ? "已开启" : "已关闭",
                     Rect(row.x + 54.0f, row.y + 3.0f, 200.0f, 20.0f),
                     Style().left().font(14.0f));
            break;
        }

        case 2:
            ui.label("aino · 单头文件 GUI 库", in.next(22.0f), Style().left());
            ui.muted_text("版本 0.2.9 · Apache-2.0 许可", in.next(18.0f), Style().left());
            ui.muted_text("代码就在 sidebar_demo.cpp，不到百行。", in.next(18.0f), Style().left());
            break;
        }
    });

    return 0;
}
