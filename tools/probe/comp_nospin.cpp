// =============================================================================
//  aino 组件总览 —— 新增的 17 个常用组件
//  编译：cl /std:c++17 /EHsc /O2 components.cpp user32.lib gdi32.lib
// =============================================================================
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using aino::Rect;
using aino::Stack;
using aino::Style;
using aino::Ui;

int main() {
    aino::Window win(1080, 700, "aino components");
    if (!win.ok()) return 1;

    // --- 组件状态 ---
    int   radio_idx = 1;
    float step_val  = 12.0f;
    int   stars     = 4;
    std::string note = "第一行\n第二行会自动接上";

    std::string uname = "张三";
    float cpu = 0.0f;

    std::vector<float> wave(24), bars_v(12);
    for (size_t i = 0; i < wave.size(); ++i)
        wave[i] = std::sin(float(i) * 0.55f) * 0.6f + 0.35f * std::sin(float(i) * 1.7f) + 0.5f;
    for (size_t i = 0; i < bars_v.size(); ++i)
        bars_v[i] = (i == 7) ? -0.45f : 0.25f + 0.6f * std::fabs(std::sin(float(i) * 0.9f + 1.3f));

    const std::vector<std::string> plans = { "免费版", "专业版", "企业版" };
    const std::vector<std::string> heads = { "模块", "状态", "耗时" };
    const std::vector<std::vector<std::string>> rows = {
        { "解析配置",   "完成", "12 ms" },
        { "加载主题",   "完成", "3 ms"  },
        { "构建布局",   "完成", "8 ms"  },
        { "光栅化",     "进行中", "41 ms" },
        { "文本整形",   "完成", "17 ms" },
        { "事件派发",   "完成", "1 ms"  },
        { "合成上屏",   "完成", "6 ms"  },
    };
    const std::vector<std::string> menu_items = { "重命名", "复制", "导出为 PNG", "删除" };

    bool  dark = false;
    bool  acc_open = true;
    bool  dlg_open = false;
    float toast_left = 0.0f;
    int   menu_pick = -1;
    std::vector<std::string> picked_log;

    win.run([&](Ui& ui) {
        // 主题切换：先处理，这样清屏用的就是新配色
        const Rect theme_btn(ui.width() - 64.0f, 20.0f, 40.0f, 40.0f);
        if (ui.icon_button("", theme_btn, Style().outline())) dark = !dark;
        ui.set_theme(dark ? aino::Theme::dark() : aino::Theme::light());
        const aino::Theme& th = ui.theme();
        const aino::Color& C = th.foreground;
        ui.clear(th.background);
        // 太阳 / 月亮自绘（用字体里的 ☀ ☾ 会因缺字形变方块）
        {
            const float bx = theme_btn.cx(), by = theme_btn.cy();
            if (dark) {
                ui.fill_circle(bx, by, 5.6f, C);
                for (int k = 0; k < 8; ++k) {
                    const float a = float(k) * 3.14159265f / 4.0f;
                    ui.draw_line(bx + std::cos(a) * 8.4f, by + std::sin(a) * 8.4f,
                                 bx + std::cos(a) * 11.2f, by + std::sin(a) * 11.2f, C, 1.8f);
                }
            } else {
                ui.fill_circle(bx - 1.2f, by - 0.6f, 8.4f, C);
                ui.fill_circle(bx + 3.6f, by - 3.4f, 7.6f, th.background);
            }
        }
        const float dt = ui.frame_dt();
        if (toast_left > 0.0f) { toast_left -= dt; ui.repaint(); }
        cpu = 0.5f + 0.5f * std::sin(ui.time() * 1.7f);

        Stack page = ui.vbox({ 0, 0, ui.width(), ui.height() }, 14.0f, 20.0f);

        // ---------------------------- 顶栏 ----------------------------
        {
            Stack bar = page.hbox(40.0f, 12.0f);
            ui.heading("组件总览", bar.next(200.0f), Style().left());
            const Rect tr = bar.rest();
            const Rect txt(tr.x, tr.y + 10.0f, std::max(tr.w - 56.0f, 1.0f), 20.0f);
            ui.push_clip(txt);       // 别让副标题钻到右上角的主题按钮底下
            ui.muted_text("18 个新增常用组件 —— 表单 · 展示 · 浮层 · 滚动", txt, Style().left());
            ui.pop_clip();
        }

        const Rect body = page.fill();
        Stack cols = ui.hbox(body, 16.0f);

        // ===================== 第 1 列：表单补充 =====================
        {
            const Rect cp = cols.part(0, 3);
            ui.panel(cp);
            Stack c = ui.vbox(cp, 10.0f, 16.0f);
            ui.subheading("表单补充", c.next(24.0f), Style().left());

            ui.muted_text("radio_group 单选组", c.next(16.0f), Style().left());
            ui.radio_group(plans, &radio_idx, c.next(66.0f), Style().font(13.0f));

            ui.muted_text("stepper 数值步进", c.next(16.0f), Style().left());
            ui.stepper(&step_val, 1.0f, 0.0f, 99.0f, c.next(34.0f));

            ui.muted_text("rating 星级评分（再点当前星清零）", c.next(16.0f), Style().left());
            ui.rating(&stars, 5, c.next(26.0f));

            ui.muted_text("textarea 多行文本框", c.next(16.0f), Style().left());
            ui.textarea(&note, c.fill(), Style().hint("支持多行输入，Enter 换行"));
        }

        // ===================== 第 2 列：展示补充 =====================
        {
            const Rect cp = cols.part(1, 3);
            ui.panel(cp);
            Stack c = ui.vbox(cp, 10.0f, 16.0f);
            ui.subheading("展示补充", c.next(24.0f), Style().left());

            Stack icons = c.hbox(56.0f, 12.0f);
            ui.avatar(uname, icons.part(0, 3));
            // spinner disabled for attribution
            {   // 右侧一格：键帽一行 + 说明
                Stack sub = ui.vbox(icons.part(2, 3), 6.0f);
                Stack kr = sub.hbox(22.0f, 5.0f);
                ui.kbd("Ctrl", kr.next_text("Ctrl", 16.0f, 11.0f, 500));
                ui.muted_text("+", kr.next(8.0f), Style().center());
                ui.kbd("K", kr.next_text("K", 18.0f, 11.0f, 500));
                ui.muted_text("avatar / spinner / kbd", sub.fill(), Style().font(11.0f).left());
            }

            ui.alert("布局器已生效",
                     "所有坐标由 Stack 推导，没有一处手算值。",
                     c.next(72.0f));

            ui.breadcrumb({ "首页", "组件", "展示补充" }, c.next(22.0f));

            {
                const Rect ar = c.next(38.0f);
                if (ui.accordion("accordion 折叠面板（点击展开 / 收起）", &acc_open, ar)) {}
                if (acc_open) {
                    ui.muted_text("这里就是被展开出来的内容块 —— 由调用方自己画。",
                                  { ar.x + 12.0f, ar.bottom() + 2.0f, ar.w - 12.0f, 18.0f }, Style().left());
                    c.skip(20.0f);
                }
            }

            ui.table(heads, rows, c.fill(), Style().font(12.0f));
        }

        // ============ 第 3 列：图表 / 浮层 / 滚动 ============
        {
            const Rect cp = cols.part(2, 3);
            ui.panel(cp);
            Stack c = ui.vbox(cp, 10.0f, 16.0f);
            ui.subheading("图表 / 浮层 / 滚动", c.next(24.0f), Style().left());

            ui.sparkline(wave, c.next(48.0f));
            ui.bars(bars_v, c.next(52.0f), Style());

            {   // menu / tooltip 宿主 / dialog 触发
                Stack row = c.hbox(34.0f, 8.0f);
                if (ui.menu("操作", menu_items, &menu_pick, row.part(0, 3), Style().outline())) {
                    picked_log.push_back(menu_items[size_t(menu_pick)]);
                    toast_left = 2.6f;
                }
                {   // tooltip 的锚点就是按钮本身
                    const Rect tb = row.part(1, 3);
                    ui.button("悬停这里", tb, Style().outline());
                    ui.tooltip(tb, "tooltip：鼠标移开就消失");
                }
                if (ui.button("打开对话框", row.part(2, 3), Style().primary())) dlg_open = true;
            }

            ui.muted_text("scroll 滚动容器（滚轮生效）", c.next(16.0f), Style().left());
            const Rect sv = c.fill();
            const float content_h = 34.0f * 12.0f;
            ui.begin_scroll(sv, content_h);
            for (int i = 0; i < 12; ++i) {
                const Rect r = ui.row(34.0f);
                ui.panel(r.inset(0.0f, 2.0f), Style().background(th.muted));
                char buf[64];
                std::snprintf(buf, sizeof(buf), "列表项 %02d    scroll_y = %.1f", i + 1, ui.scroll_y());
                ui.label(buf, Rect(r.x + 12.0f, r.y + 2.0f, r.w - 40.0f, r.h - 4.0f), Style().left());
            }
            ui.end_scroll();
        }

        // ---------------------------- toast ----------------------------
        if (toast_left > 0.0f) {
            const float a = std::min(1.0f, toast_left / 0.25f);
            const Rect tr(ui.width() - 320.0f, ui.height() - 68.0f, 300.0f, 44.0f);
            ui.toast(picked_log.empty() ? "已保存" : ("已选择：" + picked_log.back()),
                     tr, Style().font(13.0f).background(th.popover.alpha255(int(240.0f * a))));
        }

        // ---------------------------- dialog ----------------------------
        // 约定：放在帧回调最后，返回 true 时紧接着画自己的内容 —— 天然在遮罩之上。
        const Rect dlg = Rect::centered(ui.width() * 0.5f, ui.height() * 0.5f, 420.0f, 210.0f);
        if (ui.dialog("确认删除", "删除后无法恢复。这个对话框的遮罩、面板、关闭按钮都由 dialog() 负责，下面两个按钮是调用方画的。",
                      &dlg_open, dlg)) {
            Stack b = ui.hbox(Rect(dlg.x + 24.0f, dlg.bottom() - 24.0f - 34.0f, dlg.w - 48.0f, 34.0f), 10.0f);
            if (ui.button("取消", b.part(0, 2), Style().outline())) dlg_open = false;
            if (ui.button("确认删除", b.part(1, 2), Style().destructive())) {
                dlg_open = false;
                toast_left = 2.6f;
                picked_log.push_back("删除");
            }
        }
    });

    return 0;
}
