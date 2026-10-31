// =============================================================================
//  aino 组件画廊
//
//  版式参照 Fluent UI Gallery 与 shadcn/ui 组件文档：
//    左侧  分组导航，一个组件一项
//    右侧  页头（组件名 + 说明 + 对应 shadcn 名称），下面是若干示例卡片
//    卡片  左预览（可直接交互），右源码（等宽、带行号与着色）
//
//  数据驱动。一个组件就是 kPages 里的一项，一项挂 N 个 Demo；
//  Demo = { 名称, 说明, 源码, 预览高度, 绘制函数 }。
//  新增组件只往表里加一行，导航、页头、滚动、卡片布局都不用改。
//
//  示例状态全部集中在 App，绘制函数只读写它。立即模式没有控件对象，
//  所以"这个开关开着吗"就是 App 里的一个 bool，不存在同步问题。
//
//  作者：sudoevolve <sudoevolve@gmail.com>
//  编译：cl /std:c++17 /EHsc /O2 /utf-8 gallery.cpp user32.lib gdi32.lib
//        （必须带 /utf-8；字面量是 UTF-8，MSVC 默认按 GBK 解码）
// =============================================================================
// 让 MSVC 把本文件的窄字面量按 UTF-8 编。头文件里那道 pragma 通常已经覆盖
// 到这里（pragma 对编译单元余下部分生效），但万一 BOM 被剥掉、或某些工具链
// 只看当前文件，这里的 "·"（U+00B7）就会被编成单字节 -> 混合编码。
#if defined(_MSC_VER)
#  pragma execution_character_set("utf-8")
#endif

#define AINO_IMPLEMENTATION
#include "../aino.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using aino::Color;
using aino::MenuPick;
using aino::Rect;
using aino::Stack;
using aino::Style;
using aino::Ui;
using aino::Vec2;

namespace {

// =============================================================================
//  应用状态
// =============================================================================
struct App {
    // AINO_DARK=1 直接以深色启动 —— 截图脚本没法在首帧前点按钮
    bool dark = std::getenv("AINO_DARK") != nullptr;
    int  page = 0;

    // 反馈计数：按钮类示例共用，点完在卡片标题条里显示
    int         hits = 0;
    std::string last = "—";

    // 文本
    std::string email, keyword, note = "第一行\n第二行：Enter 换行，长文自动折行";
    std::string amount = "128", otp = "2048", uname = "张三", phone = "", cpwd = "";

    // 布尔
    bool remember = true, autosave = false;
    bool wifi = true, bt = false, notify = true;
    bool agree = true, bold = true, italic = false;
    bool acc1 = true, acc2 = false;
    bool pop = false, dlg = false, confirm = false, drawer = false;
    bool motion = false;

    // 数值 / 索引
    float vol = 0.62f, step = 12.0f, split = 0.42f;
    int   radio = 1, plan = 1, tab = 0, seg = 1, combo = 1;
    int   stars = 4, page_no = 2, tg = 1, bg_click = -1, menu_pick = -1, ctx_pick = -1;
    std::uint32_t tgm = 0x3;
    int   cy = 2026, cm = 10, cd = 22;

    std::string picked = "未选择";

    // toast 余量（秒）。> 0 时这一层自己 repaint —— 全窗口唯一的常驻动画
    float       toast_left = 0.0f;
    std::string toast_msg;

    int scroll_cmd = 0;   // 0 空闲 / 1 回顶 / 2 到底
    int jump = 0;         // 虚拟列表跳到底部

    // 导航里被折叠的分组（第 i 位 = 第 i 组）。默认全展开：45 项按组铺开，
    // 想缩短列表就点组头。位掩码而不是 vector<bool>，因为要能一眼看出"全开"。
    std::uint32_t nav_closed = 0;
};

// =============================================================================
//  示例数据
// =============================================================================
const std::vector<std::string> kPlans = {"免费版", "专业版", "企业版"};
const std::vector<std::string> kViews = {"概览", "分析", "设置"};
const std::vector<std::string> kLangs = {"C++", "Rust", "Go", "Zig", "Python", "TypeScript", "Swift"};
const std::vector<std::string> kOps = {"重命名", "复制", "导出为 PNG", "删除"};
const std::vector<std::string> kCrumbs = {"控制台", "组件", "导航"};
const std::vector<std::string> kBases = {"首页", "文档", "组件"};

const std::vector<std::string> kHeads = {"模块", "状态", "耗时"};
const std::vector<std::vector<std::string>> kRows = {
    {"解析配置", "完成", "12 ms"},
    {"加载主题", "完成", "3 ms"},
    {"构建布局", "完成", "8 ms"},
    {"光栅化", "进行中", "41 ms"},
};

const std::vector<float> kWave = [] {
    std::vector<float> v(24);
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = std::sin(float(i) * 0.55f) * 0.6f + 0.35f * std::sin(float(i) * 1.7f) + 0.5f;
    return v;
}();

const std::vector<float> kBars = [] {
    std::vector<float> v(12);
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = (i == 7) ? -0.45f : 0.25f + 0.6f * std::fabs(std::sin(float(i) * 0.9f + 1.3f));
    return v;
}();

// =============================================================================
//  示例表
// =============================================================================
struct Demo {
    const char* name;        // 卡片标题条左侧
    const char* note;        // 标题条右侧：这条示例的技术要点
    const char* code;        // 展示用源码（C++，不超过 ~18 行）
    float       preview_h;   // 预览区需要的高度
    void (*draw)(Ui&, App&, const Rect&);
};

struct Page {
    const char* group;
    const char* name;
    const char* desc;
    const char* shadcn;      // 对应 shadcn/ui 的组件名；"—" 表示没有对应项
    const Demo* demos;
    int         n;
    // 本页的模态浮层。必须在这里画、而不是在卡片里画：dialog / drawer 是立即
    // 绘制的，放进滚动容器会被视口裁掉，还会被后面的卡片盖住。
    void (*overlay)(Ui&, App&) = nullptr;
};

// ---------------------------------------------------------------- 布局助手
// hbox 的 next() 给的是整行宽，这里按需要裁到指定宽度，省掉手算 x。
Rect cell(Stack& row, float w, float h) {
    Rect r = row.next(h);
    r.w = std::min(w, r.w);
    return r;
}

// =============================================================================
//  概览页 —— 不是组件，是索引，所以不走 Demo 表
// =============================================================================
// 定义在 kPages 之后（要用它统计），这里先声明，免得整个概览页跟着表往下挪。
const std::vector<std::pair<const char*, int>>& group_stats();

// -----------------------------------------------------------------------------
//  把概览页那段代码**原样**跑起来 —— 开一个真正的第二个窗口
// -----------------------------------------------------------------------------
// aino 没有全局状态：一个 Window 就是一条消息循环 + 一块后备缓冲。所以"演示
// 这段代码"最直白的做法就是再起一个线程、再建一个 Window —— 而不是在画廊里
// 摆一个假框子照着画一遍（那种演示一旦代码改了就跟不上）。
//
// 为什么 detach：主窗口关掉时进程就该退出，而在另一个线程里阻塞于 run() 的
// 窗口没有"从外面叫停"的干净办法。detach 之后进程一退线程跟着没，代价只是
// 退出时那几个 GDI 句柄不回收 —— 进程都结束了，这不叫泄漏。
//
// 反过来，用户自己关掉演示窗口时线程会正常走完 run() 并析构 Window，句柄回收。
void open_quickstart_window() {
    std::thread([] {
        // 这里刻意用 WindowConfig 而不是三参数构造：演示一下新加的配置项
        // （最小尺寸、居中）。标题里的版本号顺便当个"这窗口是谁开的"标记。
        aino::WindowConfig cfg;
        cfg.width      = 460;
        cfg.height     = 320;
        cfg.title      = "aino 最小示例 · " AINO_VERSION;
        cfg.min_width  = 380;
        cfg.min_height = 260;
        cfg.center     = true;

        aino::Window win(cfg);
        if (!win.ok()) return;

        std::string email;
        bool notify = true;
        int  saved  = 0;

        win.run([&](Ui& ui) {
            ui.clear(ui.theme().background);
            ui.panel(Rect(0.0f, 0.0f, ui.width(), ui.height()));
            auto c = ui.vbox(Rect(0.0f, 0.0f, ui.width(), ui.height()), 10.0f, 20.0f);
            ui.heading("设置", c.next(34.0f));
            ui.separator(c.next(1.0f));
            ui.input(&email, c.next(24.0f));
            ui.checkbox(&notify, "邮件通知", c.next(22.0f));
            if (ui.button("保存", c.next(24.0f), Style().primary())) ++saved;
            ui.muted_text(email.empty() ? std::string("输入邮箱后点保存")
                                        : "已保存 " + std::to_string(saved) + " 次：" + email,
                          c.next(20.0f), Style().left().font(11.5f));
        });
    }).detach();
}

void draw_overview(Ui& ui, App& a, const Rect& area) {
    // 栅格是两级的：左边一半放介绍 + 完整可编译的 Quick Start，右边一半再切成
    // 分类索引与实测表。早期是三等分，左栏只有 240px 宽，代码示例 32 个字符就得
    // 断行，卡片下半截还会空出一大块 —— 两栏才能同时装下文字和代码。
    auto cols = ui.hbox(area, 18.0f);

    {
        const Rect cp = cols.part(0, 2);
        ui.panel(cp);
        Stack c = ui.vbox(cp, 9.0f, 20.0f);
        ui.subheading("a 单头文件 GUI 库", c.next(26.0f), Style().left());
        ui.separator(c.next(1.0f));
        ui.label("立即模式：每帧重建界面，控件在按下那一帧直接改变量。", c.next(20.0f),
                 Style().left().font(12.5f));
        ui.label("空闲 0 帧：没有动画时不 repaint，CPU 占用 0.00%。", c.next(20.0f),
                 Style().left().font(12.5f));
        ui.label("单文件：include 一个 .hpp，静态链接后无第三方依赖。", c.next(20.0f),
                 Style().left().font(12.5f));
        c.skip(6.0f);
        const char* code =
            "#define AINO_IMPLEMENTATION\n"
            "#include \"aino.hpp\"\n"
            "#include <string>\n"
            "\n"
            "int main() {\n"
            "    aino::Window win(880, 560, \"aino\");\n"
            "    if (!win.ok()) return 1;\n"
            "\n"
            "    std::string email;\n"
            "    bool notify = true;\n"
            "\n"
            "    win.run([&](aino::Ui& ui) {\n"
            "        ui.panel({24, 24, 400, 240});\n"
            "        auto c = ui.vbox({24, 24, 400, 240}, 10, 18);\n"
            "        ui.heading(\"设置\", c.next(34));\n"
            "        ui.input(&email, c.next(36));\n"
            "        ui.checkbox(&notify, \"邮件通知\", c.next(22));\n"
            "        ui.button(\"保存\", c.next(36), Style().primary());\n"
            "    });\n"
            "}";
        ui.code_block(code, c.next(ui.code_block_height(code, Style().font(11.5f))),
                      Style().font(11.5f));
        // 代码不是用来读的，是用来跑的：点一下开一个真窗口。
        c.skip(4.0f);
        {
            Stack row = c.hbox(34.0f);
            if (ui.button("在新窗口运行这段代码", row.part(0, 2), Style().outline()))
                open_quickstart_window();
            ui.muted_text("独立窗口 · 独立消息循环", row.part(1, 2),
                          Style().right().font(11.5f));
        }
    }

    {
        const Rect right = cols.part(1, 2);
        auto sub = ui.hbox(right, 18.0f);

        {
            const Rect cp = sub.part(0, 2);
            ui.panel(cp);
            Stack c = ui.vbox(cp, 8.0f, 20.0f);
            ui.subheading("分类", c.next(26.0f), Style().left());
            // 从 kPages 现算，不手写数字：加页之后统计自动跟上
            const auto& gs = group_stats();
            for (size_t gi = 1; gi < gs.size(); ++gi) {
                Stack r = c.hbox(26.0f);
                ui.label(gs[gi].first, r.part(0, 2), Style().left().font(12.5f));
                ui.muted_text(std::to_string(gs[gi].second) + " 页", r.part(1, 2),
                              Style().right().font(12.0f));
            }
            c.skip(4.0f);
            // 这两行要短：右半边的卡片只有 ~176px 可用，长一句就被裁在边框上。
            ui.muted_text("左侧导航按分类分组。", c.next(18.0f),
                          Style().left().font(11.5f));
            ui.muted_text("示例可操作，源码在右。", c.next(18.0f),
                          Style().left().font(11.5f));
            // 品牌块贴在卡片底部。不用 c.fill() 把它顶下去 —— fill() 会吃掉
            // 全部剩余空间，后面的 next(78) 就只剩 0 高，块直接消失。
            const Rect tb(cp.x + 20.0f, cp.bottom() - 20.0f - 78.0f, cp.w - 40.0f, 78.0f);
            ui.fill_rect(tb, ui.theme().primary, ui.theme().radius);
            ui.label("aino", tb, Style().center().semi_bold().font(20.0f)
                     .color(ui.theme().primary_foreground));
        }

        {
            const Rect cp = sub.part(1, 2);
            ui.panel(cp);
            Stack c = ui.vbox(cp, 8.0f, 20.0f);
            ui.subheading("实测", c.next(26.0f), Style().left());
            const std::vector<std::string> h = {"指标", "值"};
            const std::vector<std::vector<std::string>> rows = {
                {"第三方依赖", "0"},
                {"空闲 CPU", "0.00%"},
                {"指针移动", "变化才画"},
                {"动画", "60 fps"},
                {"组件", "47 个"},
                {"回归断言", "45 项"},
                {"文字", "系统字体"},
                {"几何", "自绘 AA"},
                {"DLL", "仅系统库"},
            };
            ui.table(h, rows, c.fill(), Style().font(12.0f));
        }
    }
    (void)a;
}

// =============================================================================
//  基础
// =============================================================================
void d_btn_variants(Ui& ui, App& a, const Rect& box) {
    auto c = ui.vbox(box, 8.0f);
    const Style::Variant vs[] = {Style::Default, Style::Secondary, Style::Outline,
                                 Style::Ghost, Style::Destructive, Style::Link};
    const char* names[] = {"Default", "Secondary", "Outline", "Ghost", "Destructive", "Link"};
    for (int i = 0; i < 6; ++i) {
        Style st;
        st.variant = vs[i];
        if (ui.button(names[i], cell(c, 132.0f, 24.0f), st)) { ++a.hits; a.last = names[i]; }
    }
}

void d_btn_states(Ui& ui, App& a, const Rect& box) {
    Style dis;
    dis.disabled = true;
    ui.button("禁用", Rect(box.x, box.y, 96.0f, 24.0f), dis);

    Style pressy;
    pressy.variant = Style::Secondary;
    pressy.press_scale = 0.94f;
    if (ui.button("按压缩放", Rect(box.x + 106.0f, box.y, 108.0f, 24.0f), pressy)) {
        ++a.hits;
        a.last = "press_scale";
    }
}

void d_btn_icon(Ui& ui, App& a, const Rect& box) {
    auto r = ui.hbox(box, 8.0f);
    const char* g[] = {"+", "−", "×", "⋯"};
    for (int i = 0; i < 4; ++i) {
        if (ui.icon_button(g[i], r.next(24.0f))) {
            ++a.hits; a.last = std::string("icon ") + g[i];
        }
    }
}

void d_badge(Ui& ui, App& a, const Rect& box) {
    auto r = ui.hbox(box, 8.0f);
    ui.badge("Default", r.next(74.0f));
    Style sec; sec.variant = Style::Secondary;
    ui.badge("Secondary", r.next(84.0f), sec);
    Style out; out.variant = Style::Outline;
    ui.badge("Outline", r.next(70.0f), out);
    Style red; red.variant = Style::Destructive;
    ui.badge("Failed", r.next(64.0f), red);
    (void)a;
}

void d_kbd_sep(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 10.0f);
    {
        Stack r = c.hbox(22.0f, 4.0f);
        ui.kbd("Ctrl", r.next_text("Ctrl", 16.0f, 11.0f, 500));
        ui.muted_text("+", r.next(10.0f), Style().center());
        ui.kbd("Shift", r.next_text("Shift", 16.0f, 11.0f, 500));
        ui.muted_text("+", r.next(10.0f), Style().center());
        ui.kbd("K", r.next_text("K", 18.0f, 11.0f, 500));
    }
    ui.separator(c.next(1.0f));
    ui.separator(c.next(1.0f));
    ui.muted_text("separator 的水平与垂直两种朝向由 box 的宽高比决定。", c.next(18.0f),
                  Style().left().font(11.5f));
    (void)a;
}

