// =============================================================================
//  aino 组件画廊 —— 单窗口 + 侧边栏分页
//
//  分类对齐 eui-neo 的侧边栏：按**域**分页，不按控件家族堆页面。
//    概览 / 控件 / 展示 / 布局 / 浮层 / 动效      ← 六个域，各一页
//  于是"按钮"和"表单"不再各占一页（它们都是控件），"滚动"也不单独成页
//  （滚动就是布局的一部分，而且和虚拟列表是**同一个** API）。
//
//  组合方式：
//    · 根布局只是一个 hbox：左 = 导航栏，右 = 页面宿主；
//    · 每一页是一个独立的 composeXxx()，页面宿主按状态挑一个来画；
//    · 浮层（dialog / toast / menu / tooltip）最后画 —— 天然盖在所有内容之上；
//    · 全窗口、全状态都在 main 里的一份 AppState，立即模式不需要控件树。
//
//  作者：sudoevolve <sudoevolve@gmail.com>
//  编译：cl /std:c++17 /EHsc /O2 /utf-8 gallery.cpp user32.lib gdi32.lib
// =============================================================================
#define AINO_IMPLEMENTATION
#include "../aino.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using aino::Color;
using aino::Rect;
using aino::Stack;
using aino::Style;
using aino::Ui;

// 注意：本文件用的是直白的中文字面量，依赖 /utf-8（CMakeLists 里已加）。
// 若直接用 cl 编（不带 /utf-8），字面量会变成 GBK —— to_wide() 的 CP_UTF8→CP_ACP
// 回退能兜住绝大多数串，但 GBK 里存在"本身就是合法 UTF-8"的歧义字节，那种救不回来。

namespace {

// =============================================================================
//  页面清单 —— 六个域，加一页就是往这三张表里各加一行
// =============================================================================
enum Page {
    PG_OVERVIEW, PG_CONTROLS, PG_DISPLAY, PG_LAYOUT, PG_OVERLAYS, PG_MOTION, PG_COUNT
};

const char* kNavy[PG_COUNT] = {
    "概览", "控件", "展示", "布局", "浮层", "动效"
};
const char* kTitle[PG_COUNT] = {
    "概览", "控件", "展示", "布局与滚动", "浮层", "动效与变换"
};
const char* kDesc[PG_COUNT] = {
    "文本、容器、徽标、进度条，外加一页实测数据。",
    "按钮六种变体与按压反馈，以及输入、选择、表单控件。",
    "头像、提示条、表格、折线、柱状，以及图元自绘。",
    "坐标全由 Stack 推导；滚动与虚拟列表是同一个视口。",
    "下拉菜单、对话框、轻提示、悬停提示。",
    "补间、缓动，以及平移 / 缩放 / 旋转。",
};

// =============================================================================
//  应用状态 —— 立即模式里状态由调用方持有，这就是它能这么小的原因
// =============================================================================
struct AppState {
    bool dark = false;
    int  page = PG_OVERVIEW;
    int  clicks = 0;
    std::string last_action = "（还没点过任何按钮）";

    // 控件
    std::string email, search, name, note = "支持多行输入：\nEnter 换行，长文会自动折行";
    bool  remember = true, autosave = true, notifications = false, dark_auto = false;
    bool  agree = true;
    float volume = 0.62f, level = 0.4f, step_val = 12.0f;
    int   plan = 1, tab = 0, range = 0, radio_idx = 1, stars = 4;

    // 展示
    std::string uname = "张三";
    bool  acc_open = true;

    // 浮层
    bool  dlg_open = false;
    int   menu_pick = -1;
    float toast_left = 0.0f;
    std::string toast_msg = "已保存";

    // 布局 / 滚动
    int   scroll_mode = 0;      // 0 交给滚轮 / 1 回顶 / 2 到底
    int   jump_bottom = 0;      // 虚拟列表：跳到底部

