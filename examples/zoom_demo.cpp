// =============================================================================
//  aino 最小示例 —— 缩放按钮
//
//  缩放范围 50%～200%。Transform 只影响绘制，不改变布局与命中区域。
//
//  作者：sudoevolve <sudoevolve@gmail.com>
//  编译：g++ -std=c++17 -O2 -static -s zoom_demo.cpp -o zoom_demo.exe
//              -mwindows -luser32 -lgdi32 -lmsimg32
// =============================================================================
#define AINO_IMPLEMENTATION
#include "../aino.hpp"

#include <cstdio>

using aino::Rect;
using aino::Stack;
using aino::Style;

int main() {
    aino::WindowConfig cfg;
    cfg.width = cfg.min_width = 520;
    cfg.height = cfg.min_height = 420;
    cfg.title = "aino · 缩放按钮";
    aino::Window win(cfg);
    if (!win.ok()) return 1;

    float zoom = 1.0f;

    win.run([&](aino::Ui& ui) {
        ui.clear(ui.theme().background);

        Stack col = ui.vbox(Rect(0.0f, 0.0f, ui.width(), ui.height()), 10.0f, 28.0f);
        ui.heading("缩放按钮", col.next(30.0f), Style().left());
        ui.muted_text("调整预览比例，或复位至 100%", col.next(18.0f), Style().left());
        col.skip(4.0f);

        // 为底部操作预留固定高度。
        const Rect stage = col.next(col.remain() - 56.0f);
        const Rect row   = col.next(44.0f);

        ui.panel(stage, Style().background(ui.theme().muted).corner(12.0f));

        // 缩放从当前显示值续接，补间结束后停止请求重绘。
        const float k = ui.tween(aino::detail::make_id("zoom_demo", stage), zoom, 0.18f,
                                 aino::easing::out_cubic);

        const Rect card = Rect::centered(stage.cx(), stage.cy(), 200.0f, 110.0f);

        aino::Transform t;
        t.scale  = k;
        t.origin = aino::Vec2(card.cx(), card.cy());   // origin 是**世界坐标**
        ui.push_clip(stage);
        ui.push_transform(t);
        {
            ui.panel(card, Style().background(ui.theme().primary).corner(14.0f));

            char buf[32];
            std::snprintf(buf, sizeof(buf), "%d%%", int(k * 100.0f + 0.5f));
            ui.heading(buf, Rect(card.x, card.y + 30.0f, card.w, 30.0f),
                       Style().center().color(ui.theme().primary_foreground));
            ui.label("绕中心缩放", Rect(card.x, card.y + 64.0f, card.w, 18.0f),
                     Style().center().font(12.0f).color(ui.theme().primary_foreground));
        }
        ui.pop_transform();
        ui.pop_clip();

        // 按钮行：三份等分，顺序「缩小 / 放大 / 复位」
        Stack btns = ui.hbox(row, 10.0f);
        if (ui.button("缩小", btns.part(0, 3), Style().outline()))
            zoom = zoom > 0.5f ? zoom - 0.25f : 0.5f;
        if (ui.button("放大", btns.part(1, 3)))
            zoom = zoom < 2.0f ? zoom + 0.25f : 2.0f;
        if (ui.button("复位", btns.part(2, 3), Style().outline()))
            zoom = 1.0f;
    });

    return 0;
}