void d_avatar(Ui& ui, App& a, const Rect& box) {
    auto r = ui.hbox(box, 12.0f);
    ui.avatar("张", r.next(24.0f));
    ui.avatar("李四", r.next(24.0f));
    ui.avatar("王", r.next(48.0f));
    ui.avatar("A", r.next(64.0f));
    (void)a;
}

void d_progress(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 8.0f);
    ui.progress(0.24f, c.next(8.0f));
    ui.progress(0.58f, c.next(8.0f));
    ui.progress(0.92f, c.next(8.0f));
    c.skip(4.0f);
    Stack r = c.hbox(24.0f, 10.0f);
    ui.skeleton(r.next(120.0f));
    ui.spinner(r.next(24.0f));
    (void)a;
}

// =============================================================================
//  表单
// =============================================================================
void d_input(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 10.0f);
    ui.input(&a.email, c.next(24.0f), Style().hint("you@example.com"));
    ui.input(&a.keyword, c.next(24.0f), Style().hint("搜索…"));
    ui.muted_text("焦点、选中、剪贴板、IME 组合串都由库处理。", c.next(18.0f),
                  Style().left().font(11.5f));
}

void d_textarea(Ui& ui, App& a, const Rect& box) {
    ui.textarea(&a.note, box, Style().font(13.0f));
}

void d_input_group(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 10.0f);
    ui.input_group(&a.amount, "¥", "元", c.next(24.0f));
    ui.input_group(&a.phone, "+86", "", c.next(24.0f));
    ui.muted_text("前后缀区域同属输入框热区，点击任意位置都能落光标。", c.next(18.0f),
                  Style().left().font(11.5f));
}

void d_otp(Ui& ui, App& a, const Rect& box) {
    ui.input_otp(&a.otp, 6, box);
}

void d_checkbox(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 10.0f);
    ui.checkbox(&a.remember, "记住这台设备", c.next(22.0f));
    ui.checkbox(&a.autosave, "自动保存", c.next(22.0f));
    Style dis;
    dis.disabled = true;
    bool tmp = true;
    ui.checkbox(&tmp, "不可用", c.next(22.0f), dis);
}

void d_switch(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 10.0f);
    {
        const Rect rr = c.next(26.0f);
        ui.label("Wi-Fi", rr, Style().left().font(12.5f));
        ui.toggle(&a.wifi, Rect(rr.right() - 44.0f, rr.y, 44.0f, 26.0f));
    }
    {
        const Rect rr = c.next(26.0f);
        ui.label("蓝牙", rr, Style().left().font(12.5f));
        ui.toggle(&a.bt, Rect(rr.right() - 44.0f, rr.y, 44.0f, 26.0f));
    }
    ui.muted_text("toggle() 是 Switch。按下去态按钮是 toggle_button()。", c.next(18.0f),
                  Style().left().font(11.5f));
}

void d_radio(Ui& ui, App& a, const Rect& box) {
    ui.radio_group(kPlans, &a.radio, box, Style().font(13.0f));
}

void d_slider(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 12.0f);
    ui.slider(&a.vol, 0.0f, 1.0f, c.next(26.0f));
    char buf[64];
    std::snprintf(buf, sizeof buf, "vol = %.2f", double(a.vol));
    ui.muted_text(buf, c.next(18.0f), Style().left().font(11.5f));
}

// 截图工具钩子：AINO_OPEN=select|combobox 让指定下拉默认展开。
// 只影响示例（下拉的展开态本来是"点出来的"，脚本点不动它），不进库。
void demo_force_open(Ui& ui, const char* kind, const Rect& box) {
    const char* want = std::getenv("AINO_OPEN");
    if (!want || std::string(want) != kind || !ui.impl) return;
    // 一帧里量测趟和绘制趟的 rect 会差几像素（id 就是按坐标算的），
    // 所以把出现过的 id 全记下来逐个点亮。
    static std::vector<uint64_t> ids;
    const uint64_t id = aino::detail::make_id(kind, box);
    if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    for (uint64_t i : ids) ui.impl->flags[i] = 1.0f;
}

void d_select(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 10.0f);
    const Rect sr = c.next(24.0f);
    demo_force_open(ui, "select", sr);        // AINO_OPEN=select 时强制展开（截图用）
    ui.select(kPlans, &a.plan, sr, Style().hint("选择套餐"));
    ui.muted_text("列表在帧末绘制，所以永远压住后面的内容。", c.next(18.0f),
                  Style().left().font(11.5f));
}

void d_combobox(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 10.0f);
    const Rect cr = c.next(24.0f);
    demo_force_open(ui, "combobox", cr);      // AINO_OPEN=combobox 时强制展开
    ui.combobox(kLangs, &a.combo, cr);
    ui.muted_text("展开后直接打字过滤，↑↓ 移动，回车确认，Esc 关闭。", c.next(18.0f),
                  Style().left().font(11.5f));
}

void d_toggle(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 12.0f);
    {
        Stack r = c.hbox(34.0f, 8.0f);
        ui.toggle_button("粗体", &a.bold, r.next(84.0f));
        ui.toggle_button("斜体", &a.italic, r.next(84.0f));
    }
    ui.toggle_group({"日", "周", "月"}, &a.tg, c.next(24.0f));
    ui.muted_text("toggle_group 单选：再点当前项会取消，使 index 变为 -1。", c.next(18.0f),
                  Style().left().font(11.5f));
}

