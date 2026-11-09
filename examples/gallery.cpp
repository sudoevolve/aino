// =============================================================================
//  aino 组件画廊
//
//  版式参照 macOS 的 App 窗口，不是 Web 文档站：
//    标题栏  红绿灯浮在侧边栏顶部，标题居中，右侧是外观分段控件
//    侧边栏  整窗高；顶部搜索框，下面是 source list（分组 + 可折叠条目）
//    内容区  Title 1 + Body 副标（右上角标对应的 AppKit 控件），
//            下面每条示例 = 一个 Group Box（表头行 + 细线 + 预览 + 可展开的源码）
//  示例区不用"左预览右源码"两栏：那是 shadcn 文档站的 Preview/Code 结构，
//  macOS 的分组语汇只有 Group Box（1px 细线 + r10，内部不铺底）。
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
#include <chrono>
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

    // ---- 窗口外壳 ----
    std::string query;             // 侧边栏搜索
    bool  nav_open = true;         // 侧边栏展开 / 收起（工具栏按钮）
    float nav_w    = 220.0f;       // 当前宽度，由 tween 驱动
    bool  quit     = false;        // 红绿灯的"红"按下

    // 反馈计数：按钮类示例共用，点完在卡片标题条里显示
    int         hits = 0;
    std::string last = "—";

    // 文本
    std::string email, keyword, note = "第一行\n第二行：Enter 换行，长文自动折行";
    std::string amount = "128", otp = "2048", uname = "张三", phone = "", cpwd = "";
    std::string search = "aino";     // 搜索框页
    int         side_sel = 0;        // 侧边栏示例的选中行
    int         well = 2;            // 颜色池示例的选中色

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

    // 每条示例的源码是否展开（第 i 位 = 当页第 i 条）。翻页时重置成"只展开第一条"，
    // 由 AINO_CODE=1 全展开（截图用）。
    std::uint32_t code_mask = 1u;
    int           code_page = -1;
    int           toggle_pending = -1;   // 下一帧要翻转的示例下标（见 draw_page）
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
    // 对应的 AppKit 控件。放在页头右侧当元信息 —— 这是本库唯一需要"对外解释
    // 自己是谁"的地方：一个 C++ 自绘控件长什么样，最省事的说法是"它等于哪个
    // 系统控件"。
    const char* appkit;
    const Demo* demos;
    int         n;
    // 本页的模态浮层。必须在这里画、而不是在卡片里画：dialog / drawer 是立即
    // 绘制的，放进滚动容器会被视口裁掉，还会被后面的卡片盖住。
    void (*overlay)(Ui&, App&) = nullptr;
    // 整页自定义排版。非空时跳过"示例卡片"那套（表头 + 代码块），整页交给它画。
    // 色彩 / 材质 / 字阶这三页是设计规范板，不是控件示例 —— 套进卡片表头反而
    // 变成"在一个框里再画一个框"。
    void (*custom)(Ui&, App&, const Rect&) = nullptr;
    // 版面列数。kit 的组件页是"一张画布上摆多簇变体"，本库用等宽多列来等价：
    // 2 列时每格 ~430，正好是这些示例原本的宽度；宽内容（表格/工具栏/材质）
    // 单独占满一行，传 1。
    int cols = 2;
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
    // 子窗口的诊断口：WIN32 子系统没有 stderr，只能落文件。
    auto clog = [](const char* msg) {
        if (const char* f = std::getenv("AINO_CHILD_LOG"))
            if (std::FILE* fp = std::fopen(f, "a")) {
                std::fprintf(fp, "%s\n", msg);
                std::fclose(fp);
            }
    };
    std::thread([clog] {
        // 这里刻意用 WindowConfig 而不是三参数构造：演示一下新加的配置项
        // （最小尺寸、居中）。标题里的版本号顺便当个"这窗口是谁开的"标记。
        aino::WindowConfig cfg;
        cfg.width      = 460;
        cfg.height     = 320;
        cfg.title      = "aino 最小示例 · " AINO_VERSION;
        cfg.min_width  = 380;
        cfg.min_height = 260;
        cfg.center     = true;

        clog("cfg 就绪，开始建窗口");
        aino::Window win(cfg);
        clog(win.ok() ? "窗口已创建" : "窗口创建失败 (ok=false)");
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

// 概览页的正片代码。抽出来是因为要用它量高度（code_block_height 和绘制必须
// 是同一份字符串，否则框会短一截）。
const char* kQuickstart =
    "#define AINO_IMPLEMENTATION\n"
    "#include \"aino.hpp\"\n"
    "#include <string>\n"
    "\n"
    "int main() {\n"
    "    aino::Window win(880, 560, \"aino\");\n"
    "    if (!win.ok()) return 1;\n"
    "    std::string email;\n"
    "    bool notify = true;\n"
    "    win.run([&](aino::Ui& ui) {\n"
    "        ui.panel({24, 24, 400, 240});\n"
    "        auto c = ui.vbox({24, 24, 400, 240}, 10, 18);\n"
    "        ui.heading(\"设置\", c.next(34));\n"
    "        ui.input(&email, c.next(36));\n"
    "        ui.checkbox(&notify, \"邮件通知\", c.next(22));\n"
    "        ui.button(\"保存\", c.next(36), Style().primary());\n"
    "    });\n"
    "}";

// 概览页 = "关于 + 快速开始" 两段，右边 "分类 + 实测" 两段。
// macOS 的分组语汇是：11 Semibold 的灰色小标题 + 下面一个 1px 细线、r10 的
// Group Box。之前这里是 2×2 的白卡片 + 描边 + 品牌色块，那是最典型的
// dashboard，macOS 的窗口里没有这种"卡片墙"。
// 页表在文件后半段才定义（要先有各页的 d_* 与自定义排版），这里只借它的总数，
// 用来在概览页显示"组件页 47 个" —— 写死过一次，加页之后就对不上了。
extern const int kPageCount;

void draw_overview(Ui& ui, App& a, const Rect& area) {
    (void)a;
    const float GAP  = 26.0f;
    const float HEAD = 18.0f;    // 分组标题行高（11 Semibold 灰字）
    const float GAPY = 24.0f;    // 两个分组之间

    const float lw = (area.w - GAP) * 0.62f;
    const auto& gs = group_stats();

    const std::vector<std::string> heads = {"指标", "值"};
    const std::vector<std::vector<std::string>> rows = {
        {"第三方依赖", "0"},
        {"空闲 CPU", "0.00%"},
        {"指针移动", "变化才画"},
        {"动画", "60 fps"},
        // 页数用 kPageCount 现算（写死过一次，加页之后就再也对不上了）；
        // 断言数来自 tools/regress.py 的期望值，改那边时记得一起改这里。
        {"组件页", std::to_string(kPageCount - 1) + " 个"},
        {"回归断言", "49 项"},
        {"文字", "系统字体"},
        {"几何", "自绘 AA"},
        {"DLL", "仅系统库"},
    };

    // 高度必须先算出总量再进滚动作用域 —— 两栏都是"从上往下排"的确定高度，
    // 所以第一遍只算数、第二遍才画。之前这页不滚动，窗口一矮，代码块就压在
    // 底部动作条上（18 行代码画到按钮行里去了）。
    const float code_h   = ui.code_block_height(kQuickstart, Style().font(11.5f));
    const float left_h   = (HEAD + 6.0f) + 128.0f + GAPY + (HEAD + 6.0f) + (code_h + 62.0f);
    const float right_h  = (HEAD + 6.0f) + (10.0f + 26.0f * float(gs.size() - 1) + 10.0f)
                         + GAPY + (HEAD + 6.0f) + (24.0f + 20.0f * float(rows.size()) + 16.0f);

    auto sc = ui.scroll(area, std::max(left_h, right_h) + 10.0f);
    (void)sc;
    // 滚动作用域里 `ui.row()` 返回的是**已减掉偏移**的屏幕坐标；这里用绝对坐标
    // 排版，所以要自己减 —— 否则滚轮一动，内容不动只被裁剪。
    const float off = ui.scroll_y();

    auto section = [&](const char* text, float x, float y, float w) {
        ui.muted_text(text, Rect(x, y, w, HEAD), Style().left().semi_bold().font(11.0f));
    };

    // ---------------- 左栏：关于 + 快速开始 ----------------
    {
        const float cx = area.x, cw = lw;
        float y = area.y - off;

        section("关于", cx, y, cw);
        y += HEAD + 6.0f;

        const Rect about(cx, y, cw, 128.0f);
        ui.panel(about);
        {
            Stack c = ui.vbox(Rect(about.x + 18.0f, about.y + 16.0f,
                                   about.w - 36.0f, about.h - 32.0f), 6.0f);
            {
                Stack r = c.hbox(24.0f);
                ui.subheading("aino", r.part(0, 2));
                ui.muted_text("v" AINO_VERSION, r.part(1, 2), Style().right().font(11.0f));
            }
            ui.muted_text("立即模式：每帧重建界面，控件在按下那一帧直接改变量。",
                          c.next(19.0f), Style().left().font(13.0f));
            ui.muted_text("空闲 0 帧：没有动画时不 repaint，CPU 占用 0.00%。",
                          c.next(19.0f), Style().left().font(13.0f));
            ui.muted_text("单文件：include 一个 .hpp，静态链接后无第三方依赖。",
                          c.next(19.0f), Style().left().font(13.0f));
        }
        y += about.h + GAPY;

        section("快速开始", cx, y, cw);
        y += HEAD + 6.0f;

        // 代码块 + 底部动作条，两段都在同一个 Group Box 里，中间一条内缩细线
        const Rect box(cx, y, cw, code_h + 62.0f);
        ui.panel(box);
        ui.code_block(kQuickstart, Rect(box.x + 8.0f, box.y + 8.0f, box.w - 16.0f, code_h),
                      Style().font(11.5f));
        ui.fill_rect(Rect(box.x + 1.0f, box.bottom() - 46.0f, box.w - 2.0f, 1.0f),
                     ui.theme().separator, 0.0f);
        {
            Stack r = ui.hbox(Rect(box.x + 14.0f, box.bottom() - 46.0f, box.w - 28.0f, 46.0f), 12.0f);
            const Rect bb = r.next(168.0f).center_y(24.0f);
            if (ui.button("在新窗口运行这段代码", bb, Style().primary()))
                open_quickstart_window();
            ui.muted_text("独立窗口 · 独立消息循环", r.rest(), Style().left().font(11.0f));
        }
    }

    // ---------------- 右栏：分类 + 实测 ----------------
    {
        const float cx = area.x + lw + GAP, cw = area.w - lw - GAP;
        float y = area.y - off;

        section("分类", cx, y, cw);
        y += HEAD + 6.0f;

        const float row_h = 26.0f;
        const Rect cat(cx, y, cw, 10.0f + row_h * float(gs.size() - 1) + 10.0f);
        ui.panel(cat);
        for (size_t i = 1; i < gs.size(); ++i) {
            const float ry = cat.y + 10.0f + row_h * float(i - 1);
            if (i > 1)
                ui.fill_rect(Rect(cat.x + 12.0f, ry, cat.w - 24.0f, 1.0f),
                             ui.theme().separator, 0.0f);
            Stack r = ui.hbox(Rect(cat.x + 14.0f, ry, cat.w - 28.0f, row_h));
            ui.label(gs[i].first, r.part(0, 2), Style().left().font(13.0f));
            ui.muted_text(std::to_string(gs[i].second) + " 页", r.part(1, 2),
                          Style().right().font(12.0f));
        }
        y += cat.h + GAPY;

        section("实测", cx, y, cw);
        y += HEAD + 6.0f;

        const Rect tbl(cx, y, cw, 24.0f + 20.0f * float(rows.size()) + 16.0f);
        ui.panel(tbl);
        ui.table(heads, rows, Rect(tbl.x + 10.0f, tbl.y + 8.0f, tbl.w - 20.0f, tbl.h - 16.0f),
                 Style().font(12.0f));
    }
}

// =============================================================================
//  基础
// =============================================================================

// 示例内容的栏宽。macOS 的表单/设置面板里，一个输入框、一条进度条、一组分段
// 控件都是**内容宽度**，不会被拉到 800px 满宽 —— 满宽的单控件是 Web 演示页的
// 做法（shadcn 的 demo 就是铺满的）。需要整宽的示例（表格、日历、图表、分割
// 面板、虚拟列表）直接用 box，不走这个函数。
constexpr float kDemoW = 400.0f;
Stack demo_col(Ui& ui, const Rect& box, float gap) {
    return ui.vbox(Rect(box.x, box.y, std::min(box.w, kDemoW), box.h), gap);
}

void d_btn_variants(Ui& ui, App& a, const Rect& box) {
    // 变体排成 3×2 的**网格**，不是一列。竖着排六个按钮会让整页看着像一张表单，
    // 而 kit 里这是六种并列的样式样本。
    const Style::Variant vs[] = {Style::Default, Style::Secondary, Style::Outline,
                                 Style::Ghost, Style::Destructive, Style::Link};
    const char* names[] = {"Default", "Secondary", "Outline", "Ghost", "Destructive", "Link"};
    const float bw = 132.0f, gap = 10.0f;
    for (int i = 0; i < 6; ++i) {
        Style st;
        st.variant = vs[i];
        const Rect r(box.x + float(i % 3) * (bw + gap), box.y + float(i / 3) * 32.0f, bw, 24.0f);
        if (ui.button(names[i], r, st)) { ++a.hits; a.last = names[i]; }
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
        ui.kbd("Shift", r.next_text("Shift", 16.0f, 11.0f, 500));
        ui.kbd("K", r.next_text("K", 18.0f, 11.0f, 500));
    }
    ui.separator(c.next(1.0f));
    ui.separator(c.next(1.0f));
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
    Stack c = demo_col(ui, box, 8.0f);
    // kit `Determinate/Medium`：100×10 胶囊 —— 8 高会显得比系统细一档
    ui.progress(0.24f, c.next(10.0f));
    ui.progress(0.58f, c.next(10.0f));
    ui.progress(0.92f, c.next(10.0f));
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
    Stack c = demo_col(ui, box, 10.0f);
    ui.input(&a.email, c.next(24.0f), Style().hint("you@example.com"));
    ui.input(&a.keyword, c.next(24.0f), Style().hint("搜索…"));
}

void d_textarea(Ui& ui, App& a, const Rect& box) {
    // 多行框给 520：macOS 的文本视图也不会横着铺满整个窗口
    ui.textarea(&a.note, Rect(box.x, box.y, std::min(box.w, 520.0f), box.h),
                Style().font(13.0f));
}

void d_input_group(Ui& ui, App& a, const Rect& box) {
    Stack c = demo_col(ui, box, 10.0f);
    ui.input_group(&a.amount, "¥", "元", c.next(24.0f));
    ui.input_group(&a.phone, "+86", "", c.next(24.0f));
}

void d_otp(Ui& ui, App& a, const Rect& box) {
    // 只占 24 高：验证码每格就是一个 Text Field，不是 48 高的大方块
    ui.input_otp(&a.otp, 6, Rect(box.x, box.y, box.w, 24.0f));
}

void d_checkbox(Ui& ui, App& a, const Rect& box) {
    Stack c = demo_col(ui, box, 10.0f);
    ui.checkbox(&a.remember, "记住这台设备", c.next(22.0f));
    ui.checkbox(&a.autosave, "自动保存", c.next(22.0f));
    Style dis;
    dis.disabled = true;
    bool tmp = true;
    ui.checkbox(&tmp, "不可用", c.next(22.0f), dis);
}

void d_switch(Ui& ui, App& a, const Rect& box) {
    Stack c = demo_col(ui, box, 10.0f);
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
}

void d_radio(Ui& ui, App& a, const Rect& box) {
    ui.radio_group(kPlans, &a.radio, box, Style().font(13.0f));
}

void d_slider(Ui& ui, App& a, const Rect& box) {
    Stack c = demo_col(ui, box, 12.0f);
    ui.slider(&a.vol, 0.0f, 1.0f, c.next(26.0f));
    float mid = 0.5f, low = 0.0f, hi = 1.0f;
    ui.slider(&mid, 0.0f, 1.0f, c.next(26.0f));
    ui.slider(&low, 0.0f, 1.0f, c.next(26.0f));
    ui.slider(&hi, 0.0f, 1.0f, c.next(26.0f), Style().off());
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
    Stack c = demo_col(ui, box, 10.0f);
    const Rect sr = c.next(24.0f);
    demo_force_open(ui, "select", sr);        // AINO_OPEN=select 时强制展开（截图用）
    ui.select(kPlans, &a.plan, sr, Style().hint("选择套餐"));
}

void d_combobox(Ui& ui, App& a, const Rect& box) {
    Stack c = demo_col(ui, box, 10.0f);
    const Rect cr = c.next(24.0f);
    demo_force_open(ui, "combobox", cr);      // AINO_OPEN=combobox 时强制展开
    ui.combobox(kLangs, &a.combo, cr);
}

void d_toggle(Ui& ui, App& a, const Rect& box) {
    Stack c = demo_col(ui, box, 12.0f);
    {
        Stack r = c.hbox(24.0f, 8.0f);
        ui.toggle_button("粗体", &a.bold, r.next(84.0f));
        ui.toggle_button("斜体", &a.italic, r.next(84.0f));
    }
    ui.toggle_group({"日", "周", "月"}, &a.tg, c.next(24.0f));
}

void d_button_group(Ui& ui, App& a, const Rect& box) {
    Stack c = demo_col(ui, box, 12.0f);
    ui.button_group({"复制", "粘贴", "剪切"}, &a.bg_click, c.next(24.0f));
    ui.toggle_group_multi({"B", "I", "U"}, &a.tgm, c.next(24.0f));
}

void d_stepper_rating(Ui& ui, App& a, const Rect& box) {
    Stack c = demo_col(ui, box, 14.0f);
    // 官方步进器是"数值框 + 右边缘竖条"，整个控件只有 ~60-140 宽。
    // 拉满整列会变成一条巨大的空输入框，看着像没画完。
    const Rect srow = c.next(24.0f);
    ui.stepper(&a.step, 1.0f, 0.0f, 99.0f, Rect(srow.x, srow.y, 96.0f, srow.h));
    ui.rating(&a.stars, 5, c.next(24.0f));
    char buf[64];
    std::snprintf(buf, sizeof buf, "step = %.0f · rating = %d", double(a.step), a.stars);
}

void d_field(Ui& ui, App& a, const Rect& box) {
    Stack c = demo_col(ui, box, 14.0f);
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
    Stack c = demo_col(ui, box, 14.0f);
    ui.tabs(kViews, &a.tab, c.next(24.0f));
    ui.segmented(kPlans, &a.seg, c.next(24.0f));
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
}

void d_accordion(Ui& ui, App& a, const Rect& box) {
    Stack c = ui.vbox(box, 4.0f);
    ui.accordion("渲染管线", &a.acc1, c.next(24.0f));
    if (a.acc1) {
    }
    ui.accordion("输入分发", &a.acc2, c.next(24.0f));
    if (a.acc2) {
    }
}

void d_alert(Ui& ui, App& a, const Rect& box) {
    Stack c = demo_col(ui, box, 10.0f);
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
}

// 截图工具钩子：AINO_OPEN=dialog|popover|drawer|toast 让浮层默认打开。
void demo_set_open(const char* kind, bool* v) {
    const char* want = std::getenv("AINO_OPEN");
    if (!want || std::string(want) != kind) return;
    // 只置一次。这个函数是在**绘制示例时每帧调用**的，无脑置 true 会让用户点
    // 关闭之后被下一帧又弹回来（"点了没反应"）—— 钩子只负责"初始态"。
    static std::vector<std::string> done;
    if (std::find(done.begin(), done.end(), std::string(kind)) != done.end()) return;
    done.push_back(kind);
    *v = true;
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
}

void d_popover(Ui& ui, App& a, const Rect& box) {
    demo_set_open("popover", &a.pop);
    ui.popover("排序", &a.pop, Rect(box.x, box.y, 120.0f, 24.0f), Vec2(190.0f, 104.0f),
               [&](const Rect& c) {
                   ui.item("按名称", "", "", Rect(c.x, c.y, c.w, 24.0f));
                   ui.item("按时间", "", "", Rect(c.x, c.y + 26.0f, c.w, 24.0f));
                   ui.item("按大小", "", "", Rect(c.x, c.y + 52.0f, c.w, 24.0f));
               });
}

void d_hover_card(Ui& ui, App& a, const Rect& box) {
    const Rect anchor(box.x, box.y, box.w, 52.0f);
    ui.item("张伟", "zhangwei@example.com", "管理员", anchor);
    ui.hover_card(anchor, Vec2(220.0f, 84.0f), [&](const Rect& c) {
        ui.label("张伟", Rect(c.x, c.y, c.w, 20.0f), Style().left().semi_bold());
    });
    (void)a;
}

void d_dialog(Ui& ui, App& a, const Rect& box) {
    demo_set_open("dialog", &a.dlg);
    if (ui.button("打开对话框", Rect(box.x, box.y, 132.0f, 24.0f), Style().primary()))
        a.dlg = true;
}

void o_dialog(Ui& ui, App& a) {
    const Rect d = Rect::centered(ui.width() * 0.5f, ui.height() * 0.5f, 380.0f, 178.0f);
    const std::string body = "删除后无法恢复，这份数据没有其他备份。";
    if (ui.dialog("确认删除", body, &a.dlg, d)) {
        // macOS 告警的按钮是**通宽 28 高的行，纵向排列**，最底一行是取消胶囊
        // （kit `Alert/3 Options + Cancel`）。右下角两颗小按钮是网页模态的排法。
        // kit `Alert/3 Options + Cancel`：上面是若干条 228×28 的**无面通宽行**，
        // 最下面单独一颗 28 高的胶囊取消键。
        const float pad = 20.0f, bh = 28.0f;
        const Rect cr(d.x + pad, d.bottom() - 16.0f - bh, d.w - pad * 2.0f, bh);
        const Rect orr(d.x + pad, cr.y - 16.0f - bh, d.w - pad * 2.0f, bh);
        if (ui.button("删除这个文件", orr,
                      Style().ghost().font(13.0f).normal_weight().color(ui.theme().destructive)))
            a.dlg = false;
        if (ui.button("取消", cr, Style().secondary().corner(bh * 0.5f).font(13.0f).medium()))
            a.dlg = false;
    }
    ui.alert_dialog("删除这个文件？", "删除后无法恢复，这份数据没有其他备份。",
                    "删除", "取消", &a.confirm,
                    Rect(ui.width() * 0.5f - 190.0f, ui.height() * 0.5f - 89.0f, 380.0f, 178.0f),
                    Style().destructive());
}

void d_alert_dialog(Ui& ui, App& a, const Rect& box) {
    if (ui.button("删除…", Rect(box.x, box.y, 120.0f, 24.0f), Style().destructive()))
        a.confirm = true;
}

void d_drawer(Ui& ui, App& a, const Rect& box) {
    demo_set_open("drawer", &a.drawer);
    if (ui.button("打开抽屉", Rect(box.x, box.y, 120.0f, 24.0f), Style().outline()))
        a.drawer = true;
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
}

void d_tooltip(Ui& ui, App& a, const Rect& box) {
    const aino::Theme& th = ui.theme();
    const Rect t(box.x, box.y, 84.0f, 24.0f);
    ui.button("删除", t, Style().outline());
    ui.tooltip(t, "删除选中的文件");
    const Rect b2(t.right() + 10.0f, t.y, 28.0f, 24.0f);
    ui.button("?", b2, Style().secondary());
    ui.tooltip(b2, "更多操作");

    // 悬停态截图抓不到，这里按系统的 18 高 / r5 / 11 Medium 在"?"下面补一个
    // 静态气泡 —— 两个锚点各自的提示文字不同，不会看成重影。
    const std::string tx = "更多操作";
    const float fs = 11.0f;
    const float w  = ui.text_width(tx, fs, 500) + 12.0f;
    const Rect bub(b2.cx() - w * 0.5f, b2.bottom() + 6.0f, w, 18.0f);
    ui.drop_shadow(bub, 5.0f, 10.0f, aino::Color(0, 0, 0, 70), 2.0f);
    ui.fill_rect(bub, th.tooltip_bg, 5.0f);
    if (th.tooltip_stroke.a > 0) ui.stroke_rect(bub, th.tooltip_stroke, 5.0f, 0.6f);
    ui.draw_text(tx, bub, th.popover_foreground, Style().center().font(fs).medium());
    (void)a;
}

void d_context_menu(Ui& ui, App& a, const Rect& box) {
    ui.panel(box);
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
        Stack r = c.hbox(24.0f, 8.0f);      // 控件高 24 是 Medium 档，36 是 Web 的默认
        if (ui.button("等分 1/3", r.part(0, 3), Style().outline())) ++a.hits;
        if (ui.button("2/3", r.part(1, 3), Style().outline())) ++a.hits;
        if (ui.button("3/3", r.part(2, 3), Style().outline())) ++a.hits;
    }
    {
        Stack r = c.hbox(24.0f, 10.0f);
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
    }
}

void d_scroll(Ui& ui, App& a, const Rect& box) {
    const aino::Theme& th = ui.theme();
    Stack c = ui.vbox(box, 8.0f);
    {
        Stack r = c.hbox(24.0f, 8.0f);
        if (ui.button("回顶", cell(r, 62.0f, 24.0f), Style().secondary())) a.scroll_cmd = 1;
        if (ui.button("到底", cell(r, 62.0f, 24.0f), Style().secondary())) a.scroll_cmd = 2;
        r.fill();
    }
    const Rect vp = c.fill();
    // 滚动区要有自己的"面"：内容区底色 + 一圈细线。没有面的话行会直接贴在
    // 页面背景上，看起来是一堆浮空的灰条。
    ui.fill_rect(vp, th.control_bg, 8.0f);
    {
        const float ih = 24.0f;
        const int   n = 20;
        auto sc = ui.scroll(vp.inset(1.0f), ih * float(n));
        if (a.scroll_cmd == 1) { sc.to(0.0f); a.scroll_cmd = 0; }
        if (a.scroll_cmd == 2) { sc.to(1e6f); a.scroll_cmd = 0; }
        for (int i = 0; i < n; ++i) {
            const Rect r = ui.row(ih);
            if (i) ui.fill_rect(Rect(r.x + 8.0f, r.y, r.w - 16.0f, 1.0f), th.separator, 0.0f);
            char buf[64];
            std::snprintf(buf, sizeof buf, "列表项 %02d", i + 1);
            ui.label(buf, Rect(r.x + 12.0f, r.y, r.w - 24.0f, r.h), Style().left());
        }
    }
    ui.stroke_rect(vp, th.separator, 8.0f, 1.0f);
}

void d_virtual(Ui& ui, App& a, const Rect& box) {
    const aino::Theme& th = ui.theme();
    Stack c = ui.vbox(box, 8.0f);
    {
        Stack r = c.hbox(24.0f, 8.0f);
        if (ui.button("跳到底部", cell(r, 92.0f, 24.0f), Style().secondary())) a.jump = 1;
        r.fill();
    }
    const Rect vp = c.fill();
    ui.fill_rect(vp, th.control_bg, 8.0f);
    {
        const float ih = 24.0f;
        const int   n = 100000;
        if (a.jump == 1) { ui.scroll_to_index(vp.inset(1.0f), n - 1, ih, n); a.jump = 0; }
        ui.scroll(vp.inset(1.0f), n, ih, [&](int i, const Rect& r) {
            if (i & 1) ui.fill_rect(r, th.fill5, 0.0f);
            if (i) ui.fill_rect(Rect(r.x + 8.0f, r.y, r.w - 16.0f, 1.0f), th.separator, 0.0f);
            char buf[48];
            std::snprintf(buf, sizeof buf, "第 %d 行", i + 1);
            ui.label(buf, Rect(r.x + 12.0f, r.y, r.w - 24.0f, r.h), Style().left().font(12.0f));
        });
    }
    ui.stroke_rect(vp, th.separator, 8.0f, 1.0f);
}

void d_split(Ui& ui, App& a, const Rect& box) {
    Rect l, r;
    ui.split_pane(&a.split, box, &l, &r, true, 0.2f);
    ui.panel(l, Style().background(ui.theme().muted).corner(8.0f));
    ui.panel(r, Style().background(ui.theme().muted).corner(8.0f));
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
    if (ui.button(a.motion ? "复位" : "播放", c.next(24.0f), Style().outline()))
        a.motion = !a.motion;
    char buf[48];
    std::snprintf(buf, sizeof buf, "t = %.3f", double(k));
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
     R"cpp(ui.progress(0.24f, c.next(10.0f));
ui.progress(0.92f, c.next(10.0f));
ui.skeleton(r.next(120.0f));
ui.spinner(r.next(24.0f));)cpp",
     96.0f, d_progress},
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
     24.0f, d_otp},
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
     137.0f, d_slider},
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
ui.toggle_group({"日", "周", "月"}, &a.tg, c.next(24.0f));)cpp",
     118.0f, d_toggle},
};