    // 动效
    bool  motion_on = false;
};

// -----------------------------------------------------------------------------
//  样例数据
// -----------------------------------------------------------------------------
const std::vector<std::string> kPlans = { "免费版", "专业版", "企业版" };
const std::vector<std::string> kRanges = { "今天", "本周", "本月" };
const std::vector<std::string> kMenuItems = { "重命名", "复制", "导出为 PNG", "删除" };
const std::vector<std::string> kTabs = { "概览", "分析", "设置" };
const std::vector<std::string> kBreadcrumb = { "首页", "组件", "展示" };

const std::vector<std::string> kHeads = { "模块", "状态", "耗时" };
const std::vector<std::vector<std::string>> kRows = {
    { "解析配置", "完成",   "12 ms" },
    { "加载主题", "完成",   "3 ms"  },
    { "构建布局", "完成",   "8 ms"  },
    { "光栅化",   "进行中", "41 ms" },
    { "事件派发", "完成",   "1 ms"  },
};
const std::vector<std::string> kFactHeads = { "指标", "值" };
const std::vector<std::vector<std::string>> kFactRows = {
    { "第三方依赖", "0 个"        },
    { "空闲 CPU",   "0.00%"      },
    { "文字",       "系统字体引擎" },
    { "几何",       "自绘 AA"     },
};

// =============================================================================
//  几个小工具 —— 让每一页的代码读起来像"排版"而不是"算坐标"
// =============================================================================
// 小节标题：只写"这是哪个组件"，不写解释。
void section_label(Ui& ui, Stack& c, const std::string& text, float gap = 4.0f) {
    if (gap > 0.0f) c.skip(gap);
    ui.muted_text(text, c.next(18.0f), Style().font(11.5f).left());
}

// =============================================================================
//  侧边栏（对应 eui-neo 的 composeNavigation）
// =============================================================================
void composeSidebar(Ui& ui, AppState& s, const Rect& sb) {
    const aino::Theme& th = ui.theme();
    const Color sb_bg = th.background.mix(th.muted, 0.6f);

    // 侧栏底色比内容区低一档，右侧一条 1px 分界线
    ui.fill_rect(sb, sb_bg, 0.0f);
    ui.fill_rect(Rect(sb.right() - 1.0f, sb.y, 1.0f, sb.h), th.border, 0.0f);

    Stack c = ui.vbox(sb, 3.0f, 20.0f);
    ui.heading("aino", c.next(32.0f), Style().font(22.0f).left());
    c.skip(2.0f);
    ui.muted_text("单头文件 GUI 库 · 0.2.1", c.next(18.0f), Style().font(11.5f).left());
    c.skip(10.0f);
    ui.separator(c.next(1.0f));
    c.skip(10.0f);

    // ---- 导航项：当前页高亮，点一下换页 ----
    for (int i = 0; i < PG_COUNT; ++i) {
        if (ui.nav_item(kNavy[i], s.page == i, c.next(34.0f)) && s.page != i) {
            s.page = i;
            s.menu_pick = -1;
            s.scroll_mode = 0;
            s.jump_bottom = 0;
        }
    }

    // ---- 底部：主题切换。用 skip 把这一组顶到最下面，仍然不出现手算坐标 ----
    {
        Stack f = ui.vbox(c.fill(), 6.0f);
        f.skip(std::max(f.remain() - 67.0f, 0.0f));      // 1 + 6 + 36 + 6 + 18
        ui.separator(f.next(1.0f));
        Stack row = f.hbox(f.next(36.0f), 10.0f);
        const Rect tb = row.next(36.0f);
        if (ui.icon_button("", tb, Style().outline())) s.dark = !s.dark;

        // 太阳 / 月亮自绘 —— 字体里的太阳/月亮符号缺字形会变方块
        {
            const Color ic = th.foreground;
            const float bx = tb.cx(), by = tb.cy();
            if (s.dark) {
                ui.fill_circle(bx, by, 5.0f, ic);
                for (int k = 0; k < 8; ++k) {
                    const float a = float(k) * 3.14159265f / 4.0f;
                    ui.draw_line(bx + std::cos(a) * 7.6f, by + std::sin(a) * 7.6f,
                                 bx + std::cos(a) * 10.2f, by + std::sin(a) * 10.2f, ic, 1.6f);
                }
            } else {
                // 月牙 = 圆减圆（几何减法）。以前是"画个圆再用侧栏底色挖掉一块"，
                // 底色一变（悬停高亮、换主题、图片底）就露出一块错的色块 —— 会穿帮。
                // 光心故意右移 0.8：月牙的重心偏左，圆心上对中看着反而偏左。
                const float mx = bx + 0.8f;
                ui.fill_crescent(mx, by, 8.0f, mx + 3.6f, by - 2.8f, 8.6f, ic);
            }
        }
        const Rect lr = row.rest();
        ui.muted_text(s.dark ? "深色主题" : "浅色主题",
                      Rect(lr.x, lr.y + 9.0f, lr.w, 18.0f), Style().font(12.0f).left());
        ui.muted_text("单头文件 · 零第三方依赖", f.next(18.0f), Style().font(11.0f).left());
    }
}

// =============================================================================
//  页面 1：概览
// =============================================================================
void composeOverview(Ui& ui, AppState& s, const Rect& box) {
    (void)s;
    Stack cols = ui.hbox(box, 16.0f);

    // ---- 文本 ----
    {
        const Rect cp = cols.part(0, 3);
        ui.panel(cp);
        Stack c = ui.vbox(cp, 8.0f, 18.0f);
        ui.subheading("文本", c.next(26.0f), Style().left());
        ui.heading("标题 heading", c.next(34.0f), Style().font(20.0f).left());
        ui.subheading("副标题 subheading", c.next(26.0f), Style().left());
        ui.label("正文 label", c.next(22.0f), Style().left());
        ui.muted_text("次要说明 muted_text", c.next(20.0f), Style().left());
        ui.code_text("代码片段 code_text", c.next(30.0f), Style().font(13.0f));
    }

    // ---- 容器 ----
    {
        const Rect cp = cols.part(1, 3);
        ui.panel(cp);
        Stack c = ui.vbox(cp, 8.0f, 18.0f);
        ui.subheading("容器", c.next(26.0f), Style().left());

        section_label(ui, c, "badge", 0.0f);
        {
            Stack r = c.hbox(22.0f, 6.0f);
            ui.badge("New", r.next(52.0f));
            ui.badge("已验证", r.next(62.0f));
            Style red; red.variant = Style::Destructive;
            ui.badge("失败", r.next(52.0f), red);
        }

        section_label(ui, c, "progress");
        ui.progress(0.68f, c.next(8.0f));
        c.skip(6.0f);
        ui.progress(0.32f, c.next(8.0f));

        section_label(ui, c, "skeleton");
        ui.skeleton(c.next(16.0f));
        c.skip(6.0f);
        ui.skeleton(c.next(16.0f));
    }

    // ---- 实测数据 ----
    {
        const Rect cp = cols.part(2, 3);
        ui.panel(cp);
        Stack c = ui.vbox(cp, 8.0f, 18.0f);
        ui.subheading("实测数据", c.next(26.0f), Style().left());
        c.skip(6.0f);
        // 高度交给布局器：表头 36 + 每行 34，写死高度一定会把最后一行切掉
        ui.table(kFactHeads, kFactRows, c.fill(), Style().font(12.0f));
    }
}

// =============================================================================
//  页面 2：控件 —— 按钮 + 输入 + 选择（原来"按钮""表单"各占一页，其实同域）
// =============================================================================
void composeControls(Ui& ui, AppState& s, const Rect& box) {
    Stack cols = ui.hbox(box, 16.0f);

    {   // 按钮
        const Rect cp = cols.part(0, 3);
        ui.panel(cp);
        Stack c = ui.vbox(cp, 8.0f, 18.0f);
        ui.subheading("按钮", c.next(26.0f), Style().left());

        section_label(ui, c, "六种语义变体", 0.0f);
        {
            const Style::Variant vs[] = { Style::Default, Style::Secondary, Style::Outline,
                                          Style::Ghost, Style::Destructive, Style::Link };
            const char* names[] = { "Default", "Secondary", "Outline", "Ghost", "Destructive", "Link" };
            Stack b = ui.vbox(c.next(246.0f), 6.0f);
            for (int i = 0; i < 6; ++i) {
                Style st;
                st.variant = vs[i];
                if (ui.button(names[i], b.next(36.0f), st)) { ++s.clicks; s.last_action = names[i]; }
            }
        }

        section_label(ui, c, "disabled / press_scale");
        {
            Stack r = c.hbox(36.0f, 8.0f);
            Style dis; dis.disabled = true;
            ui.button("禁用", r.next(88.0f), dis);
            Style pressy; pressy.variant = Style::Secondary; pressy.press_scale = 0.96f;
            if (ui.button("按我看看", r.next(96.0f), pressy)) ++s.clicks;
        }

        section_label(ui, c, "icon_button");
        {
            Stack r = c.hbox(36.0f, 8.0f);
            if (ui.icon_button("+", r.next(36.0f), Style().outline())) { ++s.clicks; s.last_action = "图标 +"; }
            if (ui.icon_button("-", r.next(36.0f), Style().outline())) { ++s.clicks; s.last_action = "图标 -"; }
            if (ui.icon_button("×", r.next(36.0f), Style().outline())) { ++s.clicks; s.last_action = "图标 ×"; }
        }

        ui.muted_text(std::to_string(s.clicks) + " 次点击 · " + s.last_action,
                      c.fill(), Style().font(11.5f).left().top());
    }

    {   // 输入
        const Rect cp = cols.part(1, 3);
        ui.panel(cp);
        Stack c = ui.vbox(cp, 8.0f, 18.0f);
        ui.subheading("输入", c.next(26.0f), Style().left());

        section_label(ui, c, "input / textarea", 0.0f);
        ui.input(&s.email, c.next(36.0f), Style().hint("you@example.com"));
        c.skip(2.0f);
        ui.input(&s.search, c.next(36.0f), Style().hint("输入关键词…"));
        c.skip(2.0f);
        ui.textarea(&s.note, c.next(92.0f), Style().font(13.0f));

        section_label(ui, c, "checkbox / toggle");
        ui.checkbox(&s.remember, "记住这台设备", c.next(22.0f));
        ui.checkbox(&s.autosave, "自动保存", c.next(22.0f));
        {
            const Rect r = c.next(26.0f);
            ui.label("桌面通知", r, Style().left());
            ui.toggle(&s.notifications, Rect(r.right() - 44.0f, r.y, 44.0f, 26.0f));
        }
    }

    {   // 选择
        const Rect cp = cols.part(2, 3);
        ui.panel(cp);
        Stack c = ui.vbox(cp, 7.0f, 18.0f);
        ui.subheading("选择", c.next(26.0f), Style().left());

        section_label(ui, c, "slider", 0.0f);
        ui.slider(&s.volume, 0.0f, 1.0f, c.next(24.0f));
        section_label(ui, c, "select");
        ui.select(kPlans, &s.plan, c.next(36.0f), Style().hint("选择套餐"));
        section_label(ui, c, "tabs");
        ui.tabs(kTabs, &s.tab, c.next(36.0f));
        section_label(ui, c, "segmented");
        ui.segmented(kRanges, &s.range, c.next(32.0f));
        section_label(ui, c, "radio_group");
        ui.radio_group(kPlans, &s.radio_idx, c.next(66.0f), Style().font(13.0f));
        section_label(ui, c, "stepper");
        ui.stepper(&s.step_val, 1.0f, 0.0f, 99.0f, c.next(34.0f));
        section_label(ui, c, "rating");
        ui.rating(&s.stars, 5, c.next(26.0f));
    }
}

// =============================================================================
//  页面 3：展示 —— 媒体、提示、表格、曲线、图元
// =============================================================================
void composeDisplay(Ui& ui, AppState& s, const Rect& box) {
    Stack cols = ui.hbox(box, 16.0f);

    {   // 媒体 / 状态
        const Rect cp = cols.part(0, 3);
        ui.panel(cp);
        Stack c = ui.vbox(cp, 8.0f, 18.0f);
        ui.subheading("头像 / 指示器 / 键帽", c.next(26.0f), Style().left());

        Stack icons = c.hbox(64.0f, 12.0f);
        ui.avatar(s.uname, icons.next(64.0f));
        ui.spinner(icons.next(64.0f));

        Stack kr = c.hbox(22.0f, 5.0f);
        ui.kbd("Ctrl", kr.next_text("Ctrl", 16.0f, 11.0f, 500));
        ui.muted_text("+", kr.next(10.0f), Style().center());
        ui.kbd("K", kr.next_text("K", 18.0f, 11.0f, 500));

        ui.separator(c.next(1.0f));
        section_label(ui, c, "skeleton（悬停呼吸）", 0.0f);
        ui.skeleton(c.next(14.0f), Style().animate());
        ui.skeleton(c.next(14.0f), Style().animate());

        ui.separator(c.next(1.0f));
        section_label(ui, c, "accordion", 0.0f);
        {
            const Rect ar = c.next(38.0f);
            ui.accordion("点我展开 / 收起", &s.acc_open, ar);
            if (s.acc_open) {
                ui.muted_text("展开区域由调用方自己画",
                              Rect(ar.x + 12.0f, ar.bottom() + 4.0f, ar.w - 12.0f, 18.0f),
                              Style().font(12.0f).left());
                c.skip(24.0f);
            }
        }
    }

    {   // 提示条 / 表格
        const Rect cp = cols.part(1, 3);
        ui.panel(cp);
        Stack c = ui.vbox(cp, 8.0f, 18.0f);
        ui.subheading("提示条 / 表格", c.next(26.0f), Style().left());

        // 高度不再拍脑袋写 74 —— 正文一折行就不够了。用 alert_height() 量：
        // 它跑的是和绘制完全同一套折行规则，所以是量出来的而不是估出来的。
        const float aw = c.area.w;
        {
            const char* t = "布局器已生效";
            const char* d = "所有坐标由 Stack 推导，没有一处手算值。";
            ui.alert(t, d, c.next(ui.alert_height(t, d, aw)));
        }
        {
            Style danger; danger.variant = Style::Destructive;
            const char* t = "删除后无法恢复";
            const char* d = "这条提示条的正文会按宽度自动折行。";
            ui.alert(t, d, c.next(ui.alert_height(t, d, aw, danger)), danger);
        }

        section_label(ui, c, "breadcrumb");
        ui.breadcrumb(kBreadcrumb, c.next(22.0f));

        section_label(ui, c, "table");
        ui.table(kHeads, kRows, c.fill(), Style().font(12.0f));
    }

    {   // 曲线 / 进度 / 图元
        const Rect cp = cols.part(2, 3);
        ui.panel(cp);
        Stack c = ui.vbox(cp, 8.0f, 18.0f);
        ui.subheading("曲线 / 进度 / 图元", c.next(26.0f), Style().left());

        const std::vector<float> wave = [] {
            std::vector<float> w(24);
            for (size_t i = 0; i < w.size(); ++i)
                w[i] = std::sin(float(i) * 0.55f) * 0.6f + 0.35f * std::sin(float(i) * 1.7f) + 0.5f;
            return w;
        }();
        const std::vector<float> bars = [] {
            std::vector<float> b(12);
            for (size_t i = 0; i < b.size(); ++i)
                b[i] = (i == 7) ? -0.45f : 0.25f + 0.6f * std::fabs(std::sin(float(i) * 0.9f + 1.3f));
            return b;
        }();
        ui.sparkline(wave, c.next(104.0f));
        ui.bars(bars, c.next(104.0f));

        section_label(ui, c, "progress");
        ui.progress(0.9f, c.next(8.0f));
        ui.progress(0.55f, c.next(8.0f));
        ui.progress(0.2f, c.next(8.0f));

        ui.separator(c.next(1.0f));
        section_label(ui, c, "自绘图元", 0.0f);
        {
            const Rect fr = c.fill();
            // 四个形状横向等分：位置全由 fr.w 算，不用 72/84/182 这种硬编码偏移。
            // 第四个月牙走 fill_crescent（几何减法）—— 以前是"再画一个 muted 色的圆
            // 盖上去"挖出来的，底色一换就露出一块错色，也就是会穿帮。
            const float cell = fr.w / 4.0f;
            const float bw = std::max(cell - 14.0f, 20.0f);
            const float cy = fr.y + 22.0f;
            auto mid = [&](int i) { return fr.x + cell * float(i) + cell * 0.5f; };

            ui.fill_rect(Rect(mid(0) - bw * 0.5f, fr.y, bw, 44.0f), ui.theme().foreground, 10.0f);
            ui.stroke_rect(Rect(mid(1) - bw * 0.5f, fr.y, bw, 44.0f), ui.theme().foreground, 10.0f, 1.5f);
            ui.fill_circle(mid(2), cy, 22.0f, ui.theme().foreground);
            ui.fill_crescent(mid(3), cy, 22.0f, mid(3) + 8.5f, cy - 5.0f, 21.8f, ui.theme().foreground);

            // 五条粗细递增的线段，按可用宽度等分
            for (int k = 0; k < 5; ++k) {
                const float step = fr.w / 5.0f;
                const float x0 = fr.x + step * float(k);
                ui.draw_line(x0, fr.y + 62.0f, x0 + step * 0.62f, fr.y + 92.0f,
                             ui.theme().foreground, 1.0f + float(k) * 0.7f);
            }
        }
    }
}

// =============================================================================
//  页面 4：布局与滚动 —— Stack 推导坐标；滚动与虚拟列表是同一个视口
// =============================================================================
void composeLayout(Ui& ui, AppState& s, const Rect& body) {
    Stack page = ui.vbox(body, 16.0f);

    // ---- 上半：布局器 ----
    {
        Stack row = page.hbox(300.0f, 16.0f);

        {   // 统一间距
            const Rect lp = row.part(0, 3);
            ui.panel(lp);
            Stack c = ui.vbox(lp, 8.0f, 18.0f);
            ui.subheading("统一间距", c.next(26.0f), Style().left());
            ui.muted_text("只给高度，位置交给布局器", c.next(18.0f), Style().left());
            ui.input(&s.name, c.next(36.0f), Style().hint("输入名字"));
            ui.checkbox(&s.agree, "同意条款", c.next(22.0f));
            ui.checkbox(&s.autosave, "自动保存", c.next(22.0f));
            ui.separator(c.next(1.0f));
            ui.slider(&s.level, 0.0f, 1.0f, c.next(24.0f));
            ui.button("确认", c.next(38.0f), Style().primary());
        }

        {   // 等分 + 嵌套
            const Rect rp = row.part(1, 3);
            ui.panel(rp);
            Stack c = ui.vbox(rp, 10.0f, 18.0f);
            ui.subheading("等分 part(i, 3)", c.next(26.0f), Style().left());
            {
                Stack t = c.hbox(32.0f, 8.0f);
                if (ui.button("一", t.part(0, 3), Style().outline())) ++s.clicks;
                if (ui.button("二", t.part(1, 3), Style().outline())) ++s.clicks;
                if (ui.button("三", t.part(2, 3), Style().outline())) ++s.clicks;
            }
            section_label(ui, c, "嵌套 + 按文字宽量", 0.0f);
            {
                Stack b = c.hbox(36.0f, 10.0f);
                const Rect b1 = b.next_text("好", 34.0f);
                const Rect b2 = b.next_text("取消", 34.0f);
                const Rect b3 = b.next_text("保存并关闭", 34.0f);
                if (ui.button("好", b1, Style().outline())) ++s.clicks;
                if (ui.button("取消", b2, Style().outline())) ++s.clicks;
                if (ui.button("保存并关闭", b3, Style().primary())) ++s.clicks;
            }
            {
                const Rect rest = c.fill();
                Stack outer = ui.vbox(rest, 8.0f);
                Stack inner = outer.hbox(150.0f, 8.0f);
                const Rect a = inner.part(0, 3);
                ui.panel(a, Style().background(ui.theme().muted));
                Stack sub = ui.vbox(a, 6.0f, 10.0f);
                ui.muted_text("子布局", sub.next(18.0f), Style().left());
                if (ui.button("深一层", sub.fill(), Style().outline())) ++s.clicks;
                ui.panel(inner.part(1, 3), Style().background(ui.theme().muted));
                ui.panel(inner.part(2, 3), Style().background(ui.theme().muted));
                outer.skip(2.0f);
                ui.muted_text("每层都只是再拿一个 Rect 开子布局", outer.next(18.0f), Style().left());
            }
        }

        {   // 滚动（通用）—— 变高内容也用它
            const Rect rp = row.part(2, 3);
            ui.panel(rp);
            Stack c = ui.vbox(rp, 8.0f, 18.0f);
            ui.subheading("滚动 · 通用", c.next(26.0f), Style().left());
            ui.muted_text("14 项；滚轮或按钮，同一个视口", c.next(18.0f), Style().left());

            Stack br = c.hbox(28.0f, 8.0f);
            if (ui.button("回顶", br.part(0, 2), Style().outline())) s.scroll_mode = 1;
            if (ui.button("到底", br.part(1, 2), Style().outline())) s.scroll_mode = 2;

            const Rect sv = [&] { Rect r = c.fill(); r.h = std::max(r.h - 10.0f, 1.0f); return r; }();
            const float item_h = 30.0f;
            const int   n = 14;
            {
                // 作用域即视口：析构自动收尾（画滚动条、弹裁剪）
                auto sc = ui.scroll(sv, item_h * float(n));
                if (s.scroll_mode == 1) { sc.to(0.0f); s.scroll_mode = 0; }
                if (s.scroll_mode == 2) { sc.to(1e6f); s.scroll_mode = 0; }
                for (int i = 0; i < n; ++i) {
                    const Rect r = ui.row(item_h);
                    ui.panel(r.inset(0.0f, 2.0f), Style().background(ui.theme().muted));
                    char buf[80];
                    std::snprintf(buf, sizeof(buf), "列表项 %02d      y = %.0f", i + 1, sc.y());
                    ui.label(buf, Rect(r.x + 12.0f, r.y + 2.0f, r.w - 46.0f, r.h - 4.0f), Style().left());
                }
            }
        }
    }

    // ---- 下半：虚拟列表 / 裁剪 / 对照表 ----
    {
        const Rect r2 = page.fill();
        Stack row = ui.hbox(r2, 16.0f);

        {   // 虚拟列表：10 万行只画一屏
            const Rect lp = row.part(0, 3);
            ui.panel(lp);
            Stack c = ui.vbox(lp, 8.0f, 18.0f);
            ui.subheading("滚动 · 虚拟（10 万行）", c.next(26.0f), Style().left());
            ui.muted_text("固定行高，只对可见行调用回调", c.next(18.0f), Style().left());

            Stack br = c.hbox(28.0f, 8.0f);
            if (ui.button("跳到底部", br.part(0, 2), Style().outline())) s.jump_bottom = 1;

            const float item_h = 26.0f;
            const int   n = 100000;
            const Rect list = c.next(std::max(c.remain() - 20.0f, 1.0f));
            if (s.jump_bottom == 1) { ui.scroll_to_index(list, n - 1, item_h, n); s.jump_bottom = 0; }

            // 和上面那个"通用"滚动是同一个函数：内容高度 = n × item_h
            const int drawn = ui.scroll(list, n, item_h, [&](int i, const Rect& r) {
                if (i & 1) ui.fill_rect(r, ui.theme().muted.with_alpha(90), 0.0f);
                char b[64];
                std::snprintf(b, sizeof(b), "第 %d 行", i + 1);
                ui.label(b, Rect(r.x + 10.0f, r.y, r.w - 20.0f, r.h), Style().left().font(12.5f));
            });

            char foot[96];
            std::snprintf(foot, sizeof(foot), "共 %d 行，本帧绘制 %d 行", n, drawn);
            ui.muted_text(foot, c.fill(), Style().font(11.5f).left().top());
        }

        {   // 裁剪
            const Rect cp = row.part(1, 3);
            ui.panel(cp);
            Stack c = ui.vbox(cp, 8.0f, 18.0f);
            ui.subheading("裁剪", c.next(26.0f), Style().left());
            ui.muted_text("几何与文字一起裁，可嵌套", c.next(18.0f), Style().left());

            const Rect cr = c.next(84.0f);
            ui.panel(cr, Style().background(ui.theme().muted));
            ui.push_clip(cr);
            // 故意画一串超出盒子的文字，验证裁剪
            ui.label("这一行文字故意写得非常长非常长非常长，长到一定超出这个盒子",
                     Rect(cr.x + 10.0f, cr.y + 18.0f, cr.w, 22.0f), Style().left());
            ui.label("第二行也一样，右边会被切掉而不是爬出面板",
                     Rect(cr.x + 10.0f, cr.y + 44.0f, cr.w, 22.0f), Style().left());
            ui.pop_clip();
            section_label(ui, c, "嵌套裁剪 = 取交集", 0.0f);
            {
                const Rect nr = c.next(56.0f);
                ui.panel(nr, Style().background(ui.theme().muted));
                ui.push_clip(nr);
                const Rect half(nr.x + nr.w * 0.5f - 20.0f, nr.y + 6.0f, nr.w, nr.h - 12.0f);
                ui.push_clip(half);
                ui.fill_rect(Rect(nr.x + 8.0f, nr.y + 10.0f, nr.w - 16.0f, nr.h - 20.0f),
                             ui.theme().primary, 6.0f);
                ui.pop_clip();
                ui.pop_clip();
            }
        }

        {   // 对照表
            const Rect cp = row.part(2, 3);
            ui.panel(cp);
            Stack c = ui.vbox(cp, 8.0f, 18.0f);
            ui.subheading("同一个 scroll()", c.next(26.0f), Style().left());
            ui.muted_text("两者只差一个内容高度的算法", c.next(18.0f), Style().left());
            const std::vector<std::string> heads = { "形态", "内容高度", "绘制" };
            const std::vector<std::vector<std::string>> rows = {
                { "通用", "content_h", "全部" },
                { "虚拟", "n×行高",     "可见行" },
                { "偏移", "按 box 记",  "共用" },
                { "条",   "同一根",     "同一套" },
            };
            ui.table(heads, rows, c.fill(), Style().font(12.0f));
        }
    }
}

// =============================================================================
//  页面 5：浮层
// =============================================================================
void composeOverlays(Ui& ui, AppState& s, const Rect& box) {
    Stack cols = ui.hbox(box, 16.0f);

    {   // 触发器
        const Rect cp = cols.part(0, 3);
        ui.panel(cp);
        Stack c = ui.vbox(cp, 10.0f, 18.0f);
        ui.subheading("触发器", c.next(26.0f), Style().left());

        section_label(ui, c, "menu", 0.0f);
        if (ui.menu("操作", kMenuItems, &s.menu_pick, c.next(36.0f), Style().outline())) {
            s.toast_msg = "已选择：" + kMenuItems[size_t(s.menu_pick)];
            s.toast_left = 2.6f;
        }

        section_label(ui, c, "dialog");
        if (ui.button("打开对话框", c.next(36.0f), Style().primary())) s.dlg_open = true;

        section_label(ui, c, "toast");
        if (ui.button("弹一个 toast", c.next(36.0f), Style().secondary())) {
            s.toast_msg = "操作已完成";
            s.toast_left = 2.6f;
        }

        section_label(ui, c, "tooltip");
        {
            const Rect tb = c.next(36.0f);
            ui.button("把鼠标停在这里", tb, Style().outline());
            ui.tooltip(tb, "tooltip：鼠标移开就消失");
        }
    }

    {   // 锚点
        const Rect cp = cols.part(1, 3);
        ui.panel(cp);
        Stack c = ui.vbox(cp, 10.0f, 18.0f);
        ui.subheading("任意锚点", c.next(26.0f), Style().left());
        c.skip(6.0f);

        {
            Stack r = c.hbox(30.0f, 10.0f);
            const Rect a = r.next(30.0f);
            ui.badge("New", a);
            ui.tooltip(a, "badge 也能挂提示");
            const Rect b = r.next_text("Ctrl", 18.0f, 11.0f, 500);
            ui.kbd("Ctrl", b);
            ui.tooltip(b, "键帽");
        }
        c.skip(10.0f);
        {
            Stack r = c.hbox(36.0f, 10.0f);
            const Rect a = r.next(84.0f);
            if (ui.button("按钮", a, Style().outline())) ++s.clicks;
            ui.tooltip(a, "按钮");
            const Rect b = r.next(36.0f);
            ui.avatar("李", b);
            ui.tooltip(b, "头像");
        }
        c.skip(10.0f);
        section_label(ui, c, "select", 0.0f);
        int tmp = s.plan;
        ui.select(kPlans, &tmp, c.next(36.0f), Style().hint("点我展开"));
    }

    {   // 绘制时机
        const Rect cp = cols.part(2, 3);
        ui.panel(cp);
        Stack c = ui.vbox(cp, 10.0f, 18.0f);
        ui.subheading("绘制时机", c.next(26.0f), Style().left());
        c.skip(6.0f);

        const std::vector<std::string> oh = { "组件", "绘制" };
        const std::vector<std::vector<std::string>> orows = {
            { "menu",    "帧末" },
            { "tooltip", "帧末" },
            { "select",  "帧末" },
            { "dialog",  "立即" },
            { "toast",   "立即" },
        };
        ui.table(oh, orows, c.fill(), Style().font(12.0f));
    }
}

// =============================================================================
//  页面 6：动效 —— 补间 + 仿射变换
// =============================================================================
void composeMotion(Ui& ui, AppState& s, const Rect& box) {
    Stack cols = ui.hbox(box, 16.0f);

    // ---- 左：补间 ----
    {
        const Rect cp = cols.part(0, 2);
        ui.panel(cp);
        Stack c = ui.vbox(cp, 10.0f, 18.0f);
        ui.subheading("补间", c.next(26.0f), Style().left());
        section_label(ui, c, "out_cubic / 0.7s / 目标中途改变会从当前值续跑", 0.0f);

        const Rect track = c.next(104.0f);
        ui.panel(track, Style().background(ui.theme().muted).corner(8.0f));

        // 目标只有 0 / 1 两态；缓动交给 tween，帧不用自己算
        const float k = ui.tween(aino::detail::make_id("gallery_tween", track),
                                 s.motion_on ? 1.0f : 0.0f, 0.7f, aino::easing::out_cubic);

        const float bw = 62.0f, bh = 36.0f;
        const float x0 = track.x + 12.0f;
        const float x1 = track.right() - 12.0f - bw;
        const Rect badge(x0 + (x1 - x0) * k, track.cy() - bh * 0.5f, bw, bh);

        // 变换只影响绘制：布局坐标还是 badge 那个矩形，旋转只是"看起来转了"
        aino::Transform tf;
        tf.rotate = -14.0f + 28.0f * k;
        tf.scale  = 0.88f + 0.12f * k;
        tf.origin = aino::Vec2(badge.cx(), badge.cy());
        ui.push_transform(tf);
        ui.panel(badge, Style().background(ui.theme().primary).corner(9.0f));
        ui.label("aino", badge, Style().center().medium()
                 .color(ui.theme().primary_foreground));
        ui.pop_transform();

        if (ui.button(s.motion_on ? "复位" : "播放", c.next(36.0f), Style().outline()))
            s.motion_on = !s.motion_on;

        char buf[96];
        std::snprintf(buf, sizeof(buf), "t = %.3f", double(k));
        ui.label(buf, c.next(18.0f), Style().left().muted_text().font(12.0f));

        ui.separator(c.next(1.0f));

        // 静态演示：三个角度并列，证明旋转文字也是原生渲染（不是拉伸位图）
        section_label(ui, c, "同一段文字，三个角度", 0.0f);
        const Rect strip = c.fill();
        ui.panel(strip, Style().background(ui.theme().muted).corner(8.0f));
        const char* kAngles[3] = { "-18°", "0°", "+18°" };
        const float pitch = (strip.w - 40.0f) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            const float cxx = strip.x + 20.0f + pitch * (float(i) + 0.5f);
            aino::Transform rt;
            rt.rotate = -18.0f + 18.0f * float(i);
            rt.origin = aino::Vec2(cxx, strip.cy());
            ui.push_transform(rt);
            ui.label(kAngles[i], Rect(cxx - 40.0f, strip.cy() - 10.0f, 80.0f, 20.0f),
                     Style().center().font(14.0f));
            ui.pop_transform();
        }
    }