void d_button_group(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 12.0f);
    ui.button_group({"复制", "粘贴", "剪切"}, &a.bg_click, c.next(24.0f));
    ui.toggle_group_multi({"B", "I", "U"}, &a.tgm, c.next(24.0f));
    ui.muted_text("前者互斥、相邻共享边框；后者用位掩码表示任意组合。", c.next(18.0f),
                  Style().left().font(11.5f));
}

void d_stepper_rating(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 14.0f);
    // 官方步进器是"数值框 + 右边缘竖条"，整个控件只有 ~60-140 宽。
    // 拉满整列会变成一条巨大的空输入框，看着像没画完。
    const Rect srow = c.next(24.0f);
    ui.stepper(&a.step, 1.0f, 0.0f, 99.0f, Rect(srow.x, srow.y, 96.0f, srow.h));
    ui.rating(&a.stars, 5, c.next(24.0f));
    char buf[64];
    std::snprintf(buf, sizeof buf, "step = %.0f · rating = %d", double(a.step), a.stars);
    ui.muted_text(buf, c.next(18.0f), Style().left().font(11.5f));
}

void d_field(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 14.0f);
    {
        const float h = ui.field_height("邮箱", "用于接收通知", "", 24.0f);
        ui.field("邮箱", "用于接收通知", "", c.next(h), [&](const Rect& r) {
            ui.input(&a.email, r, Style().hint("you@example.com"));
        });
    }
    {
        const float h = ui.field_height("密码", "", "至少 8 个字符", 24.0f);
        ui.field("密码", "", "至少 8 个字符", c.next(h), [&](const Rect& r) {
            ui.input(&a.cpwd, r);
        });
    }
}

void d_calendar(Ui& ui, App& a, const Rect& box) {
    ui.calendar(&a.cy, &a.cm, &a.cd, box);
}

// =============================================================================
//  数据展示
// =============================================================================
void d_tabs(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 14.0f);
    ui.tabs(kViews, &a.tab, c.next(24.0f));
    ui.segmented(kPlans, &a.seg, c.next(32.0f));
    ui.muted_text("下划线式与胶囊式：两套视觉，同一套索引语义。", c.next(18.0f),
                  Style().left().font(11.5f));
}

void d_table(Ui& ui, App& a, const Rect& box) {
    ui.table(kHeads, kRows, box, Style().font(12.5f));
    (void)a;
}

void d_card(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 10.0f);
    {
        const Rect p = c.next(64.0f);
        ui.panel(p);
        Stack in = ui.vbox(p, 2.0f, 14.0f);
        ui.label("panel", in.next(18.0f), Style().left().semi_bold().font(12.5f));
        ui.muted_text("卡片底色、边框、圆角来自主题", in.next(16.0f), Style().left().font(11.5f));
    }
    // 两行条目：macOS 列表里 44 高（不是 Web 的 50），行间一条 1px 分隔线
    ui.item("张伟", "zhangwei@example.com", "管理员", c.next(44.0f), Style().row_lines());
    ui.item("李娜", "lina@example.com", "只读", c.next(44.0f), Style().row_lines());
    (void)a;
}

void d_empty(Ui& ui, App& a, const Rect& box) {
    ui.empty_state("还没有通知", "有新消息时会出现在这里", box);
    (void)a;
}

void d_pagination(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 10.0f);
    ui.pagination(&a.page_no, 20, c.next(24.0f));
    char buf[64];
    std::snprintf(buf, sizeof buf, "第 %d / 20 页", a.page_no + 1);
    ui.muted_text(buf, c.next(18.0f), Style().left().font(11.5f));
}

void d_accordion(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 4.0f);
    ui.accordion("渲染管线", &a.acc1, c.next(24.0f));
    if (a.acc1) {
        ui.muted_text("几何自绘并做抗锯齿，文字交给系统字体引擎。", c.next(20.0f),
                      Style().left().font(11.5f));
    }
    ui.accordion("输入分发", &a.acc2, c.next(24.0f));
    if (a.acc2) {
        ui.muted_text("浮层通过输入闸门按位置与层级放行，不靠控件树。", c.next(20.0f),
                      Style().left().font(11.5f));
    }
}

void d_alert(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 10.0f);
    const float w = c.area.w;
    {
        const char* t = "布局器已生效";
        const char* d = "所有坐标由 Stack 推导，没有手算值。";
        ui.alert(t, d, c.next(ui.alert_height(t, d, w)));
    }
    {
        Style bad;
        bad.variant = Style::Destructive;
        const char* t = "删除后无法恢复";
        const char* d = "正文按宽度自动折行，高度由 alert_height 量出。";
        ui.alert(t, d, c.next(ui.alert_height(t, d, w, bad)), bad);
    }
    (void)a;
}

void d_breadcrumb(Ui& ui, App& a, const Rect& box) {
    ui.breadcrumb(kCrumbs, box);
    (void)a;
}

void d_chart(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 10.0f);
    ui.sparkline(kWave, c.next(72.0f));
    ui.bars(kBars, c.next(72.0f));
    (void)a;
}

// =============================================================================
//  浮层
// =============================================================================
void d_menu(Ui& ui, App& a, const Rect& box) {
    const Rect mr(box.x, box.y, 132.0f, 24.0f);
    demo_force_open(ui, "menu", mr);          // AINO_OPEN=menu 时强制展开（截图用）
    if (ui.menu("操作", kOps, &a.menu_pick, mr, Style().outline())) {
        a.last = kOps[size_t(a.menu_pick)];
        a.toast_msg = "已选择：" + a.last;
        a.toast_left = 2.4f;
    }
    ui.muted_text("选中项写入 index 并返回 true，关闭由控件自己完成。",
                  Rect(box.x, box.y + 46.0f, box.w, 18.0f), Style().left().font(11.5f));
}

// 截图工具钩子：AINO_OPEN=dialog|popover|drawer|toast 让浮层默认打开。
void demo_set_open(const char* kind, bool* v) {
    const char* want = std::getenv("AINO_OPEN");
    if (want && std::string(want) == kind) *v = true;
}

void d_menubar(Ui& ui, App& a, const Rect& box) {
    // 菜单栏必须占满一条：点开一项后横向移动要能切到隔壁
    const Rect bar(box.x, box.y, box.w, 32.0f);
    static const std::vector<std::string> labels = {"文件", "编辑", "视图", "帮助"};
    static const std::vector<std::vector<std::string>> items = {
        {"新建", "打开…", "保存"},
        {"撤销", "重做", "粘贴"},
        {"放大", "缩小", "全屏"},
        {"快捷键", "关于"},
    };
    const MenuPick mp = ui.menubar(labels, items, bar);
    if (mp.valid())
        a.picked = labels[size_t(mp.menu)] + " → " + items[size_t(mp.menu)][size_t(mp.item)];
    ui.muted_text("最近选择：" + a.picked, Rect(box.x, bar.bottom() + 12.0f, box.w, 18.0f),
                  Style().left().font(11.5f));
}

void d_popover(Ui& ui, App& a, const Rect& box) {
    demo_set_open("popover", &a.pop);
    ui.popover("排序", &a.pop, Rect(box.x, box.y, 120.0f, 24.0f), Vec2(190.0f, 104.0f),
               [&](const Rect& c) {
                   ui.item("按名称", "", "", Rect(c.x, c.y, c.w, 24.0f));
                   ui.item("按时间", "", "", Rect(c.x, c.y + 26.0f, c.w, 24.0f));
                   ui.item("按大小", "", "", Rect(c.x, c.y + 52.0f, c.w, 24.0f));
               });
    ui.muted_text("锚点由调用方给，内容由回调画，点外部或 Esc 关闭。",
                  Rect(box.x, box.y + 46.0f, box.w, 18.0f), Style().left().font(11.5f));
}

void d_hover_card(Ui& ui, App& a, const Rect& box) {
    const Rect anchor(box.x, box.y, box.w, 52.0f);
    ui.item("张伟", "zhangwei@example.com", "管理员", anchor);
    ui.hover_card(anchor, Vec2(220.0f, 84.0f), [&](const Rect& c) {
        ui.label("张伟", Rect(c.x, c.y, c.w, 20.0f), Style().left().semi_bold());
        ui.muted_text("加入于 2024 年 3 月", Rect(c.x, c.y + 22.0f, c.w, 17.0f),
                      Style().left().font(11.5f));
        ui.muted_text("只响应悬停，不参与点击命中", Rect(c.x, c.y + 42.0f, c.w, 17.0f),
                      Style().left().font(11.5f));
    });
    (void)a;
}

void d_dialog(Ui& ui, App& a, const Rect& box) {
    demo_set_open("dialog", &a.dlg);
    if (ui.button("打开对话框", Rect(box.x, box.y, 132.0f, 24.0f), Style().primary()))
        a.dlg = true;
    ui.muted_text("面板由 o_dialog() 在页面末尾绘制，遮罩覆盖整窗。",
                  Rect(box.x, box.y + 46.0f, box.w, 18.0f), Style().left().font(11.5f));
}

void o_dialog(Ui& ui, App& a) {
    const Rect d = Rect::centered(ui.width() * 0.5f, ui.height() * 0.5f, 420.0f, 200.0f);
    const std::string body =
        "正文按宽度折行并被裁进面板。按钮由调用方紧接着画，因此天然位于面板之上。";
    if (ui.dialog("确认删除", body, &a.dlg, d)) {
        // macOS 告警的按钮是**右下角右对齐、按内容定宽**的两个小按钮；
        // 把面板底边二分之一二分之一地摊开是 Web 模态的做法，一眼就不是 mac。
        const float bw = 86.0f, gap = 10.0f, by = d.bottom() - 58.0f;
        if (ui.button("删除", Rect(d.right() - 22.0f - bw, by, bw, 24.0f), Style().destructive())) a.dlg = false;
        if (ui.button("取消", Rect(d.right() - 22.0f - bw * 2.0f - gap, by, bw, 24.0f), Style().outline())) a.dlg = false;
    }
    ui.alert_dialog("删除这个文件？", "删除后无法恢复，这份数据没有其他备份。",
                    "删除", "取消", &a.confirm,
                    Rect(ui.width() * 0.5f - 190.0f, ui.height() * 0.5f - 82.0f, 380.0f, 164.0f),
                    Style().destructive());
}

void d_alert_dialog(Ui& ui, App& a, const Rect& box) {
    if (ui.button("删除…", Rect(box.x, box.y, 120.0f, 24.0f), Style().destructive()))
        a.confirm = true;
    ui.muted_text("确认弹层与上一个示例共用同一套面板绘制。",
                  Rect(box.x, box.y + 46.0f, box.w, 18.0f), Style().left().font(11.5f));
}

