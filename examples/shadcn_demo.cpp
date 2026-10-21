// =============================================================================
//  aino 示例 —— shadcn/ui 基础控件补齐（0.3.0 / 0.3.1）
//
//  这一页把新补的控件全摆出来，每个都能真点：
//
//    表单    Toggle / Toggle Group / Button Group / Input Group / Input OTP
//            Field / Combobox
//    展示    Item / Empty / Pagination / Calendar
//    浮层    Popover / Hover Card / Alert Dialog / Drawer / Context Menu / Menubar
//    布局    Split Pane / Aspect Ratio
//
//  全部状态都是 main() 里那几个普通变量，没有任何隐藏的控件对象。
//  控件在"点下去那一帧"就把变量改掉，下一帧重画时自然就是新样子 ——
//  这就是立即模式，不需要"通知视图刷新"。
//
//  作者：sudoevolve <sudoevolve@gmail.com>
//  编译：g++ -std=c++17 -O2 -static -s shadcn_demo.cpp -o shadcn_demo.exe
//              -mwindows -luser32 -lgdi32 -lmsimg32
// =============================================================================
#define AINO_IMPLEMENTATION
#include "../aino.hpp"

using aino::Rect;
using aino::Stack;
using aino::Style;
using aino::Vec2;

int main() {
    aino::Window win(780, 720, "aino · shadcn 基础控件");
    if (!win.ok()) return 1;

    // ---- 全部状态：没有一条藏在控件里 ----
    bool bold = true, italic = false;      // Toggle
    int  seg = 1;                          // Toggle Group（0=日 1=周 2=月）
    int  bg_click = -1;                    // Button Group 上一次点了哪个
    std::string amount = "128";            // Input Group
    std::string otp = "2048";              // Input OTP
    int  combo = 1;                        // Combobox
    int  cy = 2026, cm = 10, cd = 22;      // Calendar
    int  page = 2;                         // Pagination
    bool pop_open = false;                 // Popover
    bool alert_open = false;               // Alert Dialog
    bool drawer_open = false;              // Drawer
    float split = 0.42f;                   // Split Pane
    int  ctx_click = -1;                   // Context Menu
    std::string picked = "还没点菜单";      // Menubar

    const std::vector<std::string> kLangs = {"C++", "Rust", "Go", "Zig", "Python", "TypeScript", "Swift"};
    const std::vector<std::string> kMenuLabels = {"文件", "编辑", "视图", "帮助"};
    const std::vector<std::vector<std::string>> kMenuItems = {
        {"新建", "打开…", "保存", "另存为…"},
        {"撤销", "重做", "剪切", "复制", "粘贴"},
        {"放大", "缩小", "实际大小", "全屏"},
        {"快捷键", "文档", "关于"},
    };

    win.run([&](aino::Ui& ui) {
        ui.clear(ui.theme().background);
        const float w = ui.width(), h = ui.height();

        // ---------------- 菜单栏：贴窗口顶边 ----------------
        // 点开一个之后，把鼠标直接划到"编辑"上就会切过去 —— 桌面菜单栏的老规矩。
        const Rect menu_r(0.0f, 0.0f, w, 34.0f);
        const aino::MenuPick mp = ui.menubar(kMenuLabels, kMenuItems, menu_r);
        if (mp.valid())
            picked = kMenuLabels[size_t(mp.menu)] + " → " + kMenuItems[size_t(mp.menu)][size_t(mp.item)];

        auto root = ui.vbox(Rect(0.0f, 36.0f, w, h - 36.0f), 14.0f, 22.0f);
        const Rect head = root.next(50.0f);
        const Rect body = root.next(std::max(root.remain() - 66.0f, 1.0f));
        const Rect foot = root.fill();

        // ---------------- 标题 ----------------
        ui.heading("shadcn 基础控件", Rect(head.x, head.y, head.w, 28.0f));
        ui.muted_text("0.3.0 / 0.3.1 补的 20 个 API · 这一页摆了 18 个，都能点 · " + picked,
                      Rect(head.x, head.y + 29.0f, head.w, 18.0f), Style().left().font(13.0f));

        // Aspect Ratio：给宽度，高度按 16:9 自己算出来（立即模式里它就是个 helper）
        {
            const Rect card = aino::Ui::aspect_rect(
                Rect(head.right() - 84.0f, head.y + 1.0f, 84.0f, 0.0f), 16.0f / 9.0f);
            ui.panel(card, Style().background(ui.theme().muted).corner(6.0f));
            ui.muted_text("16 : 9", card, Style().center().font(11.0f).muted_text());
        }

        auto cols = ui.hbox(body, 22.0f);
        const Rect L = cols.part(0, 2);
        const Rect R = cols.part(1, 2);

        // ================= 左列：表单类 =================
        auto lc = ui.vbox(L, 11.0f);

        ui.label("Toggle（按下态按钮）", lc.next(17.0f), Style().left().font(12.0f).muted_text());
        {
            const Rect row = lc.next(34.0f);
            ui.toggle_button("粗体", &bold, Rect(row.x, row.y, 82.0f, 34.0f));
            ui.toggle_button("斜体", &italic, Rect(row.x + 90.0f, row.y, 82.0f, 34.0f));
        }

        ui.label("Toggle Group（单选，再点取消）", lc.next(17.0f),
                 Style().left().font(12.0f).muted_text());
        ui.toggle_group({"日", "周", "月"}, &seg, lc.next(34.0f));

        ui.label(bg_click < 0 ? "Button Group（相邻共享外框）"
                              : "Button Group · 刚点了第 " + std::to_string(bg_click + 1) + " 个",
                 lc.next(17.0f), Style().left().font(12.0f).muted_text());
        ui.button_group({"复制", "粘贴", "剪切"}, &bg_click, lc.next(34.0f));

        ui.label("Input Group（前后缀）", lc.next(17.0f), Style().left().font(12.0f).muted_text());
        ui.input_group(&amount, "¥", "元", lc.next(36.0f));

        ui.label("Input OTP（只收数字，退格回退）", lc.next(17.0f),
                 Style().left().font(12.0f).muted_text());
        ui.input_otp(&otp, 6, lc.next(46.0f));

        ui.label("Combobox（点开打字过滤，↑↓ 选，回车确认）", lc.next(17.0f),
                 Style().left().font(12.0f).muted_text());
        ui.combobox(kLangs, &combo, lc.next(36.0f));

        // ================= 右列：展示类 =================
        auto rc = ui.vbox(R, 11.0f);

        char cal_title[64];
        std::snprintf(cal_title, sizeof cal_title, "Calendar · 选中 %d-%02d-%02d", cy, cm, cd);
        ui.label(cal_title, rc.next(17.0f), Style().left().font(12.0f).muted_text());
        ui.calendar(&cy, &cm, &cd, rc.next(216.0f));

        ui.label("Pagination · 第 " + std::to_string(page + 1) + " / 20 页", rc.next(17.0f),
                 Style().left().font(12.0f).muted_text());
        ui.pagination(&page, 20, rc.next(32.0f));

        ui.label("Item（悬停整行亮，悬停还能出卡片）", rc.next(17.0f),
                 Style().left().font(12.0f).muted_text());
        const Rect item_r = rc.next(52.0f);
        ui.item("张伟", "zhangwei@example.com", "管理员", item_r);
        ui.hover_card(item_r, Vec2(210.0f, 78.0f), [&](const Rect& c) {
            ui.label("张伟", Rect(c.x, c.y, c.w, 22.0f), Style().left().semi_bold());
            ui.muted_text("加入于 2024 年 3 月", Rect(c.x, c.y + 24.0f, c.w, 18.0f),
                          Style().left().font(12.0f));
            ui.muted_text("这是 Hover Card，不抢点击", Rect(c.x, c.y + 44.0f, c.w, 18.0f),
                          Style().left().font(12.0f));
        });

        ui.label("Empty + Context Menu（在虚线框里右键）", rc.next(17.0f),
                 Style().left().font(12.0f).muted_text());
        const Rect empty_r = rc.next(80.0f);
        ui.empty_state("还没有通知", "有事的时候会出现在这里", empty_r);

        // ================= 底栏：Split Pane + 浮层入口 =================
        auto fc = ui.hbox(foot, 14.0f);
        const Rect sp = fc.part(0, 2);
        {
            Rect a, b;
            ui.split_pane(&split, sp, &a, &b, true, 0.22f);
            ui.panel(a, Style().background(ui.theme().muted).corner(8.0f));
            ui.muted_text("拖动中间的分隔条", Rect(a.x + 10.0f, a.y, std::max(a.w - 20.0f, 1.0f), a.h),
                          Style().center().font(12.0f).muted_text());
            ui.panel(b, Style().background(ui.theme().muted).corner(8.0f));
            ui.muted_text("Split Pane", Rect(b.x + 10.0f, b.y, std::max(b.w - 20.0f, 1.0f), b.h),
                          Style().center().font(12.0f).muted_text());
        }

        auto bh = ui.hbox(fc.part(1, 2), 8.0f);
        const Rect b1 = bh.part(0, 3);
        const Rect b2 = bh.part(1, 3);
        const Rect b3 = bh.part(2, 3);
        const float btn_h = std::min(sp.h, 40.0f);

        // 触发器由 popover 自己画（和 menu 一个长相），所以这里不另画按钮
        ui.popover("排序", &pop_open, Rect(b1.x, b1.y, b1.w, btn_h), Vec2(200.0f, 96.0f),
                   [&](const Rect& c) {
                       ui.item("按名称", "", "", Rect(c.x, c.y, c.w, 30.0f));
                       ui.item("按时间", "", "", Rect(c.x, c.y + 32.0f, c.w, 30.0f));
                       ui.item("按大小", "", "", Rect(c.x, c.y + 64.0f, c.w, 30.0f));
                   });

        if (ui.button("删除…", Rect(b2.x, b2.y, b2.w, btn_h), Style().destructive()))
            alert_open = true;
        if (ui.button("详情", Rect(b3.x, b3.y, b3.w, btn_h), Style().outline()))
            drawer_open = true;

        // ================= 浮层：必须放在最后 =================
        // 右键菜单挂在 Empty 上（弹层自己会在鼠标位置出现）
        ui.context_menu(empty_r, {"标记已读", "归档", "删除"}, &ctx_click);

        ui.alert_dialog("删除这个文件？", "删除后无法恢复。这份数据没有其他备份。",
                        "删除", "取消", &alert_open,
                        Rect(w * 0.5f - 190.0f, h * 0.5f - 84.0f, 380.0f, 168.0f),
                        Style().destructive());

        ui.drawer("详情", &drawer_open, 0, Rect(w - 300.0f, 0.0f, 300.0f, h), [&](const Rect& c) {
            ui.label("从右边滑出的面板", Rect(c.x, c.y, c.w, 22.0f), Style().left().semi_bold());
            ui.muted_text("遮罩是渐显的 —— Drawer 只做透明度动画，",
                          Rect(c.x, c.y + 28.0f, c.w, 18.0f), Style().left().font(12.0f));
            ui.muted_text("不做位移，免得按钮看着偏、点着错。",
                          Rect(c.x, c.y + 48.0f, c.w, 18.0f), Style().left().font(12.0f));
            ui.separator(Rect(c.x, c.y + 74.0f, c.w, 1.0f));
            ui.muted_text("点遮罩 / Esc / 右上角 × 关闭", Rect(c.x, c.y + 84.0f, c.w, 18.0f),
                          Style().left().font(12.0f));
        });
    });

    return 0;
}