    // ---- 右：变换 ----
    {
        const Rect cp = cols.part(1, 2);
        ui.panel(cp);
        Stack c = ui.vbox(cp, 10.0f, 18.0f);
        ui.subheading("变换", c.next(26.0f), Style().left());
        section_label(ui, c, "translate / scale / rotate / 复合，只影响绘制", 0.0f);

        // 高度 = 方块 76 + 名字 24，名字画在方块正下方，不侵占下一个元素
        const Rect area = c.next(100.0f);
        const float bw = 76.0f, bh = 76.0f;
        const float step = area.w / 4.0f;
        const char* kNames[4] = { "原样", "平移", "放大", "旋转" };

        // 每个格子都用同一套代码画，只改 Transform —— 差别一眼能看出来。
        // 注意 origin 是**世界坐标**（不是相对 rect 的），所以取格子中心。
        for (int i = 0; i < 4; ++i) {
            const Rect cell(area.x + step * float(i) + (step - bw) * 0.5f, area.y, bw, bh);
            aino::Transform t;
            if (i == 1) t.translate = aino::Vec2(8.0f, 6.0f);
            if (i == 2) { t.scale = 1.32f;  t.origin = aino::Vec2(cell.cx(), cell.cy()); }
            if (i == 3) { t.rotate = 20.0f; t.origin = aino::Vec2(cell.cx(), cell.cy()); }
            ui.push_transform(t);
            ui.panel(cell, Style().background(ui.theme().primary).corner(10.0f));
            ui.label("A", cell, Style().center().medium().font(20.0f)
                     .color(ui.theme().primary_foreground));
            ui.pop_transform();
            ui.muted_text(kNames[i], Rect(cell.x, area.bottom() + 4.0f, cell.w, 16.0f),
                          Style().center().font(11.0f));
        }

        // ---- 复合：缩放 + 旋转，用 tween 驱动 ----
        section_label(ui, c, "复合（缩放 × 旋转，补间驱动）");
        {
            const Rect boxc = c.next(120.0f);
            ui.panel(boxc, Style().background(ui.theme().muted).corner(8.0f));
            const float k = ui.tween(aino::detail::make_id("gallery_comp", boxc),
                                     s.motion_on ? 1.0f : 0.0f, 0.7f, aino::easing::out_cubic);
            aino::Transform t;
            t.scale  = 1.0f + 0.45f * k;
            t.rotate = -24.0f * k;
            t.origin = aino::Vec2(boxc.cx(), boxc.cy());
            ui.push_transform(t);
            ui.panel(Rect::centered(boxc.cx(), boxc.cy(), 84.0f, 56.0f),
                     Style().background(ui.theme().foreground).corner(8.0f));
            ui.label("旋转 + 缩放", Rect::centered(boxc.cx(), boxc.cy(), 84.0f, 56.0f),
                     Style().center().font(11.5f).color(ui.theme().background));
            ui.pop_transform();
        }

        const std::vector<std::string> heads = { "属性", "说明" };
        const std::vector<std::vector<std::string>> rows = {
            { "translate", "先平移" },
            { "scale",     "再缩放" },
            { "rotate",    "最后旋转" },
            { "origin",    "绕哪一点" },
        };
        section_label(ui, c, "顺序：T · R · S · T(-origin)", 0.0f);
        ui.table(heads, rows, c.fill(), Style().font(12.0f));
    }
}

