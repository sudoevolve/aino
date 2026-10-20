// =============================================================================
//  aino 最小示例 —— 缩放按钮
//
//  全部逻辑就是一个 float 加一个 Transform：
//    · 「缩小 / 放大」按钮只改 zoom（0.5 ~ 2.0），到边界就夹住，不回绕；
//    · 卡片绕自身中心缩放，走 tween 补间，动画跑完就停止要帧（空闲 CPU 回 0）；
//    · Transform 只影响绘制，不影响布局与命中 —— 按钮的点击区域没变过，
//      卡片放大到 200% 也不会把下面的按钮挤走。
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
    aino::Window win(520, 420, "aino · 缩放按钮");
    if (!win.ok()) return 1;

    float zoom = 1.0f;      // 目标缩放倍数，唯一的"状态"

    win.run([&](aino::Ui& ui) {
        ui.clear(ui.theme().background);

        // 根布局：标题 / 说明 / 舞台 / 按钮行，全部交给 Stack，不手算 y
        Stack col = ui.vbox(Rect(0.0f, 0.0f, ui.width(), ui.height()), 10.0f, 28.0f);
        ui.heading("缩放按钮", col.next(30.0f), Style().left());
        ui.muted_text("按钮只改一个 float，卡片绕自身中心缩放", col.next(18.0f), Style().left());
        col.skip(4.0f);

        // 先给底部按钮行留 44 + 间距 12，剩下全是舞台；窗口改大小不用改数字
        const Rect stage = col.next(col.remain() - 56.0f);
        const Rect row   = col.next(44.0f);

        // 舞台画个比卡片大一圈的底色，方便看缩放有没有溢出
        ui.panel(stage, Style().background(ui.theme().muted).corner(12.0f));

        // 目标一变，tween 就从"当前显示值"续跑；到点自动停止 repaint
        const float k = ui.tween(aino::detail::make_id("zoom_demo", stage), zoom, 0.18f,
                                 aino::easing::out_cubic);

        const Rect card = Rect::centered(stage.cx(), stage.cy(), 200.0f, 110.0f);

        aino::Transform t;
        t.scale  = k;
        t.origin = aino::Vec2(card.cx(), card.cy());   // origin 是**世界坐标**
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