void d_drawer(Ui& ui, App& a, const Rect& box) {
    demo_set_open("drawer", &a.drawer);
    if (ui.button("打开抽屉", Rect(box.x, box.y, 120.0f, 24.0f), Style().outline()))
        a.drawer = true;
    ui.muted_text("抽屉贴窗口边缘，所以同样在页面末尾绘制。",
                  Rect(box.x, box.y + 46.0f, box.w, 18.0f), Style().left().font(11.5f));
}

void o_drawer(Ui& ui, App& a) {
    ui.drawer("详情", &a.drawer, 0,
              Rect(ui.width() - 300.0f, 0.0f, 300.0f, ui.height()), [&](const Rect& c) {
        ui.label("详情面板", Rect(c.x, c.y, c.w, 22.0f), Style().left().semi_bold());
        ui.muted_text("遮罩淡入，面板不做位移动画", Rect(c.x, c.y + 24.0f, c.w, 18.0f),
                      Style().left().font(11.5f));
        ui.muted_text("点遮罩、Esc 或右上角关闭", Rect(c.x, c.y + 48.0f, c.w, 18.0f),
                      Style().left().font(11.5f));
    });
}

void d_toast(Ui& ui, App& a, const Rect& box) {
    if (ui.button("弹出提示", Rect(box.x, box.y, 120.0f, 24.0f), Style().secondary())) {
        a.toast_msg = "操作已完成";
        a.toast_left = 2.4f;
    }
    ui.muted_text("toast 在右下角，由 composeOverlayLayer 统一绘制。",
                  Rect(box.x, box.y + 46.0f, box.w, 18.0f), Style().left().font(11.5f));
}

void d_tooltip(Ui& ui, App& a, const Rect& box) {
    const Rect t(box.x, box.y, 150.0f, 24.0f);
    ui.button("悬停查看", t, Style().outline());
    ui.tooltip(t, "tooltip 在悬停约 0.4 秒后出现，移开即消失");
    const Rect b2(t.right() + 10.0f, t.y, 62.0f, 24.0f);
    ui.badge("New", b2);
    ui.tooltip(b2, "任意矩形都能作为锚点");
    (void)a;
}

void d_context_menu(Ui& ui, App& a, const Rect& box) {
    ui.panel(box);
    ui.muted_text("在此区域右键", box, Style().center().font(11.5f));
    ui.context_menu(box, {"标记已读", "归档", "删除"}, &a.ctx_pick);
    if (a.ctx_pick >= 0) {
        a.last = "右键：" + std::to_string(a.ctx_pick);
        a.ctx_pick = -1;
    }
    (void)a;
}

// =============================================================================
//  布局
// =============================================================================
void d_layout(Ui& ui, App& a, const Rect& box) {
    auto c = ui.vbox(box, 10.0f);
    {
        Stack r = c.hbox(36.0f, 8.0f);
        if (ui.button("等分 1/3", r.part(0, 3), Style().outline())) ++a.hits;
        if (ui.button("2/3", r.part(1, 3), Style().outline())) ++a.hits;
        if (ui.button("3/3", r.part(2, 3), Style().outline())) ++a.hits;
    }
    {
        Stack r = c.hbox(36.0f, 10.0f);
        const Rect b1 = r.next_text("好", 34.0f);
        const Rect b2 = r.next_text("取消", 34.0f);
        const Rect b3 = r.next_text("保存并关闭", 34.0f, 13.0f, 400);
        if (ui.button("好", b1, Style().outline())) ++a.hits;
        if (ui.button("取消", b2, Style().outline())) ++a.hits;
        if (ui.button("保存并关闭", b3, Style().primary())) ++a.hits;
    }
    const Rect rest = c.fill();
    Stack outer = ui.vbox(rest, 6.0f);
    Stack inner = outer.hbox(56.0f, 8.0f);
    for (int i = 0; i < 3; ++i) {
        const Rect p = inner.part(i, 3);
        ui.panel(p, Style().background(ui.theme().muted).corner(6.0f));
        ui.muted_text(i == 1 ? "嵌套" : "", p, Style().center().font(11.0f));
    }
}

void d_scroll(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 8.0f);
    {
        Stack r = c.hbox(24.0f, 8.0f);
        if (ui.button("回顶", r.part(0, 2), Style().outline())) a.scroll_cmd = 1;
        if (ui.button("到底", r.part(1, 2), Style().outline())) a.scroll_cmd = 2;
    }
    const Rect vp = c.fill();
    const float ih = 24.0f;
    const int   n = 20;
    auto sc = ui.scroll(vp, ih * float(n));
    if (a.scroll_cmd == 1) { sc.to(0.0f); a.scroll_cmd = 0; }
    if (a.scroll_cmd == 2) { sc.to(1e6f); a.scroll_cmd = 0; }
    for (int i = 0; i < n; ++i) {
        const Rect r = ui.row(ih);
        ui.panel(r.inset(0.0f, 1.0f), Style().background(ui.theme().muted));
        char buf[64];
        std::snprintf(buf, sizeof buf, "列表项 %02d", i + 1);
        ui.label(buf, Rect(r.x + 10.0f, r.y + 1.0f, r.w - 20.0f, r.h - 2.0f), Style().left());
    }
}

void d_virtual(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 8.0f);
    if (ui.button("跳到底部", c.next(24.0f), Style().outline())) a.jump = 1;
    const Rect vp = c.fill();
    const float ih = 26.0f;
    const int   n = 100000;
    if (a.jump == 1) { ui.scroll_to_index(vp, n - 1, ih, n); a.jump = 0; }
    const int drawn = ui.scroll(vp, n, ih, [&](int i, const Rect& r) {
        if (i & 1) ui.fill_rect(r, ui.theme().muted.with_alpha(80), 0.0f);
        char buf[48];
        std::snprintf(buf, sizeof buf, "第 %d 行", i + 1);
        ui.label(buf, Rect(r.x + 8.0f, r.y, r.w - 16.0f, r.h), Style().left().font(12.0f));
    });
    ui.muted_text("共 " + std::to_string(n) + " 行，本帧绘制 " + std::to_string(drawn) + " 行",
                  c.next(16.0f), Style().left().font(11.0f));
}

void d_split(Ui& ui, App& a, const Rect& box) {
    Rect l, r;
    ui.split_pane(&a.split, box, &l, &r, true, 0.2f);
    ui.panel(l, Style().background(ui.theme().muted).corner(8.0f));
    ui.muted_text("左侧", l, Style().center().font(11.5f));
    ui.panel(r, Style().background(ui.theme().muted).corner(8.0f));
    ui.muted_text("右侧", r, Style().center().font(11.5f));
}

void d_clip(Ui& ui, App& a, const Rect& box) {
    ui.panel(box, Style().background(ui.theme().muted));
    ui.push_clip(box);
    ui.label("这一行文字明显长于容器宽度，超出部分被裁掉而不是爬出面板。",
             Rect(box.x + 10.0f, box.y + 14.0f, box.w, 22.0f), Style().left());
    ui.label("第二行同理。几何与文字走同一套裁剪区域。",
             Rect(box.x + 10.0f, box.y + 40.0f, box.w, 22.0f), Style().left());
    ui.pop_clip();
    (void)a;
}

void d_aspect(Ui& ui, App& a, const Rect& box) {
    const Rect r = Ui::aspect_rect(Rect(box.x, box.y, std::min(box.h * 16.0f / 9.0f, box.w), 0.0f),
                                   16.0f / 9.0f);
    ui.panel(r, Style().background(ui.theme().muted).corner(8.0f));
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.0f × %.0f", double(r.w), double(r.h));
    ui.muted_text(buf, r, Style().center().font(11.5f));
    (void)a;
}

// =============================================================================
//  图形与动效
// =============================================================================
void d_primitives(Ui& ui, App& a, const Rect& box) {
    const Color fg = ui.theme().foreground;
    const float cellw = box.w / 4.0f;
    const float bw = std::min(cellw - 16.0f, 64.0f);
    auto cx = [&](int i) { return box.x + cellw * (float(i) + 0.5f); };
    const float top = box.y + 4.0f;
    ui.fill_rect(Rect(cx(0) - bw * 0.5f, top, bw, 44.0f), fg, 10.0f);
    ui.stroke_rect(Rect(cx(1) - bw * 0.5f, top, bw, 44.0f), fg, 10.0f, 1.5f);
    ui.fill_circle(cx(2), top + 22.0f, 22.0f, fg);
    ui.fill_crescent(cx(3), top + 22.0f, 22.0f, cx(3) + 8.5f, top + 17.0f, 21.8f, fg);
    for (int k = 0; k < 5; ++k) {
        const float step = box.w / 5.0f;
        const float x0 = box.x + step * float(k) + 6.0f;
        ui.draw_line(x0, top + 62.0f, x0 + step * 0.55f, top + 88.0f, fg, 1.0f + float(k) * 0.7f);
    }
    (void)a;
}

void d_transform(Ui& ui, App& a, const Rect& box) {
    const float bw = 68.0f, bh = 68.0f;
    const float step = box.w / 4.0f;
    const char* names[4] = {"原样", "平移", "缩放", "旋转"};
    for (int i = 0; i < 4; ++i) {
        const Rect r(box.x + step * float(i) + (step - bw) * 0.5f, box.y, bw, bh);
        aino::Transform t;
        if (i == 1) t.translate = Vec2(8.0f, 6.0f);
        if (i == 2) { t.scale = 1.3f; t.origin = Vec2(r.cx(), r.cy()); }
        if (i == 3) { t.rotate = 18.0f; t.origin = Vec2(r.cx(), r.cy()); }
        ui.push_transform(t);
        ui.panel(r, Style().background(ui.theme().primary).corner(10.0f));
        ui.label("A", r, Style().center().semi_bold().font(20.0f)
                 .color(ui.theme().primary_foreground));
        ui.pop_transform();
        ui.muted_text(names[i], Rect(r.x, r.bottom() + 6.0f, r.w, 16.0f),
                      Style().center().font(11.0f));
    }
}

void d_tween(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 10.0f);
    const Rect track = c.next(64.0f);
    ui.panel(track, Style().background(ui.theme().muted).corner(8.0f));
    const float k = ui.tween(aino::detail::make_id("gallery_tween", track),
                             a.motion ? 1.0f : 0.0f, 0.7f, aino::easing::out_cubic);
    const float bw = 58.0f, bh = 34.0f;
    const float x0 = track.x + 10.0f;
    const float x1 = track.right() - 10.0f - bw;
    const Rect kn(x0 + (x1 - x0) * k, track.cy() - bh * 0.5f, bw, bh);
    aino::Transform tf;
    tf.rotate = -12.0f + 24.0f * k;
    tf.origin = Vec2(kn.cx(), kn.cy());
    ui.push_transform(tf);
    ui.panel(kn, Style().background(ui.theme().primary).corner(8.0f));
    ui.label("aino", kn, Style().center().font(12.0f).color(ui.theme().primary_foreground));
    ui.pop_transform();
    if (ui.button(a.motion ? "复位" : "播放", c.next(34.0f), Style().outline()))
        a.motion = !a.motion;
    char buf[48];
    std::snprintf(buf, sizeof buf, "t = %.3f", double(k));
    ui.muted_text(buf, c.next(16.0f), Style().left().font(11.0f));
}