void composePage(Ui& ui, AppState& s, const Rect& body) {
    switch (s.page) {
        case PG_OVERVIEW: composeOverview(ui, s, body); break;
        case PG_CONTROLS: composeControls(ui, s, body); break;
        case PG_DISPLAY:  composeDisplay(ui, s, body);  break;
        case PG_LAYOUT:   composeLayout(ui, s, body);   break;
        case PG_OVERLAYS: composeOverlays(ui, s, body); break;
        case PG_MOTION:   composeMotion(ui, s, body);   break;
        default: break;
    }
}

// =============================================================================
//  浮层：永远最后画
// =============================================================================
void composeOverlayLayer(Ui& ui, AppState& s) {
    // toast
    if (s.toast_left > 0.0f) {
        const float a = std::min(1.0f, s.toast_left / 0.25f);
        const Rect tr(ui.width() - 336.0f, ui.height() - 72.0f, 308.0f, 46.0f);
        ui.toast(s.toast_msg, tr,
                 Style().font(13.0f).background(ui.theme().popover.alpha255(int(242.0f * a))));
    }

    // dialog —— 正文故意写长，用来验证折行 + 裁剪
    const Rect dlg = Rect::centered(ui.width() * 0.5f, ui.height() * 0.5f, 470.0f, 236.0f);
    const std::string body =
        "删除后无法恢复。遮罩、面板、右上角的关闭按钮、标题与正文的折行，以及正文被裁进面板，"
        "全部由 dialog() 负责；下面的两个按钮由调用方紧接着它画，所以天然落在面板之上。";
    if (ui.dialog("确认删除", body, &s.dlg_open, dlg)) {
        Stack b = ui.hbox(Rect(dlg.x + 24.0f, dlg.bottom() - 24.0f - 36.0f, dlg.w - 48.0f, 36.0f), 10.0f);
        if (ui.button("取消", b.part(0, 2), Style().outline())) s.dlg_open = false;
        if (ui.button("确认删除", b.part(1, 2), Style().destructive())) {
            s.dlg_open = false;
            s.toast_msg = "已删除";
            s.toast_left = 2.6f;
        }
    }
}

} // namespace