const Demo kButtonGroup[] = {
    {"按钮组", "同一条轨道里分段；多选用位掩码",
     R"cpp(ui.button_group({"复制", "粘贴", "剪切"}, &a.bg_click, c.next(24.0f));
ui.toggle_group_multi({"B", "I", "U"}, &a.tgm, c.next(24.0f));)cpp",
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
    {"标签栏与分段控件", "选中项浮一层淡底 vs 凹槽里浮起一块",
     R"cpp(ui.tabs({"概览", "分析", "设置"}, &a.tab, c.next(24.0f));
ui.segmented({"免费版", "专业版", "企业版"}, &a.seg, c.next(24.0f));)cpp",
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
    {"空状态", "居中排版：标题 13 Semibold，描述 11 次要色",
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
     60.0f, d_tooltip},
};

const Demo kContextMenu[] = {
    {"右键菜单", "在 area 内右键弹出；位置跟随鼠标",
     R"cpp(ui.context_menu(area, {"标记已读", "归档", "删除"}, &a.ctx_pick);)cpp",
     92.0f, d_context_menu},
};

const Demo kLayout[] = {
    {"Stack 布局", "只给高度，位置由布局器推导；part 均分、next_text 按文字宽量",
     R"cpp(Stack r = c.hbox(24.0f, 8.0f);
ui.button("等分 1/3", r.part(0, 3), Style().outline());
ui.button("2/3", r.part(1, 3), Style().outline());

Stack b = c.hbox(24.0f, 10.0f);
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

// =============================================================================
//  规范页：色彩 / 材质 / 字阶
//
//  这三页不是"控件示例"，是设计系统的规范板 —— 所以走 Page::custom 整页自绘，
//  不套示例卡片那套表头 + 代码块（那等于在一个框里再画一个框）。
// =============================================================================

// 一张色卡 = 上半「浅色外观」（白底）、下半「深色外观」（近黑底），同一个圆骑在
// 中线上。Figma 的 macOS kit 就是这么摆的：一眼能看出同一个语义色在两种外观下
// 的真实取值，而不是给两个色块让人自己去对。
struct SwatchDef { const char* name; Color light; Color dark; };

// sRGB 系统色（浅色外观 / 深色外观）。值和 aino.hpp 里 Theme::light/dark 的
// accents 是同一套，改一处必须改另一处 —— 否则规范页会跟控件实际用色对不上。
static const SwatchDef kAccentSwatches[] = {
    {"Red",    Color::hex(0xFF383C), Color::hex(0xFF4245)},
    {"Orange", Color::hex(0xFF8D28), Color::hex(0xFF9230)},
    {"Yellow", Color::hex(0xFFCC00), Color::hex(0xFFD600)},
    {"Green",  Color::hex(0x34C759), Color::hex(0x30D158)},
    {"Mint",   Color::hex(0x00C8B3), Color::hex(0x00DAC3)},
    {"Teal",   Color::hex(0x00C3D0), Color::hex(0x00D2E0)},
    {"Cyan",   Color::hex(0x00C0E8), Color::hex(0x3CD3FE)},
    {"Blue",   Color::hex(0x0088FF), Color::hex(0x0091FF)},
    {"Indigo", Color::hex(0x6155F5), Color::hex(0x6D7CFF)},
    {"Purple", Color::hex(0xCB30E0), Color::hex(0xDB34F2)},
    {"Pink",   Color::hex(0xFF2D55), Color::hex(0xFF375F)},
    {"Brown",  Color::hex(0xAC7F5E), Color::hex(0xB78A66)},
    {"Gray",   Color::hex(0x8E8E93), Color::hex(0x98989F)},
};

// Vibrant：同一个语义色在白底 / 黑底上的变体。白底上要压深（否则糊成一片），
// 黑底上要提亮（否则像块脏斑）—— 这就是 vibrancy 的物理来源。
static const SwatchDef kVibrantSwatches[] = {
    {"Red",    Color::hex(0xD50F12), Color::hex(0xFF7A7A)},
    {"Orange", Color::hex(0xD56605), Color::hex(0xFFD166)},
    {"Yellow", Color::hex(0xD6A300), Color::hex(0xFFFF46)},
    {"Green",  Color::hex(0x07A02E), Color::hex(0x6DFF95)},
    {"Mint",   Color::hex(0x009E8A), Color::hex(0x6BFFE8)},
    {"Teal",   Color::hex(0x0094A0), Color::hex(0x5EFFFF)},
    {"Cyan",   Color::hex(0x008CB0), Color::hex(0x78FFFF)},
    {"Blue",   Color::hex(0x005AD2), Color::hex(0x3AC9FF)},
    {"Indigo", Color::hex(0x3E32C8), Color::hex(0xA193FF)},
    {"Purple", Color::hex(0x9A0EAC), Color::hex(0xFF76FF)},
    {"Pink",   Color::hex(0xD9072F), Color::hex(0xFF7098)},
    {"Brown",  Color::hex(0x825738), Color::hex(0xF1C5A1)},
    {"Gray",   Color::hex(0x6E6E73), Color::hex(0xC7C7CC)},
};

// Fills / Text 的层级。alpha 用 0..255；浅色外观叠黑、深色外观叠白。
static const unsigned char kLadderAlpha[] = {26, 20, 13, 8, 5};        // Fills：10/8/5/3/2%
static const unsigned char kLabelAlpha[]  = {217, 128, 64, 26, 13};    // Text：85/50/25/10/5%
static const char* kLadderName[] = {"Primary", "Secondary", "Tertiary", "Quaternary", "Quinary"};
static const char* kTextName[]   = {"Primary", "Secondary", "Tertiary", "Quaternary", "Quinary",
                                    "Inverted"};

static void spec_tile_split(Ui& ui, const Rect& r) {
    const float half = std::floor(r.h * 0.5f);
    ui.fill_rect(Rect(r.x, r.y, r.w, half), Color(255, 255, 255), 0.0f);
    ui.fill_rect(Rect(r.x, r.y + half, r.w, r.h - half), Color(0x1C, 0x1C, 0x1E), 0.0f);
}

// 圆骑中线：上半圆用浅色外观的取值，下半圆用深色外观的取值
static void spec_circle(Ui& ui, const Rect& r, Color up, Color dn, float ratio) {
    spec_tile_split(ui, r);
    const float half = std::floor(r.h * 0.5f);
    const float d = r.w * ratio;
    ui.push_clip(Rect(r.x, r.y, r.w, half));
    ui.fill_circle(r.cx(), r.y + half, d * 0.5f, up);
    ui.pop_clip();
    ui.push_clip(Rect(r.x, r.y + half, r.w, r.h - half));
    ui.fill_circle(r.cx(), r.y + half, d * 0.5f, dn);
    ui.pop_clip();
    ui.stroke_rect(r, Color(0, 0, 0, 20), 0.0f, 1.0f);
}

// 上下两个完整的字形（不是一个字被切成两半 —— Figma 的 Text 卡就是这么摆的）
static void spec_glyph(Ui& ui, const Rect& r, Color up, Color dn, const char* g) {
    spec_tile_split(ui, r);
    const float half = std::floor(r.h * 0.5f);
    Style s;
    s.font_size = half * 0.60f;
    s.font_weight = 600;
    s.align = aino::Align::Center;
    s.valign = aino::VAlign::Middle;
    ui.draw_text(g, Rect(r.x, r.y, r.w, half), up, s);
    ui.draw_text(g, Rect(r.x, r.y + half, r.w, r.h - half), dn, s);
    ui.stroke_rect(r, Color(0, 0, 0, 20), 0.0f, 1.0f);
}

void page_colors(Ui& ui, App& a, const Rect& area) {
    (void)a;
    const float PAD = 16.0f, GAP = 10.0f, SEC = 15.0f, NAME = 12.0f, SEG = 14.0f;
    const float W = std::min(area.w, 1000.0f);
    const float inner = W - PAD * 2.0f;

    const float c13 = (inner - GAP * 12.0f) / 13.0f;
    const float halfw = (inner - 22.0f) * 0.5f;
    const float c5 = (halfw - GAP * 4.0f) / 5.0f;
    const float c6 = (halfw - GAP * 5.0f) / 6.0f;
    const float h5 = c5, h6 = c6;

    const float row1 = SEC + 4.0f + NAME + 4.0f + c13 + SEG;      // 13 列
    const float row2 = SEC + 4.0f + NAME + 4.0f + std::max(h5, h6) + SEG;
    const float box_h = PAD * 2.0f + row1 * 2.0f + row2 * 2.0f;
    const float sw_h  = 86.0f;
    const float total = box_h + 12.0f + sw_h + 4.0f;

    auto sc = ui.scroll(area, total);
    (void)sc;
    const float off = ui.scroll_y();

    const Rect box(area.x, area.y - off, W, box_h);
    ui.panel(box);

    auto heading = [&](const char* t, float x, float y, float w) {
        ui.muted_text(t, Rect(x, y, w, SEC), Style().left().semi_bold().font(11.0f));
    };
    auto names = [&](const SwatchDef* arr, int n, float x0, float y, float cw) {
        for (int i = 0; i < n; ++i)
            ui.muted_text(arr[i].name, Rect(x0 + float(i) * (cw + GAP), y, cw, NAME),
                          Style().center().font(9.5f));
    };

    float y = box.y + PAD;

    // ---- Accents ----
    heading("Accents", box.x + PAD, y, 240.0f);
    names(kAccentSwatches, 13, box.x + PAD, y + SEC + 4.0f, c13);
    for (int i = 0; i < 13; ++i) {
        const Rect r(box.x + PAD + float(i) * (c13 + GAP),
                     y + SEC + 4.0f + NAME + 4.0f, c13, c13);
        spec_circle(ui, r, kAccentSwatches[i].light, kAccentSwatches[i].dark, 0.62f);
    }
    y += row1;

    // ---- Accents - Vibrant ----
    heading("Accents - Vibrant", box.x + PAD, y, 240.0f);
    names(kVibrantSwatches, 13, box.x + PAD, y + SEC + 4.0f, c13);
    for (int i = 0; i < 13; ++i) {
        const Rect r(box.x + PAD + float(i) * (c13 + GAP),
                     y + SEC + 4.0f + NAME + 4.0f, c13, c13);
        spec_circle(ui, r, kVibrantSwatches[i].light, kVibrantSwatches[i].dark, 0.62f);
    }
    y += row1;

    // ---- Fills | Text ----
    const float lx = box.x + PAD, rx = box.x + PAD + halfw + 22.0f;
    heading("Fills", lx, y, halfw);
    for (int i = 0; i < 5; ++i) {
        ui.muted_text(kLadderName[i], Rect(lx + float(i) * (c5 + GAP), y + SEC + 4.0f, c5, NAME),
                      Style().center().font(9.5f));
        const Rect r(lx + float(i) * (c5 + GAP), y + SEC + 4.0f + NAME + 4.0f, c5, h5);
        spec_circle(ui, r, Color(0, 0, 0, kLadderAlpha[i]), Color(255, 255, 255, kLadderAlpha[i]),
                    0.66f);
    }
    heading("Text", rx, y, halfw);
    for (int i = 0; i < 6; ++i) {
        ui.muted_text(kTextName[i], Rect(rx + float(i) * (c6 + GAP), y + SEC + 4.0f, c6, NAME),
                      Style().center().font(9.5f));
        const Rect r(rx + float(i) * (c6 + GAP), y + SEC + 4.0f + NAME + 4.0f, c6, h6);
        const Color up = (i == 5) ? Color(255, 255, 255) : Color(0, 0, 0, kLabelAlpha[i]);
        const Color dn = (i == 5) ? Color(0, 0, 0) : Color(255, 255, 255, kLabelAlpha[i]);
        spec_glyph(ui, r, up, dn, "A");
    }
    y += row2;

    // ---- Fills - Vibrant | Text - Vibrant ----
    heading("Fills - Vibrant", lx, y, halfw);
    for (int i = 0; i < 5; ++i) {
        ui.muted_text(kLadderName[i], Rect(lx + float(i) * (c5 + GAP), y + SEC + 4.0f, c5, NAME),
                      Style().center().font(9.5f));
        const Rect r(lx + float(i) * (c5 + GAP), y + SEC + 4.0f + NAME + 4.0f, c5, h5);
        // vibrant 的填充更"透"：同一层级在白底上更深、黑底上更亮
        const Color up(0, 0, 0, (unsigned char)std::min(255, int(kLadderAlpha[i]) + 6));
        const Color dn(255, 255, 255, (unsigned char)std::min(255, int(kLadderAlpha[i]) + 10));
        spec_circle(ui, r, up, dn, 0.66f);
    }
    heading("Text - Vibrant", rx, y, halfw);
    for (int i = 0; i < 5; ++i) {
        ui.muted_text(kLadderName[i], Rect(rx + float(i) * (c6 + GAP), y + SEC + 4.0f, c6, NAME),
                      Style().center().font(9.5f));
        const Rect r(rx + float(i) * (c6 + GAP), y + SEC + 4.0f + NAME + 4.0f, c6, h6);
        spec_glyph(ui, r, Color(0, 0, 0, kLabelAlpha[i]), Color(255, 255, 255, kLabelAlpha[i]), "A");
    }

    // ---- 可拖用的 Swatch 组件 ----
    const float sy = box.bottom() + 12.0f;
    ui.muted_text("Swatch", Rect(area.x, sy, 240.0f, SEC), Style().left().semi_bold().font(11.0f));
    const float sw = 56.0f;
    const float sy2 = sy + SEC + 6.0f;
    for (int i = 0; i < 4; ++i) {
        const Rect r(area.x + float(i) * (sw + 14.0f), sy2, sw, sw);
        ui.panel(r, Style().corner(10.0f));
        const float d = sw * 0.46f;
        const SwatchDef& s0 = (i == 0 || i == 1) ? kAccentSwatches[0] : kVibrantSwatches[0];
        const Rect inner(r.x + (sw - d) * 0.5f, r.y + (sw - d) * 0.5f, d, d);
        if (i == 0 || i == 2) spec_circle(ui, inner, s0.light, s0.dark, 0.66f);
        else                  spec_glyph(ui, inner, s0.light, s0.dark, "A");
    }
}

// =============================================================================
//  规范页：字阶
// =============================================================================
// macOS 的 11 个文本样式。字号是 pt（= aino 的逻辑像素），这里按 TYPE_SCALE
// 缩小展示 —— 34pt 的 LargeTitle 摆成全尺寸，一屏放不下四个矩阵，也失去了
// "并排比较"的意义。缩小比例写在块标题里，不藏着。
struct TypeStyleDef { const char* name; float size; int weight; };

static const TypeStyleDef kTypeStyles[] = {
    {"LargeTitle",  34.0f, 400},
    {"Title1",      28.0f, 400},
    {"Title2",      22.0f, 400},
    {"Title3",      20.0f, 400},
    {"Headline",    17.0f, 600},
    {"Body",        17.0f, 400},
    {"Callout",     16.0f, 400},
    {"Subheadline", 15.0f, 400},
    {"Footnote",    13.0f, 400},
    {"Caption1",    12.0f, 400},
    {"Caption2",    11.0f, 400},
};

static const char* kTypeCols[3] = {"Label Color", "Secondary Label Color", "Tertiary Label Color"};

// tone: 0 = Light Opaque / 1 = Dark Opaque / 2 = Light Vibrant / 3 = Dark Vibrant
static void type_block(Ui& ui, const Rect& box, const char* title, int tone) {
    const float SCALE = 0.72f;
    const bool dark = (tone == 1 || tone == 3);
    const bool vib  = (tone >= 2);

    ui.muted_text(title, Rect(box.x, box.y, box.w, 16.0f),
                  Style().left().semi_bold().font(11.0f));

    const Rect panel(box.x, box.y + 20.0f, box.w, box.h - 20.0f);

    // 面：Opaque 是纯色，Vibrant 是一层带色的材质（用渐变近似 —— 真的 vibrancy
    // 要把背后内容取样再混色，那是 window server 的事）
    if (vib) {
        if (dark) {
            ui.fill_rect_grad(panel, Color::hex(0x1B2440), Color::hex(0x0C0F16), 10.0f);
        } else {
            ui.fill_rect_grad(panel, Color::hex(0xF3E9E0), Color::hex(0xE6E6F2), 10.0f);
        }
    } else {
        ui.fill_rect(panel, dark ? Color::hex(0x1C1C1E) : Color::hex(0xFFFFFF), 10.0f);
    }
    ui.stroke_rect(panel, dark ? Color(255, 255, 255, 24) : Color(0, 0, 0, 20), 10.0f, 1.0f);

    const unsigned char alpha[3] = {217, 128, 64};
    const float colw = (panel.w - 24.0f) * (1.0f / 3.0f);

    for (int c = 0; c < 3; ++c) {
        const float cx = panel.x + 12.0f + colw * float(c);
        if (c > 0)
            ui.fill_rect(Rect(cx - 8.0f, panel.y + 6.0f, 1.0f, panel.h - 12.0f),
                         dark ? Color(255, 255, 255, 24) : Color(0, 0, 0, 20), 0.0f);
        ui.draw_text(kTypeCols[c], Rect(cx, panel.y + 8.0f, colw - 8.0f, 12.0f),
                     dark ? Color(255, 255, 255, 128) : Color(0, 0, 0, 128),
                     Style().left().font(8.5f));

        const Color ink = dark ? Color(255, 255, 255, alpha[c]) : Color(0, 0, 0, alpha[c]);
        float y = panel.y + 26.0f;
        for (const TypeStyleDef& ts : kTypeStyles) {
            const float fs = ts.size * SCALE;
            for (int w = 0; w < 2; ++w) {                 // Regular / Semibold
                Style s;
                s.font_size = fs;
                s.font_weight = w == 0 ? ts.weight : (ts.weight >= 600 ? 700 : 600);
                s.align = aino::Align::Left;
                s.valign = aino::VAlign::Top;
                ui.draw_text(ts.name, Rect(cx, y, colw - 10.0f, fs * 1.22f), ink, s);
                y += std::floor(fs * 1.20f);
            }
            y += 5.0f;
        }
    }
}

void page_typography(Ui& ui, App& a, const Rect& area) {
    (void)a;
    const float GAP = 22.0f;
    const float bw = (area.w - GAP) * 0.5f;

    // 块高按样式表算：每行两个字重 + 行间 5
    float block_h = 20.0f + 26.0f;          // 块标题 + 列头
    for (const TypeStyleDef& ts : kTypeStyles)
        block_h += std::floor(ts.size * 0.72f * 1.20f) * 2.0f + 5.0f;
    block_h += 14.0f;

    const float total = block_h * 2.0f + GAP + 8.0f;
    auto sc = ui.scroll(area, total);
    (void)sc;
    const float off = ui.scroll_y();

    const char* titles[4] = {
        "Text Styles - Left Aligned - Light Opaque",
        "Text Styles - Left Aligned - Dark Opaque",
        "Text Styles - Left Aligned - Light Vibrant",
        "Text Styles - Left Aligned - Dark Vibrant",
    };
    for (int i = 0; i < 4; ++i) {
        const float x = area.x + (i % 2) * (bw + GAP);
        const float y = area.y - off + (i / 2) * (block_h + GAP);
        type_block(ui, Rect(x, y, bw, block_h), titles[i], i);
    }
}

// =============================================================================
//  规范页：材质
// =============================================================================
// macOS 的传统材质（NSVisualEffectView）：一层半透明模糊面压在前景内容上，
// "厚度"决定背后能透出多少。这里刻意不用 Liquid Glass 那套玻璃胶囊 —— 那是
// macOS 26 才有的语言，跟这个库其余部分（纯色面 + hairline 分层）不是一套东西。
//
// aino 拿不到窗口背后的像素（那是 window server 的事），所以模糊用"同一张壁纸
// + 不同 alpha 的半透明面"表现：底下是连续的一张壁纸，每块材质按自己的厚度压
// 上去，透出的颜色深浅就是厚度差。

struct BlurMatDef { const char* name; float alpha; };

// Blur 厚度：Ultra Thick 几乎不透，Ultra Thin 只剩一层雾
static const BlurMatDef kBlurMats[] = {
    {"Ultra Thick", 0.92f},
    {"Thick",       0.78f},
    {"Medium",      0.62f},
    {"Thin",        0.46f},
    {"Ultra Thin",  0.30f},
};

// 语义材质：同一个模糊面，不同的使用场景。HUD 永远是暗的（它是浮在内容上的
// 黑色面板），其余跟随外观。
struct SemanticMatDef { const char* name; float alpha; bool always_dark; };
static const SemanticMatDef kSemanticMats[] = {
    {"Sidebar",  0.55f, false},
    {"Menu",     0.80f, false},
    {"Popover",  0.82f, false},
    {"HUD",      0.72f, true},
    {"Window",   0.90f, false},
    {"Content",  0.96f, false},
};

// 桌面壁纸：一张渐变 + 三个大色斑。材质透出的就是它 —— 没有彩色底，
// 半透明面在纯色背景上等于没画。
static void desktop_wallpaper(Ui& ui, const Rect& r, float radius) {
    // 壁纸要**中等明度**：太深的话 90% 的白材质压上去就是一块白板，五档厚度
    // 全都长一个样；太浅又看不出"透"。取中间调，白材质的每一档都能拉开。
    // 色斑只用**低饱和、大半径**的一两笔：背景太花的话，五块材质透出的差异
    // 分不清是"厚度"还是"背后那块正好是个色斑"。
    ui.fill_rect_grad(r, Color::hex(0x8FB3CE), Color::hex(0xD9A38F), radius);
    ui.push_clip(r);
    ui.fill_circle(r.x + r.w * 0.50f, r.y + r.h * 0.50f, r.h * 1.05f, Color(61, 142, 134, 70));
    ui.fill_circle(r.x + r.w * 0.80f, r.y + r.h * 0.20f, r.h * 0.55f, Color(233, 196, 106, 60));
    ui.pop_clip();
}

// 一块材质面：纯半透明 + 一条 hairline。传统 vibrancy 没有高光边、没有玻璃
// 折射，越接近系统控件越好 —— 有金属高光就成了另一个人做的东西。
static void material_face(Ui& ui, const Rect& r, float alpha, bool dark, float radius) {
    const unsigned char a = (unsigned char)std::lround(std::min(1.0f, std::max(0.0f, alpha)) * 255.0f);
    ui.fill_rect(r, dark ? Color(22, 22, 24, a) : Color(255, 255, 255, a), radius);
    ui.stroke_rect(r, dark ? Color(255, 255, 255, 22) : Color(0, 0, 0, 16), radius, 1.0f);
}

void page_materials(Ui& ui, App& a, const Rect& area) {
    (void)a;
    const bool dark = ui.theme().window_bg.luminance() < 0.5f;
    const float GAP = 16.0f;

    const float band_h  = 148.0f;
    const float label_h = 22.0f;
    const float sec_h   = label_h + band_h + 24.0f;
    const float total   = sec_h * 4.0f + GAP * 3.0f + 16.0f;

    auto sc = ui.scroll(area, total);
    (void)sc;
    float y = area.y - ui.scroll_y();

    auto section = [&](const char* title, const char* note) {
        ui.subheading(title, Rect(area.x, y, area.w * 0.45f, label_h),
                      Style().left().semi_bold().font(15.0f));
        ui.muted_text(note, Rect(area.x + area.w * 0.25f, y, area.w * 0.75f, label_h),
                      Style().right().font(11.0f));
        y += label_h;
    };

    // ------------------------------------------------------------------
    // 1) Liquid Glass —— 用"背景模糊"代替折射与色散
    // ------------------------------------------------------------------
    // kit 的玻璃由 Depth 30 / Refraction 70 / Dispersion 20 三个参数定义，那是
    // 逐像素光线模拟的活。CPU 光栅化的库做不了，也不该假装做得了 —— 这里只实现
    // 其中**看得见的那一半**：背后内容被柔化（Frost 4~24）+ 一层 25% 的冷白材质
    // + 顶部一圈高光。色散/折射不做。
    section("Liquid Glass", "模糊半径 = kit 的 Frost：4 / 6 / 10 / 16 / 24");
    {
        const Rect strip(area.x, y, area.w, band_h);
        desktop_wallpaper(ui, strip, 12.0f);
        const float cw = (strip.w - GAP * 4.0f) / 5.0f;
        static const float kFrost[5] = {4.0f, 6.0f, 10.0f, 16.0f, 24.0f};
        static const char* kName[5] = {"4", "6", "10", "16", "24"};
        for (int i = 0; i < 5; ++i) {
            const Rect cell(strip.x + float(i) * (cw + GAP), strip.y, cw, strip.h);
            ui.glass(cell, 12.0f, Style().blur(kFrost[i]));
            ui.draw_text("Aa", Rect(cell.x, cell.y + 12.0f, cell.w, 34.0f),
                         dark ? Color(255, 255, 255, 220) : Color(0, 0, 0, 190),
                         Style().center().semi_bold().font(22.0f));
            ui.muted_text("Frost " + std::string(kName[i]),
                          Rect(cell.x, cell.bottom() + 5.0f, cell.w, 14.0f),
                          Style().center().font(9.5f));
        }
    }
    y += band_h + 24.0f;

    // ------------------------------------------------------------------
    // 2) 传统 vibrancy —— 只有"透"，没有模糊
    // ------------------------------------------------------------------
    // NSVisualEffectView 的五个厚度档：背后是同一张壁纸，按 alpha 压上去。
    // 和上一组对比就能看出：玻璃的边界从"背后是什么色"变成了"光从哪来"。
    section("Vibrancy", "只有透明度，没有模糊：Ultra Thick → Ultra Thin");
    {
        const Rect strip(area.x, y, area.w, band_h);
        desktop_wallpaper(ui, strip, 12.0f);
        const float cw = (strip.w - GAP * 4.0f) / 5.0f;
        for (int i = 0; i < 5; ++i) {
            const Rect cell(strip.x + float(i) * (cw + GAP), strip.y, cw, strip.h);
            material_face(ui, cell, kBlurMats[i].alpha, dark, 12.0f);
            ui.draw_text("Aa", Rect(cell.x, cell.y + 12.0f, cell.w, 34.0f),
                         dark ? Color(255, 255, 255, 220) : Color(0, 0, 0, 190),
                         Style().center().semi_bold().font(22.0f));
            ui.muted_text(kBlurMats[i].name,
                          Rect(cell.x, cell.bottom() + 5.0f, cell.w, 14.0f),
                          Style().center().font(9.5f));
        }
    }
    y += band_h + 24.0f;

    // ------------------------------------------------------------------
    // 3) 语义材质 —— 同一个模糊面，不同的使用场景
    // ------------------------------------------------------------------
    section("Semantic Materials", "Sidebar / Menu / Popover / HUD / Window / Content");
    {
        const Rect strip(area.x, y, area.w, band_h);
        desktop_wallpaper(ui, strip, 12.0f);
        const float cw = (strip.w - GAP * 5.0f) / 6.0f;
        for (int i = 0; i < 6; ++i) {
            const Rect cell(strip.x + float(i) * (cw + GAP), strip.y, cw, strip.h);
            const bool cell_dark = kSemanticMats[i].always_dark || dark;
            if (std::string(kSemanticMats[i].name) == "Menu" ||
                std::string(kSemanticMats[i].name) == "Popover") {
                ui.glass(cell, 12.0f, Style().blur(16.0f));   // 系统菜单是玻璃
            } else {
                material_face(ui, cell, kSemanticMats[i].alpha, cell_dark, 12.0f);
            }
            ui.draw_text(kSemanticMats[i].name,
                         Rect(cell.x + 10.0f, cell.y + 10.0f, cell.w - 20.0f, 14.0f),
                         cell_dark ? Color(255, 255, 255, 210) : Color(0, 0, 0, 210),
                         Style().left().semi_bold().font(10.0f));
            ui.muted_text(kSemanticMats[i].name,
                          Rect(cell.x, cell.bottom() + 5.0f, cell.w, 14.0f),
                          Style().center().font(9.5f));
        }
    }
    y += band_h + 24.0f;

    // ------------------------------------------------------------------
    // 4) 叠放：一个玻璃窗口浮在壁纸上
    // ------------------------------------------------------------------
    section("Layered", "标题栏与侧边栏是玻璃，内容面是实心 —— 三层的透过率不同");
    {
        const Rect strip(area.x, y, area.w, band_h + 40.0f);
        desktop_wallpaper(ui, strip, 12.0f);

        const Rect win(strip.x + strip.w * 0.28f, strip.y + 22.0f,
                       strip.w * 0.44f, strip.h - 40.0f);
        const float bar = 28.0f, side_w = win.w * 0.34f;

        material_face(ui, win, 0.96f, dark, 10.0f);
        ui.push_clip(win);
        ui.glass(Rect(win.x, win.y + bar, side_w, win.h - bar), 0.0f, Style().blur(18.0f));
        ui.fill_rect(Rect(win.x + side_w, win.y + bar, 1.0f, win.h - bar),
                     dark ? Color(255, 255, 255, 26) : Color(0, 0, 0, 18), 0.0f);
        for (int i = 0; i < 4; ++i)
            ui.fill_rect(Rect(win.x + 12.0f, win.y + bar + 12.0f + float(i) * 20.0f,
                              side_w - 24.0f, 10.0f), ui.theme().label4, 5.0f);
        for (int i = 0; i < 3; ++i)
            ui.fill_rect(Rect(win.x + side_w + 16.0f, win.y + bar + 16.0f + float(i) * 22.0f,
                              win.w - side_w - 32.0f, 10.0f), ui.theme().label4, 5.0f);
        ui.pop_clip();

        // 标题栏压在侧边栏之上（玻璃，圆角 10）
        ui.glass(Rect(win.x, win.y, win.w, bar), 10.0f, Style().blur(20.0f));
        const float lr = 4.5f;
        const Color dots[3] = {Color::hex(0xFF5F57), Color::hex(0xFEBC2E), Color::hex(0x28C840)};
        for (int i = 0; i < 3; ++i)
            ui.fill_circle(win.x + 14.0f + float(i) * 14.0f, win.y + bar * 0.5f, lr, dots[i]);
        ui.draw_text("Window", Rect(win.x, win.y, win.w, bar),
                     dark ? Color(255, 255, 255, 160) : Color(0, 0, 0, 150),
                     Style().center().font(9.5f).semi_bold());
    }
}

// =============================================================================
//  第九轮：kit 里有、库里原来没有的控件
// =============================================================================
void d_search(Ui& ui, App& a, const Rect& box) {
    // 三种状态**并排**（有内容 / 空 / 禁用），不是竖着摞在左上角。kit 的
    // `Search Field` 就是一个 6 态的变体条；竖排三行占不满一行宽，页面看着空。
    const float gap = 24.0f;
    const float fw  = std::min(280.0f, (box.w - gap * 2.0f) / 3.0f);
    static std::string off_val = "aino";     // 禁用示例独占一个串，不跟第一条联动
    std::string empty;
    ui.search_field(&a.search, Rect(box.x, box.y, fw, 24.0f), Style().hint("搜索"));
    ui.search_field(&empty,    Rect(box.x + fw + gap, box.y, fw, 24.0f), Style().hint("搜索"));
    ui.search_field(&off_val,  Rect(box.x + (fw + gap) * 2.0f, box.y, fw, 24.0f),
                    Style().hint("搜索").off());
}

void d_color_well(Ui& ui, App& a, const Rect& box) {
    static const Color kWells[5] = {Color::hex(0x0088FF), Color::hex(0xFF383C),
                                    Color::hex(0x34C759), Color::hex(0xFFCC00),
                                    Color(255, 255, 255)};
    const float w = 48.0f, gap = 12.0f;
    float x = box.x;
    for (int i = 0; i < 5; ++i) {
        const Rect r(x, box.y + 6.0f, w, 24.0f);
        if (ui.color_well(kWells[i], r)) a.well = i;
        if (a.well == i)
            ui.stroke_rect(r.expand(3.0f), ui.theme().ring.scaled_alpha(0.8f), 9.0f, 1.5f);
        x += w + gap;
    }
}

void d_image_well(Ui& ui, App& a, const Rect& box) {
    ui.image_well(Rect(box.x, box.y, 200.0f, 110.0f), Style().hint("拖入图片"));
    (void)a;
}

void d_sidebar(Ui& ui, App& a, const Rect& box) {
    const aino::Theme& th = ui.theme();
    const float w = std::min(box.w, 200.0f);
    const Rect nav(box.x, box.y, w, 240.0f);
    ui.fill_rect(nav, th.sidebar_bg, 10.0f);
    ui.stroke_rect(nav, th.separator, 10.0f, 1.0f);

    float y = nav.y + 10.0f;
    const char* titles[2] = {"收藏", "标签"};
    const char* items[2][4] = {{"应用程序", "桌面", "文稿", "下载"},
                               {"重要", "待办", "归档", ""}};
    for (int s = 0; s < 2; ++s) {
        if (s) { y += 8.0f; }
        y += 18.0f;
        for (int i = 0; i < 4; ++i) {
            if (!items[s][i][0]) continue;
            const int idx = s * 4 + i;
            if (ui.nav_item(items[s][i], a.side_sel == idx,
                            Rect(nav.x + 10.0f, y, nav.w - 20.0f, 24.0f),
                            Style().padding(8.0f).font(13.0f)))
                a.side_sel = idx;
            y += 24.0f;
        }
    }
}

void d_toolbar(Ui& ui, App& a, const Rect& box) {
    const aino::Theme& th = ui.theme();
    const Rect bar(box.x, box.y, box.w, 52.0f);
    ui.fill_rect(bar, th.title_bg, 10.0f);
    ui.fill_rect(Rect(bar.x, bar.bottom() - 1.0f, bar.w, 1.0f), th.separator, 0.0f);

    float x = bar.x + 12.0f;
    ui.icon_button("‹", Rect(x, bar.cy() - 12.0f, 26.0f, 24.0f), Style().ghost());
    x += 26.0f;
    ui.icon_button("›", Rect(x, bar.cy() - 12.0f, 26.0f, 24.0f), Style().ghost());
    x += 40.0f;
    ui.segmented({"日", "周", "月"}, &a.tg, Rect(x, bar.cy() - 12.0f, 126.0f, 24.0f));
    ui.search_field(&a.search, Rect(bar.right() - 178.0f, bar.cy() - 12.0f, 164.0f, 24.0f),
                    Style().hint("搜索"));
}

void d_window(Ui& ui, App& a, const Rect& box) {
    const aino::Theme& th = ui.theme();
    const Rect win(box.x, box.y, std::min(box.w, 420.0f), 210.0f);
    ui.drop_shadow(win, 12.0f, 22.0f, Color(0, 0, 0, 60), 6.0f);
    ui.fill_rect(win, th.title_bg, 12.0f);
    // 内容面：顶部两角跟着标题栏圆，向下铺满
    ui.fill_rect(Rect(win.x, win.y + 34.0f, win.w, win.h - 34.0f), th.window_bg, 12.0f);

    // kit `Title Bars/Window`：标题栏 46、红绿灯 14 直径、间距 9
    const Color lights[3] = {Color::hex(0xFA9891), Color::hex(0xFFDA66), Color::hex(0xADE392)};
    for (int i = 0; i < 3; ++i) {
        const float cx = win.x + 21.0f + 23.0f * float(i);
        ui.fill_circle(cx, win.y + 23.0f, 7.0f, lights[i]);
        ui.stroke_rect(Rect(cx - 7.0f, win.y + 16.0f, 14.0f, 14.0f), Color(0, 0, 0, 26), 7.0f, 0.5f);
    }
    ui.draw_text("未命名", Rect(win.x, win.y, win.w, 46.0f), th.foreground,
                 Style().center().semi_bold().font(13.0f));
    (void)a;
}

void d_sheet(Ui& ui, App& a, const Rect& box) {
    // 截图钩子：AINO_OPEN=sheet 直接把它打开（点出来的状态脚本抓不到）
    if (const char* o = std::getenv("AINO_OPEN"); o && std::string(o) == "sheet")
        a.drawer = true;
    if (ui.button("显示 sheet", Rect(box.x, box.y, 110.0f, 24.0f), Style().secondary()))
        a.drawer = true;
}

// 覆层：真的从窗口顶部降下来（不是画一张示意图）
void o_sheet(Ui& ui, App& a) {
    const float w = std::min(ui.width() * 0.56f, 460.0f);
    const float kTitleH = 52.0f;      // 与窗口外壳同高（定义在下面，这里先局部取）
    ui.sheet("保存更改", "是否保存对「未命名」的更改？此操作会覆盖磁盘上的旧版本。",
             &a.drawer, Rect((ui.width() - w) * 0.5f, kTitleH, w, 132.0f));
    if (!a.drawer) return;
    const Rect base((ui.width() - w) * 0.5f, kTitleH, w, 132.0f);
    const float bw = 96.0f, bh = 24.0f, gap = 10.0f;
    const float by = base.bottom() - 16.0f - bh;
    const float bx = base.right() - 20.0f - bw;
    if (ui.button("保存", Rect(bx, by, bw, bh), Style().primary())) a.drawer = false;
    if (ui.button("取消", Rect(bx - gap - bw, by, bw, bh), Style().secondary())) a.drawer = false;
}

// ---- 合并页：kit 把同一族控件放在一页，这里跟着它合 ----
// Toggles = 复选框 + 单选 + 开关（HIG 的 Toggles 页）；Segmented Controls =
// 标签栏 + 分段 + 切换组（AppKit 里都是 NSSegmentedControl 一族）。
const Demo kToggles[]  = {kCheckbox[0], kRadio[0], kSwitch[0]};
const Demo kSegCtl[]   = {kTabs[0], kToggle[0], kButtonGroup[0]};
// kInput 只有 2 条（单行 / 多行），第三条是 kInputGroup 的"前后缀" ——
// 写 kInput[2] 会读到数组外的内存，页面上会出现一段来路不明的内容。
const Demo kTextF[]    = {kInput[0], kInput[1], kInputGroup[0]};
const Demo kMenusPage[] = {kMenu[0], kContextMenu[0]};

const Demo kSearchPage[] = {
    {"搜索框", "NSSearchField 的胶囊皮：input 复用 + 放大镜 + 清除钮",
     R"cpp(ui.search_field(&q, Rect(x, y, 260.0f, 24.0f), Style().hint("搜索"));)cpp",
     82.0f, d_search},
};
const Demo kColorWellPage[] = {
    {"颜色池", "NSColorWell：48×24；白色也有轮廓",
     R"cpp(if (ui.color_well(c, Rect(x, y, 48.0f, 24.0f))) open_picker();)cpp",
     70.0f, d_color_well},
};
const Demo kImageWellPage[] = {
    {"图像池", "NSImageView 的拖放占位：5% 凹面 + 图片符号",
     R"cpp(ui.image_well(Rect(x, y, 200.0f, 110.0f), Style().hint("拖入图片"));)cpp",
     130.0f, d_image_well},
};
const Demo kSidebarPage[] = {
    {"源列表", "分组标题 + 24 高的行；选中 = 强调色 23%",
     R"cpp(ui.nav_item("文稿", active, Rect(x, y, w, 24.0f), Style().padding(8.0f));)cpp",
     300.0f, d_sidebar},
};
const Demo kToolbarPage[] = {
    {"工具栏", "图标按钮 + 分段控件 + 搜索框，整条是一种材质",
     R"cpp(ui.segmented({"日", "周", "月"}, &v, r);
ui.search_field(&q, r2, Style().hint("搜索"));)cpp",
     104.0f, d_toolbar},
};
const Demo kWindowPage[] = {
    {"窗口", "标题栏 46 · 红绿灯 14/9 · 内容面圆角跟着窗口",
     R"cpp(// 窗口外壳：title_bg 标题栏 + window_bg 内容面 + 12 圆角)cpp",
     264.0f, d_window},
};
const Demo kSheetPage[] = {
    {"工作表", "上平下圆、贴标题栏；不压黑窗口",
     R"cpp(ui.sheet("保存更改", "是否保存？", &open, Rect(x, kTitleH, w, 132.0f));)cpp",
     96.0f, d_sheet},
};

const Page kPages[] = {
    // ── 侧边栏顺序 = Figma `macOS 27` 的页面顺序（HIG 的字母序），分组只是
    //    把 kit 的 30 个页面收成 5 段，页面本身一个不多一个不少。
    {"开始", "概览", "库的定位、结构与实测数据。", "—", nullptr, 0},

    // ── 外观（kit 的 Colors / Text Styles / Materials）──
    {"外观", "Colors", "语义色、填充层级与文本层级在两种外观下的取值。", "—",
     nullptr, 0, nullptr, page_colors, 1},
    {"外观", "Text Styles", "11 个文本样式在 3 个层级 × 4 种材质下的样子。", "—",
     nullptr, 0, nullptr, page_typography, 1},
    {"外观", "Materials", "液态玻璃（模糊替身）与传统 vibrancy 的对比。", "—",
     nullptr, 0, nullptr, page_materials, 1},

    // ── 控件（字母序）──
    {"控件", "Buttons", "推按钮的六种语义变体，统一 24 高度（Medium 档）。", "NSButton",
     kBasic, COUNT(kBasic)},
    {"控件", "Color Wells", "颜色池：48×24 的实心色块。", "NSColorWell",
     kColorWellPage, COUNT(kColorWellPage)},
    {"控件", "Combo Boxes", "可键入过滤的下拉。", "NSComboBox",
     kCombobox, COUNT(kCombobox)},
    {"控件", "Disclosure Controls", "披露行：三角在行首，展开区域由调用方绘制。", "NSDisclosureGroup",
     kAccordion, COUNT(kAccordion)},
    {"控件", "Image Wells", "图像池的空位。", "NSImageView",
     kImageWellPage, COUNT(kImageWellPage)},
    {"控件", "Pop-up and Pull-down Buttons", "下拉按钮：8% 面 + 上下双箭头。", "NSPopUpButton",
     kSelect, COUNT(kSelect)},
    {"控件", "Search Fields", "胶囊搜索框。", "NSSearchField",
     kSearchPage, COUNT(kSearchPage), nullptr, nullptr, 1},
    {"控件", "Segmented Controls", "凹槽内分段；选中 = 强调色内缩 2。", "NSSegmentedControl",
     kSegCtl, COUNT(kSegCtl)},
    {"控件", "Sliders and Dials", "轨道 4px、把手 20×16 的白色胶囊。", "NSSlider",
     kSlider, COUNT(kSlider)},
    {"控件", "Steppers", "20 宽的竖条：上半 ＋、下半 −。", "NSStepper",
     kStepper, COUNT(kStepper)},
    {"控件", "Text Fields", "单行、多行与带前后缀的输入框。", "NSTextField / NSTextView",
     kTextF, COUNT(kTextF)},
    {"控件", "Toggles", "复选框、单选与开关。", "NSButton · switch",
     kToggles, COUNT(kToggles)},

    // ── 容器 ──
    {"容器", "Forms", "标签、说明、错误的纵向组合。", "—",
     kField, COUNT(kField), nullptr, nullptr, 1},
    {"容器", "Group Boxes", "面板与带插槽的列表项。", "NSBox",
     kCard, COUNT(kCard)},
    {"容器", "Lists and Tables", "表头 + 行分隔线，数字列右对齐。", "NSTableView",
     kTable, COUNT(kTable), nullptr, nullptr, 1},
    {"容器", "Sidebars", "源列表：分组标题 + 选中胶囊。", "NSOutlineView · source list",
     kSidebarPage, COUNT(kSidebarPage), nullptr, nullptr, 1},
    {"容器", "Split Views", "可拖动的分割面板。", "NSSplitView",
     kSplit, COUNT(kSplit), nullptr, nullptr, 1},
    {"容器", "Toolbars", "图标按钮 + 分段 + 搜索的整条材质。", "NSToolbar",
     kToolbarPage, COUNT(kToolbarPage), nullptr, nullptr, 1},
    {"容器", "Windows", "标题栏、红绿灯与内容面。", "NSWindow",
     kWindowPage, COUNT(kWindowPage), nullptr, nullptr, 1},

    // ── 菜单与浮层 ──
    {"浮层", "Alerts", "提示条：符号 + 两行文字，不是一张色卡。", "—",
     kAlert, COUNT(kAlert)},
    {"浮层", "Dialogs", "模态面板与确认弹层。", "NSAlert",
     kDialog, COUNT(kDialog), o_dialog},
    {"浮层", "Menu Bar and Dock", "窗口级菜单栏。", "NSMenu · main",
     kMenubar, COUNT(kMenubar), nullptr, nullptr, 1},
    {"浮层", "Menus", "锚定下拉与右键菜单，共用同一个面板。", "NSMenu",
     kMenusPage, COUNT(kMenusPage)},
    {"浮层", "Notifications", "右下角的短暂反馈。", "NSUserNotification",
     kToast, COUNT(kToast)},
    {"浮层", "Popovers", "任意内容的浮出面板。", "NSPopover",
     kPopover, COUNT(kPopover)},
    {"浮层", "Sheets", "贴在标题栏下方的工作表。", "NSWindow · sheet",
     kSheetPage, COUNT(kSheetPage), o_sheet},
    {"浮层", "Tooltips", "悬停说明。", "NSView.toolTip",
     kTooltip, COUNT(kTooltip)},

    // ── 反馈 ──
    {"反馈", "Progress Indicators", "进度条、骨架与旋转指示。", "NSProgressIndicator",
     kProgress, COUNT(kProgress)},
    {"反馈", "Scrollbar", "通用滚动与虚拟列表。", "NSScrollView",
     kScroll, COUNT(kScroll), nullptr, nullptr, 1},
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
constexpr float kNavW      = 244.0f;
constexpr float kNavItemH  = 26.0f;
constexpr float kNavGroupH = 24.0f;
constexpr float kTitleH    = 52.0f;
constexpr float kPageHeadH = 84.0f;   // 页头（Large Title 26 + Callout 14）占的高度
constexpr std::uint64_t kNavTweenId = 0x6e61765f7731ull;

// ---------------------------------------------------------------- 侧边栏的行表
// 行表是导航的**唯一真相**：高度、滚动范围、命中判定全从它来。以前高度由
// nav_content_height() 另算一遍，加一行搜索过滤就得同时改两个地方。
struct NavRow { int kind; int page; int group; };   // 0 = 分组头，1 = 条目

std::string lower_ascii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

// 搜索非空时只留命中的条目；整组都没命中的，分组头也一起消失。
// 搜索期间强制展开 —— 否则"搜到了却在折叠组里看不见"。
std::vector<NavRow> nav_rows(const App& a) {
    const std::string q = lower_ascii(a.query);
    std::vector<char> hit(size_t(kPageCount), 0);
    for (int i = 0; i < kPageCount; ++i)
        hit[size_t(i)] = q.empty() || lower_ascii(kPages[i].name).find(q) != std::string::npos;

    std::vector<NavRow> v;
    for (int i = 0; i < kPageCount; ++i) {
        const int gi = kPageGroup[size_t(i)];
        if (i == 0 || gi != kPageGroup[size_t(i - 1)]) {
            bool any = false;
            for (int k = i; k < kPageCount && kPageGroup[size_t(k)] == gi; ++k)
                any = any || hit[size_t(k)] != 0;
            if (!any) continue;
            v.push_back({0, i, gi});
            if (q.empty() && (a.nav_closed & (1u << unsigned(gi)))) {
                while (i + 1 < kPageCount && kPageGroup[size_t(i + 1)] == gi) ++i;
                continue;
            }
        }
        if (hit[size_t(i)]) v.push_back({1, i, gi});
    }
    return v;
}

// 圆环（图元只有实心圆，放大镜得自己拼）。20 段折线，半径 5 时每段 1.5px，
// 肉眼看不出棱角。
void circle_ring(Ui& ui, float cx, float cy, float r, const Color& c, float w) {
    const int n = 20;
    for (int i = 0; i < n; ++i) {
        const float a0 = 6.2831853f * float(i) / float(n);
        const float a1 = 6.2831853f * float(i + 1) / float(n);
        ui.draw_line(cx + std::cos(a0) * r, cy + std::sin(a0) * r,
                     cx + std::cos(a1) * r, cy + std::sin(a1) * r, c, w);
    }
}

void magnifier(Ui& ui, float cx, float cy, float r, const Color& c) {
    circle_ring(ui, cx, cy, r, c, 1.3f);
    const float k = 0.70710678f;
    ui.draw_line(cx + r * k, cy + r * k, cx + r * k + 2.6f, cy + r * k + 2.6f, c, 1.4f);
}

// 侧边栏开关的 SF 形状："sidebar.left" —— 圆角矩形 + 左侧一列实心。
void sidebar_glyph(Ui& ui, float cx, float cy, const Color& c) {
    const Rect r(cx - 7.5f, cy - 6.0f, 15.0f, 12.0f);
    ui.fill_rect(Rect(r.x + 1.6f, r.y + 1.6f, 4.2f, r.h - 3.2f), c.scaled_alpha(0.5f), 1.6f);
    ui.stroke_rect(r, c, 3.0f, 1.2f);
}

// 侧边栏图标：**按分组**给一枚单色字形（Finder / Xcode 的 source list 每一项
// 都带图标；纯文字在 macOS 里几乎没有先例）。没有去凑 SF Symbols ——41 页各画
// 一枚会变成一堆猜的形状，同组共用一枚反而更像系统里的"类型图标"。
void group_glyph(Ui& ui, int gi, float cx, float cy, const Color& c) {
    // 全部**描边**（不用实心底）：12px 的图标一旦有填充就糊成一坨灰块，
    // SF Symbols 的 regular 字重也是这个做法。线宽 1.2 在 3x 下是 3.6，
    // 和系统源列表的图标笔划接近。
    const float h = 6.0f;
    switch (gi) {
    case 0:                                     // 入门：圆环 + 点心
        circle_ring(ui, cx, cy, h, c, 1.2f);
        ui.fill_circle(cx, cy, 1.5f, c);
        break;
    case 1:                                     // 基础：圆角方框（一个控件）
        ui.stroke_rect(Rect(cx - h, cy - h * 0.8f, h * 2.0f, h * 1.6f), c, 3.2f, 1.2f);
        break;
    case 2:                                     // 表单：输入框（框 + 一条字）
        ui.stroke_rect(Rect(cx - h, cy - h * 0.8f, h * 2.0f, h * 1.6f), c, 3.0f, 1.2f);
        ui.fill_rect(Rect(cx - h * 0.5f, cy - 0.6f, h, 1.2f), c, 0.6f);
        break;
    case 3:                                     // 数据：三行（列表）
        for (int i = 0; i < 3; ++i) {
            const float w = (i == 2) ? 1.1f : 2.0f;
            ui.fill_rect(Rect(cx - h, cy - 4.2f + float(i) * 4.2f, w * h, 1.3f), c, 0.65f);
        }
        break;
    case 4:                                     // 浮层：两块错开的方框
        ui.stroke_rect(Rect(cx - h, cy - h * 0.95f, h * 1.45f, h * 1.1f), c.scaled_alpha(0.65f), 2.4f, 1.2f);
        ui.stroke_rect(Rect(cx - h * 0.4f, cy - h * 0.15f, h * 1.4f, h * 1.05f), c, 2.4f, 1.2f);
        break;
    case 5:                                     // 反馈：两条进度（一长一短）
        ui.stroke_rect(Rect(cx - h, cy - 4.4f, h * 2.0f, 3.2f), c.scaled_alpha(0.55f), 1.6f, 1.2f);
        ui.fill_rect(Rect(cx - h + 0.6f, cy - 3.8f, h * 1.05f, 2.0f), c, 1.0f);
        ui.stroke_rect(Rect(cx - h, cy + 1.6f, h * 2.0f, 3.2f), c.scaled_alpha(0.55f), 1.6f, 1.2f);
        ui.fill_rect(Rect(cx - h + 0.6f, cy + 2.2f, h * 0.55f, 2.0f), c, 1.0f);
        break;
    default:                                    // 容器：两栏
        ui.stroke_rect(Rect(cx - h, cy - h * 0.85f, h * 0.85f, h * 1.7f), c, 2.0f, 1.2f);
        ui.stroke_rect(Rect(cx + h * 0.15f, cy - h * 0.85f, h * 0.85f, h * 1.7f), c, 2.0f, 1.2f);
        break;
    }
}

// -----------------------------------------------------------------------------
//  标题栏
// -----------------------------------------------------------------------------
// macOS 11 之后侧边栏是**整窗高**的：红绿灯浮在侧边栏顶部，标题栏只横跨内容区。
// 之前这里从 y=0 直接铺内容 —— 没有标题栏、没有红绿灯，整页于是像网页而不像 App。
void draw_titlebar(Ui& ui, App& a) {
    const aino::Theme& th = ui.theme();
    const float w  = ui.width();
    const float cy = 26.0f;

    // 标题栏用**材质色**（`Materials - Controls/Title Bar`），不是窗口底 + 一条线。
    // 系统里工具栏和内容区是"两块不同的面"，靠换色分界；同一色再压一根 1px 线
    // 是网页头部的写法。浅色下这块正好和侧边栏同色，连成一条灰带 —— 这就是
    // macOS 的统一工具栏。
    ui.fill_rect(Rect(a.nav_w, 0.0f, w - a.nav_w, kTitleH), th.title_bg, 0.0f);

    // 红绿灯：直径 12、间距 8（官方 Traffic Light 就是这个数）。纯色圆在浅灰
    // 侧边栏上会发飘，所以先垫一层 12% 的暗底当 0.5px 边。
    const Color lights[3] = {Color::hex(0xFF5F57), Color::hex(0xFEBC2E),
                             Color::hex(0x28C840)};
    const auto& in = ui.input();
    for (int i = 0; i < 3; ++i) {
        const float cx = 22.0f + 20.0f * float(i);
        ui.fill_circle(cx, cy, 6.3f, lights[i].mix(Color(0, 0, 0, 255), 0.18f));
        ui.fill_circle(cx, cy, 6.0f, lights[i]);
        // 红 = 关闭（真关：Window::close 让消息循环收尾）。黄/绿不假装能点：
        // 这个库没有最小化/全屏接口，画个能点的按钮却什么都不做更糟。
        if (i == 0 && in.pressed[0] &&
            Rect(cx - 6.0f, cy - 6.0f, 12.0f, 12.0f).contains(in.mouse.x, in.mouse.y))
            a.quit = true;
    }

    // 侧边栏开关。侧边栏收起时让到红绿灯右边 —— 否则正好压在红绿灯上。
    const float toggle_x = std::max(a.nav_w + 14.0f, 92.0f);
    const Rect toggle(toggle_x, cy - 12.0f, 24.0f, 24.0f);
    if (ui.icon_button("", toggle, Style().ghost())) a.nav_open = !a.nav_open;
    sidebar_glyph(ui, toggle.cx(), toggle.cy(), th.label2);

    // 窗口标题：居中在内容区上（不是整个窗口 —— 侧边栏那边是红绿灯的地盘）
    ui.label("aino 组件画廊",
             Rect(a.nav_w, 0.0f, w - a.nav_w, kTitleH),
             Style().center().semi_bold().font(13.0f));

    // 外观：外观切换在 macOS 里是"选一个值"，用分段控件，不是开关。
    const Rect seg(w - 16.0f - 108.0f, cy - 11.0f, 108.0f, 22.0f);
    int mode = a.dark ? 1 : 0;
    if (ui.segmented({"浅色", "深色"}, &mode, seg, Style().font(11.5f)))
        a.dark = (mode == 1);
}

// -----------------------------------------------------------------------------
//  侧边栏
// -----------------------------------------------------------------------------
void draw_nav(Ui& ui, App& a, const Rect& nav) {
    const aino::Theme& th = ui.theme();
    ui.fill_rect(nav, th.sidebar_bg, 0.0f);
    ui.fill_rect(Rect(nav.right() - 1.0f, nav.y, 1.0f, nav.h), th.separator, 0.0f);
    if (nav.w < 60.0f) return;                 // 收起来了：只留一条材质，别画内容

    // 搜索框。macOS 的 NSSearchField 是**胶囊**（Figma `Search` 符号 r=100），
    // 放大镜在框内左端 6px 处，文字从 22 起排。之前这里画的是 r6 的方角输入框 +
    // 框外一个放大镜 —— 那是 Web 的搜索框。
    const Rect sf(nav.x + 10.0f, kTitleH + 8.0f, nav.w - 20.0f, 24.0f);
    ui.input(&a.query, sf, Style().corner(12.0f).padded(26.0f, 3.0f).hint("搜索"));
    magnifier(ui, sf.x + 12.0f, sf.cy(), 4.4f, th.label2);

    const float list_y = sf.bottom() + 10.0f;
    const Rect list(nav.x, list_y, nav.w, std::max(nav.bottom() - list_y - 6.0f, 1.0f));
    const std::vector<NavRow> rows = nav_rows(a);
    float total = 6.0f;
    for (const NavRow& r : rows) total += (r.kind == 0 ? kNavGroupH : kNavItemH);
    total += 10.0f;

    auto sc = ui.scroll(list, total);
    (void)sc;
    for (const NavRow& r : rows) {
        if (r.kind == 0) {
            const Rect gh = ui.row(kNavGroupH);
            // 搜索时强制展开（否则"搜到了却在折叠组里看不见"），三角也跟着朝下
            const bool open = !a.query.empty() ||
                              !(a.nav_closed & (1u << unsigned(r.group)));
            // 组头整行可点（官方只有三角可点，但整行热区更好使，且不额外画东西）
            if (ui.button("", Rect(gh.x + 6.0f, gh.y, gh.w - 12.0f, gh.h), Style().ghost())) {
                if (a.query.empty()) a.nav_closed ^= (1u << unsigned(r.group));
                ui.set_focus(nullptr);
            }
            // 披露三角在**左**边（Finder / Xcode 的 navigator 都是这个位置）
            {
                const float cx = gh.x + 11.0f, cy = gh.cy();
                const Color cc = th.label2;
                if (open) {
                    ui.draw_line(cx - 4.0f, cy - 2.0f, cx, cy + 2.0f, cc, 1.3f);
                    ui.draw_line(cx, cy + 2.0f, cx + 4.0f, cy - 2.0f, cc, 1.3f);
                } else {
                    ui.draw_line(cx - 2.0f, cy - 4.0f, cx + 2.0f, cy, cc, 1.3f);
                    ui.draw_line(cx + 2.0f, cy, cx - 2.0f, cy + 4.0f, cc, 1.3f);
                }
            }
            ui.muted_text(kPages[r.page].group,
                          Rect(gh.x + 21.0f, gh.y, gh.w - 31.0f, gh.h),
                          Style().left().semi_bold().font(11.0f));
            continue;
        }
        const Rect rw = ui.row(kNavItemH);
        const bool active = (a.page == r.page);
        // 选中底左右各内缩 **10**（kit `Item/*/Selected`：198 宽的行，选中块是
        // 178 宽），文字从 44 起 —— 图标 20 宽、间隔 6，和系统 source list 一致。
        if (ui.nav_item(kPages[r.page].name, active,
                        Rect(rw.x + 10.0f, rw.y, rw.w - 20.0f, rw.h),
                        Style().font(13.0f).padding(34.0f)))
            a.page = r.page;
        // 图标是**有色的**，不是灰的：Finder 的收藏、系统设置的面板图标都是
        // 彩色 SF Symbol（而且大半是蓝的）。文字仍然走灰阶（label / label2），
        // 只有图标带强调色 —— 这跟"整行变蓝"是两件事。
        // 用满色。压 alpha 是错的：alpha 越低越往背景色上混，浅色主题还行，
        // 深色主题会直接混成一块暗蓝（#0778CE），比背景还闷。选中与未选中靠
        // 行底胶囊和文字字重区分，不靠图标明度。
        (void)active;
        group_glyph(ui, r.group, rw.x + 28.0f, rw.cy(), th.tint);
    }
    if (rows.empty())
        ui.muted_text("没有匹配的组件", Rect(list.x, list.y + 8.0f, list.w, 20.0f),
                      Style().center().font(11.0f));
}

// =============================================================================
//  内容区
// =============================================================================
// Figma `macOS 27` 的组件页 = **Header + 一串 section**。section 只有"标题 +
// 内容"，没有边框、没有底板：控件直接坐在窗口面上，条目之间靠留白分级。
//
// 之前这里是"Group Box 套表头 + 源码"，那是文档站（shadcn Preview/Code）的写法 ——
// 控件被关进一个框里，和 kit 里"控件浮在内容面上"的样子差一层。
//
//   标题行   15 Semibold（Title 3）              11 次要色说明（右）
//   内容     整宽，控件自己决定左右留白
//   ──────── 细线（只在展开源码时出现）────────
//   源码     11.5 等宽
//
// 标题行整行可点 = 展开 / 收起源码；默认不展开，页面上看到的就是 kit 那种
// 纯组件表，想看用法再点开。
constexpr float kSecH    = 26.0f;   // section 标题行（15 Semibold）
constexpr float kSecTop  = 6.0f;    // 标题 → 内容
constexpr float kRowH    = 22.0f;
constexpr float kCodePad = 10.0f;   // 源码块与细线之间的留白
constexpr float kBoxGap  = 26.0f;   // 两条 section 之间

struct BoxGeom {
    float y       = 0.0f;    // 在本列里的纵向位置（多列排版用）
    float demo_h  = 0.0f;    // 预览实测高度
    float code_h  = 0.0f;    // 源码块高度（收起时 0）
    float inner_w = 0.0f;    // 交给示例函数的宽度
    float box_h   = 0.0f;    // section 总高
};

// 交给示例函数的**内容宽度**。测量和绘制必须共用这一个函数，否则量出来的
// 高度和画出来的对不上。kit 的 section 是整宽的，只留 2px 给描边级别的溢出。
float demo_inner_w(float page_w) {
    return std::max(page_w - 2.0f, 80.0f);
}

// 内容实测高度：让示例在测量模式下空跑一遍（不落像素、不响应、不排帧）。
// 声明值 preview_h 只当"没量出来时的兜底"—— 50 条示例里 37 条声明值偏大。
float demo_need_h(Ui& ui, App& a, const Demo& d, float inner_w) {
    ui.begin_measure();
    d.draw(ui, a, Rect(0.0f, 0.0f, inner_w, d.preview_h));
    const aino::Rect used = ui.end_measure();
    return std::max(used.h, 0.0f);
}

BoxGeom box_geom(Ui& ui, const Demo& d, float page_w, float need_h, bool expanded) {
    BoxGeom g;
    g.inner_w = demo_inner_w(page_w);
    g.demo_h  = need_h > 1.0f ? need_h : d.preview_h;
    g.code_h  = expanded ? ui.code_block_height(d.code, Style().font(11.5f)) : 0.0f;
    g.box_h   = kSecH + kSecTop + g.demo_h;
    if (expanded) g.box_h += 1.0f + kCodePad + g.code_h + kCodePad;
    return g;
}

void draw_demo_box(Ui& ui, App& a, int idx, const Demo& d, const Rect& box,
                   const BoxGeom& g, bool expanded) {
    const aino::Theme& th = ui.theme();

    // ---- 标题行（整行可点：展开 / 收起源码）----
    const Rect head(box.x, box.y, box.w, kSecH);
    // 不在绘制途中翻状态：这一帧的高度是按旧状态算的，就地翻转会让展开出来的
    // 源码画进一个矮了一截的区域里（压到下一个 section 上）。记下来，下一帧在
    // 算高度之前翻。
    if (ui.button("", head, Style().ghost())) {
        a.toggle_pending = idx;
        ui.set_focus(nullptr);
    }
    // 只有标题。kit 的 section 没有说明行 —— 控件自己会说话，旁边再写一行
    // "这个控件是什么"属于自言自语。
    (void)d.note;
    ui.subheading(d.name, Rect(head.x, head.y, head.w, head.h),
                  Style().left().semi_bold().font(15.0f));

    // ---- 内容：直接坐在窗口底上，不套底板、不描边 ----
    const float dy = head.bottom() + kSecTop;
    d.draw(ui, a, Rect(box.x, dy, g.inner_w, g.demo_h));

    // ---- 源码（展开时）----
    if (expanded) {
        const float sy = dy + g.demo_h + kSecTop;
        ui.fill_rect(Rect(box.x + 14.0f, sy, box.w - 28.0f, 1.0f), th.separator, 0.0f);
        ui.code_block(d.code, Rect(box.x + kCodePad, sy + kCodePad,
                                   box.w - 2.0f * kCodePad, g.code_h),
                      Style().font(11.5f));
    }
}

void draw_page(Ui& ui, App& a, const Rect& area) {
    const Page& p = kPages[a.page];

    // ---- 页头 ----
    // kit 的每个组件页都以一个 Header 开场：大标题（Bold）+ 一行说明 + 一个
    // 指向 HIG 的链接。比例按 1400 宽的画布折算回来 = Large Title 26 / Callout 14。
    // 副标用次要色，不加分隔线 —— 分隔线是文档站的写法，mac 靠留白分级。
    ui.heading(p.name, Rect(area.x, area.y, area.w, 34.0f), Style().left().font(26.0f));
    ui.muted_text(p.desc, Rect(area.x, area.y + 36.0f, area.w * 0.78f, 20.0f),
                  Style().left().font(14.0f));

    // 右上角：对应的 AppKit 控件（本库唯一需要"对外解释自己"的元信息）
    if (std::string(p.appkit) != "—") {
        const std::string tag = "AppKit · " + std::string(p.appkit);
        const float tw = ui.text_width(tag, 11.0f) + 4.0f;
        ui.muted_text(tag, Rect(area.right() - tw, area.y + 8.0f, tw, 16.0f),
                      Style().right().font(11.0f));
    }
    // 交互回显：点过按钮之后显示最近一次动作，"到底有没有触发回调"是可见的
    if (a.hits > 0)
        ui.muted_text("最近 · " + a.last,
                      Rect(area.x + area.w * 0.72f, area.y + 31.0f, area.w * 0.28f, 20.0f),
                      Style().right().font(11.0f));

    // ---- 内容 ----
    const Rect content(area.x, area.y + kPageHeadH, area.w,
                       area.bottom() - area.y - kPageHeadH);
    if (a.page == 0) {
        draw_overview(ui, a, content);
        return;
    }
    if (p.custom) { p.custom(ui, a, content); return; }

    // 上一帧点过的表头在这里生效 —— 必须在算高度之前。
    if (a.toggle_pending >= 0) {
        a.code_mask ^= (1u << unsigned(a.toggle_pending));
        a.toggle_pending = -1;
    }

    // 翻页时回到"只展开第一条"：否则上一页展开的位会套到这一页别的示例上。
    if (a.code_page != a.page) {
        a.code_page = a.page;
        a.code_mask = std::getenv("AINO_CODE") ? 0xffffffffu : 0u;
    }

    // 先量高度，再定滚动范围。测量必须在 scroll 之外做：scroll 会给绘制加裁剪
    // 和偏移，套着那层量不到内容的真实高度。
    const float page_w = content.w;
    const int   cols   = p.cols > 0 ? p.cols : 2;
    const float gap    = 28.0f;
    const float cell_w = (page_w - gap * float(cols - 1)) / float(cols);

    std::vector<BoxGeom> geo(size_t(p.n));
    std::vector<int>     col_of(size_t(p.n), 0);
    std::vector<float>   col_y(size_t(cols), 0.0f);
    for (int i = 0; i < p.n; ++i) {
        const float need = demo_need_h(ui, a, p.demos[i], cell_w);
        const bool  exp  = (a.code_mask >> unsigned(i)) & 1u;
        geo[size_t(i)] = box_geom(ui, p.demos[i], cell_w, need, exp);
        int best = 0;
        for (int c = 1; c < cols; ++c)
            if (col_y[size_t(c)] < col_y[size_t(best)]) best = c;
        col_of[size_t(i)] = best;
        geo[size_t(i)].y = col_y[size_t(best)];
        col_y[size_t(best)] += geo[size_t(i)].box_h + kBoxGap;
    }
    float total = 0.0f;
    for (int c = 0; c < cols; ++c) total = std::max(total, col_y[size_t(c)]);
    total += 8.0f;

    {
        auto sc = ui.scroll(content, total);
        (void)sc;
        const Rect origin = ui.row(total);
        for (int i = 0; i < p.n; ++i) {
            const BoxGeom& g = geo[size_t(i)];
            const bool exp = (a.code_mask >> unsigned(i)) & 1u;
            draw_demo_box(ui, a, i, p.demos[i],
                          Rect(origin.x + float(col_of[size_t(i)]) * (cell_w + gap),
                               origin.y + g.y, cell_w, g.box_h), g, exp);
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
    const float inner_w = demo_inner_w(card_w);
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

    // 侧边栏宽度：收起/展开走补间，内容区跟着一起动（不是"瞬间换一张图"）
    a.nav_w = ui.tween(kNavTweenId, a.nav_open ? kNavW : 0.0f, 0.2f,
                       aino::easing::out_cubic);

    if (a.nav_w > 1.0f) draw_nav(ui, a, Rect(0.0f, 0.0f, a.nav_w, ui.height()));
    draw_titlebar(ui, a);

    const Rect area(a.nav_w + 28.0f, kTitleH + 20.0f,
                    std::max(ui.width() - a.nav_w - 56.0f, 1.0f),
                    std::max(ui.height() - kTitleH - 40.0f, 1.0f));
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

static const char* app_name_from_env() {
    static std::string name;
    if (name.empty()) {
        const char* p = std::getenv("AINO_START_PAGE");
        name = p ? p : "概览";
    }
    return name.c_str();
}

int main() {
    aino::Window win(1180, 760, "aino components");
    if (!win.ok()) return 1;
    App app;
    app.page = start_page_from_env();
    // 截图钩子：搜索框预置关键字 / 侧边栏预置收起，省得脚本去点
    if (const char* q = std::getenv("AINO_QUERY")) app.query = q;
    if (const char* n = std::getenv("AINO_NAV")) app.nav_open = std::atoi(n) != 0;
    app.nav_w = app.nav_open ? 220.0f : 0.0f;
    // 性能钩子：AINO_BENCH=N 连续重绘 N 帧（不打地鼠式鼠标事件），
    // 把每帧耗时写进 AINO_FRAME_LOG 指的文件。用来回答"交互卡是不是帧太慢"。
    if (const char* bn = std::getenv("AINO_BENCH")) {
        const int n = std::max(1, std::atoi(bn));
        std::FILE* out = nullptr;
        if (const char* fl = std::getenv("AINO_FRAME_LOG"))
            if (!(fl[0] == '1' && fl[1] == 0)) out = std::fopen(fl, "w");
        std::vector<double> us;
        us.reserve(size_t(n));
        // 前 3 帧丢掉：首帧要建字形缓存、量布局，不代表稳态
        for (int i = 0; i < n + 3; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            win.render();
            const auto t1 = std::chrono::steady_clock::now();
            if (i >= 3) us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        std::sort(us.begin(), us.end());
        auto pct = [&](double p) { return us[size_t(p * double(us.size() - 1))] / 1000.0; };
        double sum = 0.0;
        for (double v : us) sum += v;
        const double mean = sum / double(us.size()) / 1000.0;
        std::fprintf(out ? out : stderr,
            "bench %s  n=%d\n  均 %.2f ms (%.0f fps)  p50 %.2f  p95 %.2f  最差 %.2f\n",
            app_name_from_env(), int(us.size()), mean, 1000.0 / mean,
            pct(0.50), pct(0.95), us.back() / 1000.0);
        if (out) { std::fflush(out); std::fclose(out); }
        return 0;
    }
    // AINO_BENCH_RUN=N：走**真实主循环**连排 N 帧，记录每帧的开始时刻。
    // 单独测绘制的 AINO_BENCH 看不到 Sleep 节流 —— 而"卡不卡"就是帧间隔决定的。
    int bench_run = 0;
    if (const char* br = std::getenv("AINO_BENCH_RUN")) bench_run = std::max(1, std::atoi(br));
    std::vector<double> stamp;
    bool dumped = false;
    int frames = 0;
    // 截图钩子：在**下一帧开头**取上一帧的后备缓冲。
    // 弹层（下拉 / 菜单 / 对话框）都是 deferred 到帧末才画的，回调里立刻取会
    // 全部漏掉 —— 之前截出来的"下拉根本没展开"就是这个原因。
    // 复现钩子：AINO_CHILD_AT=N —— 第 N 帧打开"在新窗口运行这段代码"那个演示窗口。
    // 子窗口在这里建（帧回调查内部 = 主线程正在绘制），才是"点按钮那一下"的时序；
    // 脚本去点按钮得靠像素找坐标，布局一改就点空。
    int child_at = -1;
    if (const char* ca = std::getenv("AINO_CHILD_AT")) child_at = std::atoi(ca);
    bool child_fired = false;
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
        if (child_at >= 0 && !child_fired && frames + 1 >= child_at) {
            child_fired = true;
            open_quickstart_window();
        }
        if (app.quit) win.close();     // 红绿灯的红
        ++frames;
        shot_pending = true;
        if (bench_run > 0) {
            static const auto t_start = std::chrono::steady_clock::now();
            const auto t = std::chrono::steady_clock::now();
            stamp.push_back(std::chrono::duration<double, std::milli>(t - t_start).count());
            if (int(stamp.size()) < bench_run) {
                ui.repaint();                       // 连着要下一帧
            } else {
                std::FILE* out = stderr;
                std::FILE* f = nullptr;
                if (const char* fl = std::getenv("AINO_FRAME_LOG"))
                    if (!(fl[0] == '1' && fl[1] == 0)) f = std::fopen(fl, "a");   // 追加：别盖掉帧日志
                if (f) out = f;
                std::fprintf(out, "bench-run %s  n=%d\n", app_name_from_env(), int(stamp.size()));
                double prev = 0.0, sum = 0.0, worst = 0.0;
                int over20 = 0, over33 = 0;
                for (double v : stamp) {
                    const double d = v - prev;
                    prev = v;
                    if (d <= 0.0) continue;
                    sum += d;
                    if (d > worst) worst = d;
                    if (d > 20.0) ++over20;
                    if (d > 33.0) ++over33;
                }
                const int n = int(stamp.size()) - 1;
                std::fprintf(out, "  帧间隔 均 %.2f ms (%.1f fps)  最差 %.2f ms  >20ms %d 帧  >33ms %d 帧\n",
                             sum / n, 1000.0 * n / sum, worst, over20, over33);
                std::fprintf(out, "  序列 ms:");
                for (size_t i = 1; i < stamp.size() && i < 41; ++i)
                    std::fprintf(out, " %.0f", stamp[i] - stamp[i - 1]);
                std::fprintf(out, "\n");
                if (f) { std::fflush(f); std::fclose(f); }
                win.close();
            }
        }
    });
    return 0;
}