// =============================================================================
//  组件表
// =============================================================================
const Demo kBasic[] = {
    {"六种语义变体", "Style::Variant 决定配色，高度由 box 决定",
     R"cpp(Style st;
st.variant = Style::Default;
ui.button("Default", c.next(24.0f), st);

st.variant = Style::Link;
ui.button("Link", c.next(24.0f), st);)cpp",
     256.0f, d_btn_variants},
    {"禁用与按压缩放", "disabled 不参与命中；press_scale 只影响按下帧",
     R"cpp(Style dis;
dis.disabled = true;
ui.button("禁用", r, dis);

Style pressy;
pressy.variant = Style::Secondary;
pressy.press_scale = 0.94f;
ui.button("按压缩放", r, pressy);)cpp",
     40.0f, d_btn_states},
    {"图标按钮", "宽度由 box 决定，字形居中",
     R"cpp(ui.icon_button("+", r.next(24.0f), Style().outline());
ui.icon_button("-", r.next(24.0f), Style().outline());
ui.icon_button("x", r.next(24.0f), Style().outline());)cpp",
     36.0f, d_btn_icon},
};

const Demo kBadge[] = {
    {"四种变体", "Outline 与 Destructive 用于状态区分",
     R"cpp(ui.badge("Default", r.next(74.0f));
Style sec; sec.variant = Style::Secondary;
ui.badge("Secondary", r.next(84.0f), sec);
Style out; out.variant = Style::Outline;
ui.badge("Outline", r.next(70.0f), out);)cpp",
     24.0f, d_badge},
    {"键帽与分隔线", "kbd 的宽度按文字实测，不按字符数估",
     R"cpp(ui.kbd("Ctrl", r.next_text("Ctrl", 16.0f, 11.0f, 500));
ui.muted_text("+", r.next(10.0f), Style().center());
ui.kbd("K", r.next_text("K", 18.0f, 11.0f, 500));
ui.separator(c.next(1.0f));)cpp",
     82.0f, d_kbd_sep},
};

const Demo kAvatar[] = {
    {"头像", "圆形；文字取首字，尺寸由 box 决定",
     R"cpp(ui.avatar("张", r.next(24.0f));
ui.avatar("李四", r.next(24.0f));
ui.avatar("A", r.next(64.0f));)cpp",
     64.0f, d_avatar},
};

const Demo kProgress[] = {
    {"进度 / 骨架 / 指示器", "skeleton 与 spinner 默认静态，加 .animate() 才动",
     R"cpp(ui.progress(0.24f, c.next(8.0f));
ui.progress(0.92f, c.next(8.0f));
ui.skeleton(r.next(120.0f));
ui.spinner(r.next(24.0f));)cpp",
     88.0f, d_progress},
};

const Demo kInput[] = {
    {"单行输入", "焦点、选区、剪贴板、IME 组合串全部内建",
     R"cpp(ui.input(&a.email, c.next(24.0f),
         Style().hint("you@example.com"));
ui.input(&a.keyword, c.next(24.0f), Style().hint("搜索…"));)cpp",
     120.0f, d_input},
    {"多行输入", "Enter 换行，长文按宽度折行，高度由 box 决定",
     R"cpp(ui.textarea(&a.note, box, Style().font(13.0f));)cpp",
     104.0f, d_textarea},
};

const Demo kInputGroup[] = {
    {"前后缀", "整块区域都是热区，点前后缀也能落光标",
     R"cpp(ui.input_group(&a.amount, "¥", "元", c.next(24.0f));
ui.input_group(&a.phone, "+86", "", c.next(24.0f));)cpp",
     120.0f, d_input_group},
};

const Demo kOtp[] = {
    {"六位验证码", "只收数字，自动前进，退格回退",
     R"cpp(ui.input_otp(&a.otp, 6, box);)cpp",
     48.0f, d_otp},
};

const Demo kCheckbox[] = {
    {"复选框", "整行可点，不只是一边的方块",
     R"cpp(ui.checkbox(&a.remember, "记住这台设备", c.next(22.0f));
ui.checkbox(&a.autosave, "自动保存", c.next(22.0f));)cpp",
     90.0f, d_checkbox},
};

const Demo kSwitch[] = {
    {"开关", "toggle() 是 Switch；toggle_button() 是按下态按钮",
     R"cpp(ui.toggle(&a.wifi, Rect(r.right() - 44.0f, r.y, 44.0f, 26.0f));)cpp",
     98.0f, d_switch},
};

const Demo kRadio[] = {
    {"单选组", "按 box 高度均分，每项等距",
     R"cpp(ui.radio_group({"免费版", "专业版", "企业版"}, &a.radio, box);)cpp",
     76.0f, d_radio},
};

const Demo kSlider[] = {
    {"滑块", "拖动时连续写回，两端夹取到 min/max",
     R"cpp(ui.slider(&a.vol, 0.0f, 1.0f, c.next(26.0f));)cpp",
     58.0f, d_slider},
};

const Demo kSelect[] = {
    {"下拉选择", "列表帧末绘制；点外部或 Esc 关闭",
     R"cpp(ui.select({"免费版", "专业版", "企业版"}, &a.plan,
          c.next(24.0f), Style().hint("选择套餐"));)cpp",
     74.0f, d_select},
};

const Demo kCombobox[] = {
    {"可搜索下拉", "展开后键入即过滤，↑↓ 移动，回车确认",
     R"cpp(ui.combobox({"C++", "Rust", "Go", "Zig", "Python"},
             &a.combo, c.next(24.0f));)cpp",
     74.0f, d_combobox},
};

const Demo kToggle[] = {
    {"切换按钮与分组", "分组为单选；再点当前项会取消（index = -1）",
     R"cpp(ui.toggle_button("粗体", &a.bold, r.next(84.0f));
ui.toggle_button("斜体", &a.italic, r.next(84.0f));
ui.toggle_group({"日", "周", "月"}, &a.tg, c.next(34.0f));)cpp",
     118.0f, d_toggle},
};

const Demo kButtonGroup[] = {
    {"按钮组", "相邻共享边框；多选用位掩码",
     R"cpp(ui.button_group({"复制", "粘贴", "剪切"}, &a.bg_click, c.next(34.0f));
ui.toggle_group_multi({"B", "I", "U"}, &a.tgm, c.next(34.0f));)cpp",
     116.0f, d_button_group},
};

const Demo kStepper[] = {
    {"步进器与评分", "两者都会夹取到合法区间",
     R"cpp(ui.stepper(&a.step, 1.0f, 0.0f, 99.0f, c.next(24.0f));
ui.rating(&a.stars, 5, c.next(24.0f));)cpp",
     118.0f, d_stepper_rating},
};

const Demo kField[] = {
    {"表单字段", "label / hint / error 三段高度由 field_height 量出",
     R"cpp(float h = ui.field_height("邮箱", "用于接收通知", "", 24.0f);
ui.field("邮箱", "用于接收通知", "", c.next(h), [&](const Rect& r) {
    ui.input(&a.email, r, Style().hint("you@example.com"));
});)cpp",
     200.0f, d_field},
};

const Demo kCalendar[] = {
    {"月历", "纯整数日期算法，不依赖 ctime，跨时区一致",
     R"cpp(ui.calendar(&a.cy, &a.cm, &a.cd, box);)cpp",
     206.0f, d_calendar},
};

const Demo kTabs[] = {
    {"标签页与胶囊", "两套视觉，同一套索引语义",
     R"cpp(ui.tabs({"概览", "分析", "设置"}, &a.tab, c.next(24.0f));
ui.segmented({"免费版", "专业版", "企业版"}, &a.seg, c.next(32.0f));)cpp",
     120.0f, d_tabs},
};

const Demo kTable[] = {
    {"表格", "列宽按内容实测分配，行高固定",
     R"cpp(ui.table({"模块", "状态", "耗时"}, rows, box,
         Style().font(12.5f));)cpp",
     176.0f, d_table},
};

const Demo kCard[] = {
    {"面板与列表项", "panel 是容器；item 有标题、描述与右侧插槽",
     R"cpp(ui.panel(p);
ui.item("张伟", "zhangwei@example.com", "管理员", c.next(50.0f));)cpp",
     190.0f, d_card},
};

const Demo kEmpty[] = {
    {"空状态", "虚线框内居中排版，标题与描述分层",
     R"cpp(ui.empty_state("还没有通知", "有新消息时会出现在这里", box);)cpp",
     112.0f, d_empty},
};

const Demo kPagination[] = {
    {"分页", "槽位数量恒定，翻页时整排不会左右跳",
     R"cpp(ui.pagination(&a.page_no, 20, c.next(24.0f));)cpp",
     64.0f, d_pagination},
};

const Demo kAccordion[] = {
    {"折叠面板", "展开区域由调用方绘制，控件只负责标题行",
     R"cpp(ui.accordion("渲染管线", &a.acc1, c.next(24.0f));
if (a.acc1) ui.muted_text("...", c.next(20.0f));)cpp",
     114.0f, d_accordion},
};

const Demo kAlert[] = {
    {"提示条", "正文按宽度折行；高度由 alert_height 量出",
     R"cpp(float h = ui.alert_height(title, desc, w);
ui.alert(title, desc, c.next(h));)cpp",
     154.0f, d_alert},
};

const Demo kBreadcrumb[] = {
    {"面包屑", "分隔符随主题前景色，末项高亮",
     R"cpp(ui.breadcrumb({"控制台", "组件", "导航"}, box);)cpp",
     40.0f, d_breadcrumb},
};

const Demo kChart[] = {
    {"折线与柱状", "数值数组直接传入，负值向下画",
     R"cpp(ui.sparkline(wave, c.next(72.0f));
ui.bars(bars, c.next(72.0f));)cpp",
     162.0f, d_chart},
};

const Demo kMenu[] = {
    {"下拉菜单", "选中项写入 index 并返回 true",
     R"cpp(if (ui.menu("操作", items, &a.menu_pick,
                 Rect(box.x, box.y, 132.0f, 24.0f), Style().outline())) {
    a.last = items[a.menu_pick];
})cpp",
     72.0f, d_menu},
};

const Demo kMenubar[] = {
    {"菜单栏", "打开一项后横向移动自动切换；整条栏一起登记输入闸门",
     R"cpp(MenuPick mp = ui.menubar(labels, items, bar);
if (mp.valid()) {
    a.picked = labels[mp.menu] + " -> " + items[mp.menu][mp.item];
})cpp",
     68.0f, d_menubar},
};

const Demo kPopover[] = {
    {"浮出面板", "锚点与内容尺寸都由调用方给，点外部关闭",
     R"cpp(ui.popover("排序", &a.pop, anchor, Vec2(190.0f, 104.0f),
           [&](const Rect& c) { ui.item("按名称", "", "", c); });)cpp",
     72.0f, d_popover},
};