// =============================================================================
//  main —— 组合：根 hbox = 导航栏 + 页面宿主，浮层放最后
// =============================================================================
int main() {
    aino::Window win(1120, 720, "aino gallery");
    if (!win.ok()) return 1;

    AppState app;

    win.run([&](Ui& ui) {
        // 主题要最先处理：这样清屏用的就是新配色
        ui.set_theme(app.dark ? aino::Theme::dark() : aino::Theme::light());
        ui.clear(ui.theme().background);

        // toast 是个短暂的常驻动画；没有它的时候整窗是 0% 空闲 CPU
        if (app.toast_left > 0.0f) { app.toast_left -= ui.frame_dt(); ui.repaint(); }

        // ---- 根布局：左导航 + 右内容 ----
        const float nav_w = 216.0f;
        composeSidebar(ui, app, Rect(0.0f, 0.0f, nav_w, ui.height()));
        const Rect content(nav_w, 0.0f, ui.width() - nav_w, ui.height());

        // ---- 内容区：页头 + 页面本体 ----
        Stack page = ui.vbox(content, 0.0f, 28.0f);
        ui.heading(kTitle[app.page], page.next(34.0f), Style().left());
        ui.muted_text(kDesc[app.page], page.next(20.0f), Style().left());
        page.skip(10.0f);
        ui.separator(page.next(1.0f));
        page.skip(16.0f);
        composePage(ui, app, page.fill());

        // ---- 浮层最后画，天然盖在所有内容之上 ----
        composeOverlayLayer(ui, app);
    });

    return 0;
}