const Demo kHoverCard[] = {
    {"悬停卡片", "只响应悬停，不参与点击命中，因此不会挡住锚点本身",
     R"cpp(ui.hover_card(anchor, Vec2(220.0f, 84.0f), [&](const Rect& c) {
    ui.label("张伟", Rect(c.x, c.y, c.w, 20.0f));
});)cpp",
     60.0f, d_hover_card},
};

const Demo kDialog[] = {
    {"对话框", "遮罩、面板、折行、关闭按钮由 dialog 负责；按钮调用方续画",
     R"cpp(if (ui.dialog("确认删除", body, &a.dlg, d)) {
    Stack b = ui.hbox(Rect(d.x + 22.0f, d.bottom() - 58.0f,
                           d.w - 44.0f, 24.0f), 10.0f);
    if (ui.button("取消", b.part(0, 2), Style().outline())) a.dlg = false;
})cpp",
     72.0f, d_dialog},
    {"确认弹层", "带语义色按钮的双按钮版本，返回值表示用户已确认",
     R"cpp(ui.alert_dialog("删除这个文件？", body, "删除", "取消",
                &a.confirm, rect, Style().destructive());)cpp",
     72.0f, d_alert_dialog},
};

const Demo kDrawer[] = {
    {"抽屉", "side 0 从右、1 从左；遮罩淡入但不做位移动画",
     R"cpp(ui.drawer("详情", &a.drawer, 0, rect, [&](const Rect& c) {
    ui.label("详情面板", c);
});)cpp",
     72.0f, d_drawer},
};

const Demo kToast[] = {
    {"轻提示", "右下角，2.4 秒后自行消失；期间由窗口驱动重绘",
     R"cpp(a.toast_msg = "操作已完成";
a.toast_left = 2.4f;   // 由 composeOverlayLayer 绘制)cpp",
     72.0f, d_toast},
};

const Demo kTooltip[] = {
    {"悬停提示", "任意矩形都能当锚点；移开即消失",
     R"cpp(ui.tooltip(btn_rect, "tooltip 在悬停后出现");
ui.tooltip(badge_rect, "任意矩形都能作为锚点");)cpp",
     44.0f, d_tooltip},
};

const Demo kContextMenu[] = {
    {"右键菜单", "在 area 内右键弹出；位置跟随鼠标",
     R"cpp(ui.context_menu(area, {"标记已读", "归档", "删除"}, &a.ctx_pick);)cpp",
     92.0f, d_context_menu},
};

const Demo kLayout[] = {
    {"Stack 布局", "只给高度，位置由布局器推导；part 均分、next_text 按文字宽量",
     R"cpp(Stack r = c.hbox(36.0f, 8.0f);
ui.button("等分 1/3", r.part(0, 3), Style().outline());
ui.button("2/3", r.part(1, 3), Style().outline());

Stack b = c.hbox(36.0f, 10.0f);
ui.button("保存并关闭", b.next_text("保存并关闭", 34.0f),
          Style().primary());)cpp",
     156.0f, d_layout},
};

const Demo kScroll[] = {
    {"通用滚动", "内容高度显式给出；滚轮、拖动条、to() 三条路径共用同一视口",
     R"cpp(auto sc = ui.scroll(vp, item_h * float(n));
if (cmd == Top) sc.to(0.0f);
if (cmd == Bottom) sc.to(1e6f);
for (int i = 0; i < n; ++i) {
    const Rect r = ui.row(item_h);
})cpp",
     200.0f, d_scroll},
    {"虚拟列表", "十万行只对可见行调用回调，绘制行数由返回值给出",
     R"cpp(int drawn = ui.scroll(vp, n, item_h, [&](int i, const Rect& r) {
    ui.label(row_text(i), r);
});)cpp",
     200.0f, d_virtual},
};

const Demo kSplit[] = {
    {"分割面板", "ratio 由拖动写回，两侧矩形同步给出",
     R"cpp(Rect l, r;
ui.split_pane(&a.split, box, &l, &r, true, 0.2f);
ui.panel(l);
ui.panel(r);)cpp",
     132.0f, d_split},
};

const Demo kClip[] = {
    {"裁剪", "几何与文字共用裁剪区，可嵌套（取交集）",
     R"cpp(ui.push_clip(box);
ui.label("超出容器宽度的文字会被裁掉", rect);
ui.pop_clip();)cpp",
     92.0f, d_clip},
};

const Demo kAspect[] = {
    {"宽高比", "立即模式没有容器组件，它是个算 Rect 的静态方法",
     R"cpp(Rect r = Ui::aspect_rect(Rect(x, y, w, 0.0f), 16.0f / 9.0f);
// r.h 由宽度按比例算出)cpp",
     118.0f, d_aspect},
};

const Demo kPrimitives[] = {
    {"八个图元", "几何自绘并抗锯齿；月牙用几何减法而不是挖洞",
     R"cpp(ui.fill_rect(r, fg, 10.0f);
ui.stroke_rect(r, fg, 10.0f, 1.5f);
ui.fill_circle(cx, cy, 22.0f, fg);
ui.fill_crescent(cx, cy, 22.0f, cx + 8.5f, cy - 5.0f, 21.8f, fg);
ui.draw_line(x0, y0, x1, y1, fg, 1.5f);
ui.draw_text("...", box, fg);
ui.drop_shadow(r, 8.0f, 12.0f, color);
ui.clear(color);)cpp",
     96.0f, d_primitives},
};

const Demo kTransform[] = {
    {"仿射变换", "只影响绘制，不影响布局与命中判定；origin 是世界坐标",
     R"cpp(aino::Transform t;
t.scale = 1.3f;
t.rotate = 18.0f;
t.origin = Vec2(r.cx(), r.cy());
ui.push_transform(t);
ui.panel(r);
ui.pop_transform();)cpp",
     98.0f, d_transform},
};

const Demo kTween[] = {
    {"补间", "目标中途改变会从当前值续跑；动画期间自动请求重绘，到点即停",
     R"cpp(float k = ui.tween(id, a.motion ? 1.0f : 0.0f,
                      0.7f, aino::easing::out_cubic);
t.rotate = -12.0f + 24.0f * k;)cpp",
     142.0f, d_tween},
};

#define COUNT(a) int(sizeof(a) / sizeof((a)[0]))

const Page kPages[] = {
    {"入门", "概览", "库的定位、结构与实测数据。", "—", nullptr, 0},
    {"基础", "Button", "触发一个动作。六种语义变体，统一 24 高度（Medium 档）。", "Button",
     kBasic, COUNT(kBasic)},
    {"基础", "Badge", "状态标记与键帽。", "Badge / Kbd",
     kBadge, COUNT(kBadge)},
    {"基础", "Avatar", "以首字成像的圆形头像。", "Avatar",
     kAvatar, COUNT(kAvatar)},
    {"基础", "Progress", "进度、骨架与加载指示。", "Progress / Skeleton / Spinner",
     kProgress, COUNT(kProgress)},
    {"表单", "Input", "单行与多行文本输入。", "Input / Textarea",
     kInput, COUNT(kInput)},
    {"表单", "Input Group", "带前后缀的输入框。", "Input Group",
     kInputGroup, COUNT(kInputGroup)},
    {"表单", "Input OTP", "定长数字验证码。", "Input OTP",
     kOtp, COUNT(kOtp)},
    {"表单", "Checkbox", "多选，整行可点。", "Checkbox",
     kCheckbox, COUNT(kCheckbox)},
    {"表单", "Switch", "二态开关。", "Switch",
     kSwitch, COUNT(kSwitch)},
    {"表单", "Radio Group", "互斥单选。", "Radio Group",
     kRadio, COUNT(kRadio)},
    {"表单", "Slider", "连续值区间选择。", "Slider",
     kSlider, COUNT(kSlider)},
    {"表单", "Select", "下拉单选。", "Select",
     kSelect, COUNT(kSelect)},
    {"表单", "Combobox", "可键入过滤的下拉。", "Combobox",
     kCombobox, COUNT(kCombobox)},
    {"表单", "Toggle", "按下态按钮与分组。", "Toggle / Toggle Group",
     kToggle, COUNT(kToggle)},
    {"表单", "Button Group", "相邻按钮共享边框，支持多选。", "Button Group",
     kButtonGroup, COUNT(kButtonGroup)},
    {"表单", "Stepper", "数值步进与星级评分。", "—",
     kStepper, COUNT(kStepper)},
    {"表单", "Form Field", "标签、说明、错误的纵向组合。", "Field",
     kField, COUNT(kField)},
    {"表单", "Calendar", "月视图日期选择。", "Calendar",
     kCalendar, COUNT(kCalendar)},
    {"数据展示", "Tabs", "标签页与胶囊分段。", "Tabs / Toggle Group",
     kTabs, COUNT(kTabs)},
    {"数据展示", "Table", "只读表格。", "Table",
     kTable, COUNT(kTable)},
    {"数据展示", "Card", "容器与带插槽的列表项。", "Card / Item",
     kCard, COUNT(kCard)},
    {"数据展示", "Empty", "空状态占位。", "Empty",
     kEmpty, COUNT(kEmpty)},
    {"数据展示", "Pagination", "页码导航。", "Pagination",
     kPagination, COUNT(kPagination)},
    {"数据展示", "Accordion", "单段折叠。", "Accordion",
     kAccordion, COUNT(kAccordion)},
    {"数据展示", "Alert", "提示条。", "Alert",
     kAlert, COUNT(kAlert)},
    {"数据展示", "Breadcrumb", "路径导航。", "Breadcrumb",
     kBreadcrumb, COUNT(kBreadcrumb)},
    {"数据展示", "Chart", "折线与柱状。", "Chart",
     kChart, COUNT(kChart)},
    {"浮层", "Menu", "锚定下拉菜单。", "Dropdown Menu",
     kMenu, COUNT(kMenu)},
    {"浮层", "Menubar", "窗口级菜单栏。", "Menubar",
     kMenubar, COUNT(kMenubar)},
    {"浮层", "Popover", "任意内容的浮出面板。", "Popover",
     kPopover, COUNT(kPopover)},
    {"浮层", "Hover Card", "悬停预览卡片。", "Hover Card",
     kHoverCard, COUNT(kHoverCard)},
    {"浮层", "Dialog", "模态对话框与确认弹层。", "Dialog / Alert Dialog",
     kDialog, COUNT(kDialog), o_dialog},
    {"浮层", "Drawer", "侧边抽屉。", "Sheet / Drawer",
     kDrawer, COUNT(kDrawer), o_drawer},
    {"浮层", "Toast", "短暂的操作反馈。", "Toast / Sonner",
     kToast, COUNT(kToast)},
    {"浮层", "Tooltip", "悬停说明。", "Tooltip",
     kTooltip, COUNT(kTooltip)},
    {"浮层", "Context Menu", "右键菜单。", "Context Menu",
     kContextMenu, COUNT(kContextMenu)},
    {"布局", "Layout", "Stack 与均分。", "—",
     kLayout, COUNT(kLayout)},
    {"布局", "Scroll", "通用滚动与虚拟列表。", "Scroll Area",
     kScroll, COUNT(kScroll)},
    {"布局", "Split Pane", "可拖动的分割面板。", "Resizable",
     kSplit, COUNT(kSplit)},
    {"布局", "Clip", "矩形裁剪。", "—",
     kClip, COUNT(kClip)},
    {"布局", "Aspect Ratio", "按比例推导高度。", "Aspect Ratio",
     kAspect, COUNT(kAspect)},
    {"图形与动效", "Primitives", "八个绘制图元。", "—",
     kPrimitives, COUNT(kPrimitives)},
    {"图形与动效", "Transform", "平移、缩放、旋转。", "—",
     kTransform, COUNT(kTransform)},
    {"图形与动效", "Tween", "补间与缓动。", "—",
     kTween, COUNT(kTween)},
};

const int kPageCount = COUNT(kPages);

// 每个分组有几页。概览页的"分类"卡读它 —— 以前是手写数字，加页之后必然对不上
// （写着 5 页，实际 4 页）。定义放在这里是因为要用 kPages，先声明在概览页之前。
const std::vector<std::pair<const char*, int>>& group_stats() {
    static const std::vector<std::pair<const char*, int>> v = [] {
        std::vector<std::pair<const char*, int>> r;
        for (int i = 0; i < kPageCount; ++i) {
            const char* g = kPages[i].group;
            if (r.empty() || std::string(r.back().first) != g) r.push_back({g, 1});
            else ++r.back().second;
        }
        return r;
    }();
    return v;
}

// 每页属于第几个分组（0 起）。分组在 kPages 里是连续排列的，所以扫一遍就够；
// 导航的分组折叠靠它把"这一页在哪个组"换算成位掩码下标。
const std::vector<int> kPageGroup = [] {
    std::vector<int> v(size_t(kPageCount), 0);
    int g = 0;
    for (int i = 1; i < kPageCount; ++i) {
        if (std::string(kPages[i].group) != std::string(kPages[i - 1].group)) ++g;
        v[size_t(i)] = g;
    }
    return v;
}();
const int kGroupCount = kPageGroup.empty() ? 0 : kPageGroup.back() + 1;

// =============================================================================
//  导航
// =============================================================================
constexpr float kNavW = 232.0f;
constexpr float kNavItemH = 24.0f;
constexpr float kNavGroupH = 26.0f;
constexpr float kHeaderH = 92.0f;

// 折叠状态下的列表高度也要算准：滚动条的 max_offset 直接用它，
// 少算了下半截就滚不到，多算了就是一片空白。
float nav_content_height(const App& a) {
    float h = 0.0f;
    for (int i = 0; i < kPageCount; ++i) {
        if (i == 0 || std::string(kPages[i].group) != std::string(kPages[i - 1].group))
            h += kNavGroupH;
        if (!(a.nav_closed & (1u << unsigned(kPageGroup[size_t(i)])))) h += kNavItemH;
    }
    return h + 8.0f;
}

void draw_nav(Ui& ui, App& a, const Rect& nav) {
    const aino::Theme& th = ui.theme();
    // 侧边栏是 macOS 的 source list 面：比内容区深一档的整片浅灰（暗色下更暗），
    // 不是"背景色 + 一点点灰"。
    ui.fill_rect(nav, th.sidebar_bg, 0.0f);
    ui.fill_rect(Rect(nav.right() - 1.0f, nav.y, 1.0f, nav.h), th.separator, 0.0f);

    // 品牌区。不用 vbox：这里只有两行固定位置，且 pad 会同时吃掉上下留白，
    // 两行在 78px 里会叠在一起。
    ui.label("aino", Rect(nav.x + 18.0f, nav.y + 14.0f, nav.w - 36.0f, 20.0f),
             Style().left().semi_bold().font(15.0f));
    ui.muted_text("单头文件 GUI 库 · " + std::string(AINO_VERSION),
                  Rect(nav.x + 18.0f, nav.y + 34.0f, nav.w - 36.0f, 14.0f),
                  Style().left().font(11.0f));

    // 底部工具条
    const float foot_h = 52.0f;
    const Rect foot(nav.x, nav.bottom() - foot_h, nav.w, foot_h);
    ui.fill_rect(Rect(foot.x, foot.y, foot.w - 1.0f, 1.0f), th.border, 0.0f);
    {
        Stack f = ui.hbox(Rect(foot.x + 16.0f, foot.y, foot.w - 32.0f, foot_h), 10.0f);
        const Rect tb = f.next(34.0f);
        // hbox 切出来的横切片是**整行高**（52），而按钮是 34×34 的正方形。
        // 用 center_y 在行里居中，别写 tb.y + 9 —— 行高一改就和右边的字错位。
        const Rect ib = tb.center_y(34.0f);
        if (ui.icon_button("", ib, Style().outline()))
            a.dark = !a.dark;
        // 太阳 / 月亮自绘：字体里的这两个符号在部分系统缺字形，会显示成方块
        {
            const Color ic = th.foreground;
            const float bx = ib.cx(), by = ib.cy();
            if (a.dark) {
                ui.fill_circle(bx, by, 5.0f, ic);
                for (int k = 0; k < 8; ++k) {
                    const float ang = float(k) * 3.14159265f / 4.0f;
                    ui.draw_line(bx + std::cos(ang) * 7.4f, by + std::sin(ang) * 7.4f,
                                 bx + std::cos(ang) * 10.0f, by + std::sin(ang) * 10.0f, ic, 1.5f);
                }
            } else {
                // 月牙 = 圆减圆。用底色挖洞的做法在换主题后会露出错色。
                ui.fill_crescent(bx + 0.8f, by, 7.6f, bx + 4.4f, by - 2.6f, 8.2f, ic);
            }
        }
        // 同理：文字盒子直接给整行，垂直居中交给 Style 的 valign。
        ui.muted_text(a.dark ? "深色" : "浅色", f.rest(), Style().left().font(11.5f));
    }

    // 列表
    const Rect list(nav.x, 78.0f, nav.w, nav.bottom() - foot_h - 78.0f);
    auto sc = ui.scroll(list, nav_content_height(a));
    (void)sc;
    for (int i = 0; i < kPageCount; ++i) {
        const int gi = kPageGroup[size_t(i)];
        const bool open = !(a.nav_closed & (1u << unsigned(gi)));
        if (i == 0 || std::string(kPages[i].group) != std::string(kPages[i - 1].group)) {
            const Rect gh = ui.row(kNavGroupH);
            // 组头本身就是折叠开关。用空的 ghost 按钮占位 —— 它不画文字，
            // 只在悬停时给一层底色，点击热区就是整行。
            // 折叠不改变当前页：导航收起不等于离开这一页（和编辑器折叠目录一样）。
            if (ui.button("", Rect(gh.x + 10.0f, gh.y, gh.w - 20.0f, gh.h), Style().ghost())) {
                a.nav_closed ^= (1u << unsigned(gi));
                // 按钮点完会拿到键盘焦点，焦点环留在行上像"这行被选中了"。
                // 组头只是折叠开关，立刻交还焦点。
                ui.set_focus(nullptr);
            }
            // 文字的盒子就是整行：Style 的 valign 默认就是 Middle，所以它会
            // 自动落到行中心，和右边那个手工算中心点的折叠三角对齐。
            // 别再写 y + 12 这种固定偏移 —— 行高一改就偏，而且和三角不在一条线上。
            ui.muted_text(kPages[i].group,
                          Rect(gh.x + 18.0f, gh.y, gh.w - 50.0f, gh.h),
                          Style().left().font(11.0f));
            // 折叠标记：展开时朝下，折叠时朝右（两条线拼的尖角，不依赖字形）
            {
                const float cx = gh.right() - 26.0f, cy = gh.y + gh.h * 0.5f;
                const Color cc = th.muted_foreground;
                if (open) {
                    ui.draw_line(cx - 4.0f, cy - 2.0f, cx, cy + 2.0f, cc, 1.4f);
                    ui.draw_line(cx, cy + 2.0f, cx + 4.0f, cy - 2.0f, cc, 1.4f);
                } else {
                    ui.draw_line(cx - 2.0f, cy - 4.0f, cx + 2.0f, cy, cc, 1.4f);
                    ui.draw_line(cx + 2.0f, cy, cx - 2.0f, cy + 4.0f, cc, 1.4f);
                }
            }
        }
        if (!open) continue;
        const Rect r = ui.row(kNavItemH);
        const bool active = (a.page == i);
        if (ui.nav_item(kPages[i].name, active, Rect(r.x + 8.0f, r.y, r.w - 16.0f, r.h))) {
            if (!active) a.page = i;
        }
    }
}

// =============================================================================
//  内容区
// =============================================================================
struct CardGeom {
    float pad = 14.0f;
    float title_h = 30.0f;
    float code_w = 0.0f;
    float body_w = 0.0f;
    float preview_w = 0.0f;
    float inner_h = 0.0f;    // 交给示例函数的内容盒高度
    float body_h = 0.0f;     // 预览底板（= 内容盒 + 上下各 16 的留白）
};

// 预览底板的上下留白。
constexpr float kPlatePadY = 16.0f;

// 交给示例函数的**内容盒**宽度：预览底板再内缩 20px（左右各 20，共 40）。
// 测量和绘制都必须用这一个函数，否则量出来的高度和画出来的不一样。
float preview_inner_w(float card_w) {
    const float code_w = std::max(card_w * 0.42f, 280.0f);
    const float body_w = card_w - 24.0f;
    return body_w - code_w - 12.0f - 40.0f;
}

// 卡片几何。need_h 是内容实测高度，和 preview_h（示例的声明高度）取大：
// 声明值只当"最小高度"，内容比它高就自动长高。
CardGeom card_geom(Ui& ui, const Demo& d, float card_w, float need_h) {
    CardGeom g;
    g.code_w    = std::max(card_w * 0.42f, 280.0f);
    g.body_w    = card_w - 2.0f * g.pad;
    g.preview_w = g.body_w - g.code_w - 12.0f;
    g.inner_h = std::max(d.preview_h, need_h);
    // 行高只由**内容**决定，不再被源码块撑开 —— 之前代码一长，预览区就被拉出
    // 一大片空白，整页看起来像文档站。源码超出就裁（code_block 按 box 高度自行截断）。
    g.body_h = g.inner_h + 2.0f * kPlatePadY;
    return g;
}

// 内容实测高度：让示例在测量模式下空跑一遍（不落像素、不响应、不排帧）。
// 以前卡片高度全靠手填的 preview_h，45 页 × 每页若干示例里总有几条写小了，
// 结果就是内容爬出卡片下边框。现在声明值只是下限，高度永远够。
float demo_need_h(Ui& ui, App& a, const Demo& d, float inner_w) {
    ui.begin_measure();
    d.draw(ui, a, Rect(0.0f, 0.0f, inner_w, d.preview_h));
    const aino::Rect used = ui.end_measure();
    return std::max(used.h, 0.0f);
}

void draw_demo_card(Ui& ui, App& a, const Demo& d, const Rect& card, const CardGeom& g) {
    const aino::Theme& th = ui.theme();
    const float body_h = g.body_h;

    // ---- 标题在**上面**：macOS 的分组标题是"标题 + 右侧说明"，内容在下面。
    // 之前把标题放在卡片底部、左边套一块带边框的底板，那是 Web 文档站的写法。
    const Rect bar(card.x, card.y, g.body_w, 20.0f);
    ui.label(d.name, Rect(bar.x, bar.y, bar.w * 0.45f, bar.h),
             Style().left().semi_bold().font(13.0f));
    ui.muted_text(d.note, Rect(bar.x, bar.y, bar.w, bar.h),
                  Style().right().font(11.0f));

    const Rect body(card.x, bar.bottom() + 10.0f, g.body_w, body_h);
    const Rect pre(body.x, body.y, g.preview_w, body_h);
    // 代码块与预览区等高：左右两条边对齐
    const Rect cod(body.right() - g.code_w, body.y, g.code_w, body_h);

    // ---- 预览区：直接铺在窗口背景上，不套底板、不描边 ----
    // macOS 的内容区就是"背景 + 内容"，分层靠明度差；一圈 1px 边框 + 圆角卡片
    // 是 Web 语言，也是这套界面之前看起来"像网页"的主要原因之一。
    const Rect content(pre.x + 20.0f,
                       pre.y + kPlatePadY + (body_h - 2.0f * kPlatePadY - g.inner_h) * 0.5f,
                       pre.w - 40.0f, g.inner_h);
    d.draw(ui, a, content);

    ui.code_block(d.code, cod, Style().font(11.5f));
}

void draw_page(Ui& ui, App& a, const Rect& area) {
    const Page& p = kPages[a.page];

    // ---- 页头 ----
    {
        Stack h = ui.vbox(Rect(area.x, area.y, area.w, kHeaderH), 0.0f);
        ui.heading(p.name, h.next(26.0f), Style().left().font(20.0f));
        ui.muted_text(p.desc, h.next(20.0f), Style().left().font(12.5f));

        if (std::string(p.shadcn) != "—") {
            // 对应关系写成右上角的次要文字。原来是描边胶囊——那是最典型的 Web 徽章，
            // macOS 的窗口里不会出现这种"标签条"。
            const std::string tag = "shadcn/ui · " + std::string(p.shadcn);
            const float tw = ui.text_width(tag, 11.0f) + 4.0f;
            ui.muted_text(tag, Rect(area.right() - tw, area.y + 6.0f, tw, 18.0f),
                          Style().right().font(11.0f));
            // 交互回显：点过按钮之后在页头右侧显示最近一次动作，
            // 这样"点下去到底有没有触发回调"在画面上是可见的。
            if (a.hits > 0) {
                ui.muted_text("最近 · " + a.last, Rect(area.x, area.y + 34.0f,
                                                      area.w - tw - 12.0f, 18.0f),
                              Style().right().font(11.0f));
            }
        }
    }
    const Rect rule(area.x, area.y + kHeaderH - 14.0f, area.w, 1.0f);
    ui.separator(rule);

    // ---- 内容 ----
    const Rect content(area.x, area.y + kHeaderH + 6.0f, area.w, area.bottom() - area.y - kHeaderH - 6.0f);
    if (a.page == 0) {
        draw_overview(ui, a, content);
        return;
    }

    // 先量高度，再定滚动范围与卡片尺寸。测量必须在 scroll 之外做：scroll 会给
    // 绘制加裁剪和偏移，测量时套上那层就量不到内容的真实高度了。
    const float card_w = content.w;
    const float inner_w = preview_inner_w(card_w);
    std::vector<float> need(size_t(p.n), 0.0f);
    std::vector<CardGeom> geo(size_t(p.n));
    float total = 8.0f;
    for (int i = 0; i < p.n; ++i) {
        need[size_t(i)] = demo_need_h(ui, a, p.demos[i], inner_w);
        geo[size_t(i)] = card_geom(ui, p.demos[i], card_w, need[size_t(i)]);
        total += geo[size_t(i)].body_h + 56.0f + 18.0f;
    }

    {
        auto sc = ui.scroll(content, total);
        (void)sc;
        for (int i = 0; i < p.n; ++i) {
            const CardGeom& g = geo[size_t(i)];
            const Rect row = ui.row(g.body_h + 56.0f + 18.0f);
            draw_demo_card(ui, a, p.demos[i],
                           Rect(row.x, row.y, row.w, g.body_h + 56.0f), g);
        }
    }
    // 模态浮层必须在 scroll 作用域**结束之后**画：滚动作用域持有一个裁剪区，
    // 在里面画的话遮罩只会盖住内容区，左侧导航还是亮的。
    if (p.overlay) p.overlay(ui, a);
}

// =============================================================================
//  浮层：最后画
// =============================================================================
void draw_overlays(Ui& ui, App& a) {
    if (a.toast_left > 0.0f) {
        const float k = std::min(1.0f, a.toast_left / 0.25f);
        const Rect tr(ui.width() - 320.0f, ui.height() - 68.0f, 296.0f, 44.0f);
        ui.toast(a.toast_msg, tr,
                 Style().font(12.5f).background(ui.theme().popover.alpha255(int(242.0f * k))));
    }
}

// =============================================================================
//  布局自检
// =============================================================================
// 把每一页每一条示例都量一遍，挑出"声明高度不符"的：need > declared 意味着
// 换回手填高度就会爬出卡片下边框；need < declared 只是底部多留白。
// 只在 AINO_LAYOUT_REPORT=<路径> 时跑一次，平时零开销。
void dump_layout_report(Ui& ui, App& a, float card_w) {
    const char* path = std::getenv("AINO_LAYOUT_REPORT");
    if (!path || !*path) return;
    std::FILE* f = std::fopen(path, "w");
    if (!f) return;
    const float inner_w = preview_inner_w(card_w);
    int over = 0, under = 0, total = 0;
    // 1.5px 容差：描边压在盒边界上时会往外占半个像素，不算溢出。
    const float tol = 1.5f;
    std::fprintf(f, "# aino gallery 布局报告  inner_w=%.1f\n", double(inner_w));
    std::fprintf(f, "# page/demo  declared  measured  delta\n");
    for (int i = 0; i < kPageCount; ++i) {
        const Page& p = kPages[i];
        for (int k = 0; k < p.n; ++k) {
            const Demo& d = p.demos[k];
            const float need = demo_need_h(ui, a, d, inner_w);
            const float delta = need - d.preview_h;
            ++total;
            if (delta > tol) ++over;
            if (delta < -0.5f) ++under;
            std::fprintf(f, "%-14s / %-22s %7.1f %9.1f %+8.1f%s\n",
                         p.name, d.name, double(d.preview_h), double(need), double(delta),
                         delta > tol ? "  <-- 溢出" : "");
        }
    }
    // total 是**示例条数**，不是页数 —— 之前这里写的是 kPageCount，页数和条数
    // 只差一个数，看着不显眼，对账的时候会以为少统计了。
    std::fprintf(f, "# over=%d under=%d total=%d\n", over, under, total);
    std::fclose(f);
}

// =============================================================================
//  main
// =============================================================================
void build_ui(Ui& ui, App& a) {
    // 主题要先落定，清屏用的就是新配色
    ui.set_theme(a.dark ? aino::Theme::dark() : aino::Theme::light());
    ui.clear(ui.theme().background);

    if (a.toast_left > 0.0f) {
        a.toast_left -= ui.frame_dt();
        ui.repaint();     // 全窗口唯一的常驻动画；没有 toast 时这一行不执行
    }

    draw_nav(ui, a, Rect(0.0f, 0.0f, kNavW, ui.height()));
    const Rect area(kNavW + 24.0f, 24.0f, ui.width() - kNavW - 56.0f,
                    std::max(ui.height() - 48.0f, 1.0f));
    draw_page(ui, a, area);
    draw_overlays(ui, a);
}

} // namespace

// 工具用的起始页：AINO_START_PAGE=<序号|名字>。截图 / 冒烟脚本要逐页拍，
// 靠点导航得先算滚动偏移和分组折叠状态，太容易跟着版式一起坏。
int start_page_from_env() {
    const char* sp = std::getenv("AINO_START_PAGE");
    if (!sp || !*sp) return 0;
    if (const int n = std::atoi(sp); n > 0 && n < kPageCount) return n;
    for (int i = 0; i < kPageCount; ++i) {
        if (std::string(kPages[i].name) == sp) return i;
    }
    return 0;
}

// 工具钩子：AINO_SHOT_BMP=<路径> —— 把后备缓冲存成 24bit BMP。
// xvfb 里没有交互桌面，Linux 上想看"真画成什么样"只能这么取帧；
// Windows 上平时不用（有 shot_pages.py 走 PrintWindow）。
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
    hd[0] = 'B'; hd[1] = 'M';
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
    for (int y = h - 1; y >= 0; --y) {              // BMP 自下而上
        const uint8_t* src = &cv.px[size_t(y) * size_t(cv.stride)];
        for (int x = 0; x < w; ++x) {
            line[size_t(x) * 3u + 0u] = src[size_t(x) * 4u + aino::detail::CH_B];
            line[size_t(x) * 3u + 1u] = src[size_t(x) * 4u + aino::detail::CH_G];
            line[size_t(x) * 3u + 2u] = src[size_t(x) * 4u + aino::detail::CH_R];
        }
        std::fwrite(line.data(), 1, line.size(), f);
    }
    std::fclose(f);
    std::fprintf(stderr, "[gallery] 已写出 %s（%dx%d）\n", path, w, h);
}

int main() {
    aino::Window win(1180, 760, "aino components");
    if (!win.ok()) return 1;
    App app;
    app.page = start_page_from_env();
    bool dumped = false;
    int frames = 0;
    // 截图钩子：在**下一帧开头**取上一帧的后备缓冲。
    // 弹层（下拉 / 菜单 / 对话框）都是 deferred 到帧末才画的，回调里立刻取会
    // 全部漏掉 —— 之前截出来的"下拉根本没展开"就是这个原因。
    const char* shot = std::getenv("AINO_SHOT_BMP");
    const bool shot_every = std::getenv("AINO_SHOT_EVERY") != nullptr;
    bool shot_pending = false;
    win.run([&](Ui& ui) {
        if (!dumped) {
            dumped = true;
            // 首帧量一次：此时字体引擎已就绪，量出来的宽度才是真宽度
            dump_layout_report(ui, app, ui.width() - kNavW - 56.0f);
        }
        if (shot && shot_pending && (shot_every || frames >= 2)) {
            dump_canvas_bmp(win, shot);
            shot_pending = false;
        }
        build_ui(ui, app);
        ++frames;
        shot_pending = true;
    });
    return 0;
}
