// =============================================================================
//  组件级性能台架 —— 一个进程只画一个组件
//
//  为什么做成独立 exe 而不是往 gallery 里加一页：
//    性能数据必须能"单独归因"。同一窗口里同时存在 spinner/skeleton 这类
//    自请求重绘的组件时，量到的 CPU 是混在一起的，分不清谁在吃。
//    所以这里每个进程只画一个组件，彼此完全不干扰。
//
//  三种工况（由驱动脚本 perf_bench.py 组合）：
//    · 静置     ：不加 --anim，事件驱动，CPU 必须是 0.00%
//    · 连续重绘 ：--anim，每帧 repaint()，用来量"单帧绘制成本"
//    · 交互     ：不加 --anim，鼠标在组件上抖动/点击，量真实交互开销
//
//  输出：stderr 每 60 帧一行，格式固定，供脚本解析
//    [perf] comp=<name> frames=<n> comp_avg_us=<x> comp_max_us=<x>
//
//  编译：g++ -std=c++17 -O2 -mwindows perf_comp.cpp -o perf_comp.exe -lgdi32 -luser32
//        （-mwindows 是为了避免控制台窗口抢前台把采样窗口挡住 —— 那会让 BitBlt
//          的可视区为空，CPU 读数直接失真）
// =============================================================================
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using aino::Color;
using aino::Rect;
using aino::Style;
using aino::Ui;

// -----------------------------------------------------------------------------
// 组件清单 —— 名字表与绘制函数一一对应
// -----------------------------------------------------------------------------
namespace {

enum Comp {
    C_EMPTY,
    // 文本
    C_LABEL, C_HEADING, C_SUBHEADING, C_MUTED, C_CODE, C_TEXT_WRAP,
    // 容器
    C_PANEL, C_CARD, C_SEPARATOR, C_BADGE, C_PROGRESS, C_SKELETON, C_SKELETON_ANIM,
    // 交互
    C_BUTTON, C_BUTTON_FULL, C_ICON_BUTTON, C_NAV_ITEM, C_INPUT, C_CHECKBOX,
    C_TOGGLE, C_SLIDER, C_TABS, C_SEGMENTED, C_SELECT, C_RADIO, C_STEPPER,
    C_TEXTAREA, C_RATING,
    // 展示
    C_AVATAR, C_SPINNER, C_SPINNER_ANIM, C_ALERT, C_BREADCRUMB, C_KBD, C_ACCORDION, C_TABLE,
    C_SPARKLINE, C_BARS,
    // 浮层
    C_TOOLTIP, C_MENU, C_DIALOG, C_TOAST,
    // 图元
    C_PRIM_RRECT, C_PRIM_CIRCLE, C_PRIM_LINE, C_PRIM_SHADOW, C_PRIM_TEXT,
    // 阴影尺寸阶梯（专门用来拟合 SDF 阴影的 ns/像素，不进主表）
    C_SHADOW_S, C_SHADOW_M, C_SHADOW_L,
    C_COUNT
};

struct Name { const char* name; Comp comp; };

const Name kNames[] = {
    {"empty",         C_EMPTY},
    {"label",         C_LABEL},
    {"heading",       C_HEADING},
    {"subheading",    C_SUBHEADING},
    {"muted_text",    C_MUTED},
    {"code_text",     C_CODE},
    {"text_wrap",     C_TEXT_WRAP},
    {"panel",         C_PANEL},
    {"card",          C_CARD},
    {"separator",     C_SEPARATOR},
    {"badge",         C_BADGE},
    {"progress",      C_PROGRESS},
    {"skeleton",      C_SKELETON},
    {"skeleton_anim", C_SKELETON_ANIM},
    {"button",        C_BUTTON},
    {"button_full",   C_BUTTON_FULL},
    {"icon_button",   C_ICON_BUTTON},
    {"nav_item",      C_NAV_ITEM},
    {"input",         C_INPUT},
    {"checkbox",      C_CHECKBOX},
    {"toggle",        C_TOGGLE},
    {"slider",        C_SLIDER},
    {"tabs",          C_TABS},
    {"segmented",     C_SEGMENTED},
    {"select",        C_SELECT},
    {"radio_group",   C_RADIO},
    {"stepper",       C_STEPPER},
    {"textarea",      C_TEXTAREA},
    {"rating",        C_RATING},
    {"avatar",        C_AVATAR},
    {"spinner",       C_SPINNER},
    {"spinner_anim",  C_SPINNER_ANIM},
    {"alert",         C_ALERT},
    {"breadcrumb",    C_BREADCRUMB},
    {"kbd",           C_KBD},
    {"accordion",     C_ACCORDION},
    {"table",         C_TABLE},
    {"sparkline",     C_SPARKLINE},
    {"bars",          C_BARS},
    {"tooltip",       C_TOOLTIP},
    {"menu",          C_MENU},
    {"dialog",        C_DIALOG},
    {"toast",         C_TOAST},
    {"prim_rrect",    C_PRIM_RRECT},
    {"prim_circle",   C_PRIM_CIRCLE},
    {"prim_line",     C_PRIM_LINE},
    {"prim_shadow",   C_PRIM_SHADOW},
    {"prim_text",     C_PRIM_TEXT},
    {"shadow_s",      C_SHADOW_S},
    {"shadow_m",      C_SHADOW_M},
    {"shadow_l",      C_SHADOW_L},
};

// 组件状态（立即模式：状态由调用方持有，跨帧保留才有 hover/选中效果）
struct State {
    std::string text = "输入内容";
    std::string note = "第一行\n第二行\n第三行";
    std::string long_text;      // text_wrap 用：一段会折行很多次的中文
    bool  b1 = true, b2 = false;
    float f1 = 0.62f;
    int   i1 = 1, i2 = 0, i3 = 3;
    bool  open_dialog = true;
    bool  open_accordion = true;
    int   menu_clicked = -1;
    float step_v = 12.0f;
    std::vector<std::string> items;
    std::vector<std::string> heads;
    std::vector<std::vector<std::string>> rows;
    std::vector<float> series;

    State() {
        items = {"选项一", "选项二", "选项三", "选项四"};
        heads = {"项目", "数值", "状态"};
        rows = {{"渲染帧", "6.4 ms", "正常"},
                {"空闲 CPU", "0.00%", "达标"},
                {"头文件", "247 KiB", "单文件"}};
        series = {12, 30, 22, 48, 36, 55, 41, 62, 50, 70, 58, 66};
        long_text = "aino 是一个单头文件跨平台 GUI 库，立即模式，"
                    "每一帧都由调用方重新描述界面；状态放在调用方的变量里，"
                    "没有控件树，也没有每控件状态机，所以体积可以小到只有一个头文件。";
    }
};

// -----------------------------------------------------------------------------
// 计时
// -----------------------------------------------------------------------------
inline double qpc_us() {
    LARGE_INTEGER t, f;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) * 1e6 / double(f.QuadPart);
}

// -----------------------------------------------------------------------------
// 单组件绘制 —— box 按组件各自的合理尺寸给，用错尺寸会让数据失真
// -----------------------------------------------------------------------------
void draw_one(Ui& ui, Comp c, State& s) {
    const float W = ui.width();

    switch (c) {
    case C_EMPTY:
        break;

    // ---------------- 文本 ----------------
    case C_LABEL:
        ui.label("正文 label —— 默认前景色", Rect(70, 110, 320, 28), Style().left());
        break;
    case C_HEADING:
        ui.heading("标题 Heading", Rect(70, 110, 320, 34), Style().font(20.0f).left());
        break;
    case C_SUBHEADING:
        ui.subheading("副标题 Subheading", Rect(70, 110, 320, 26), Style().left());
        break;
    case C_MUTED:
        ui.muted_text("次要说明 muted_text", Rect(70, 110, 320, 20), Style().left());
        break;
    case C_CODE:
        ui.code_text("aino.hpp · 0.1.0", Rect(70, 110, 320, 30), Style().font(13.0f));
        break;
    case C_TEXT_WRAP:
        // 折行是文本里最贵的路径：每一行都要重新断词、重新排版
        ui.label(s.long_text, Rect(40, 90, 380, 110),
                 Style().left().top().wrap_text().line_spacing(3.0f));
        break;

    // ---------------- 容器 ----------------
    case C_PANEL:
        ui.panel(Rect(50, 95, 360, 130));
        break;
    case C_CARD:
        ui.card(Rect(50, 95, 360, 130));
        break;
    case C_SEPARATOR:
        ui.separator(Rect(70, 140, 320, 1));
        break;
    case C_BADGE:
        ui.badge("已验证", Rect(70, 120, 72, 24));
        break;
    case C_PROGRESS:
        ui.progress(0.68f, Rect(70, 130, 320, 8));
        break;
    case C_SKELETON:
        ui.skeleton(Rect(70, 110, 320, 16));            // 默认静态
        break;
    case C_SKELETON_ANIM:
        ui.skeleton(Rect(70, 110, 320, 16), Style().animate());   // 呼吸动画
        break;

    // ---------------- 交互 ----------------
    case C_BUTTON:
        ui.button("保存", Rect(70, 110, 160, 40), Style().primary());
        break;
    case C_BUTTON_FULL:
        ui.button("整行宽度", Rect(70, 110, 320, 40), Style().primary());
        break;
    case C_ICON_BUTTON:
        ui.icon_button("+", Rect(70, 110, 40, 40), Style().outline());
        break;
    case C_NAV_ITEM:
        ui.nav_item("导航条目", true, Rect(40, 110, 200, 34));
        break;
    case C_INPUT:
        ui.input(&s.text, Rect(70, 110, 320, 36), Style().hint("you@example.com"));
        break;
    case C_CHECKBOX:
        ui.checkbox(&s.b1, "记住这台设备", Rect(70, 115, 240, 22));
        break;
    case C_TOGGLE:
        ui.toggle(&s.b2, Rect(70, 112, 44, 26));
        break;
    case C_SLIDER:
        ui.slider(&s.f1, 0.0f, 1.0f, Rect(70, 125, 320, 20));
        break;
    case C_TABS:
        ui.tabs({"概览", "按钮", "表单"}, &s.i1, Rect(70, 110, 320, 38));
        break;
    case C_SEGMENTED:
        ui.segmented({"日", "周", "月"}, &s.i2, Rect(70, 110, 320, 34));
        break;
    case C_SELECT:
        ui.select(s.items, &s.i1, Rect(70, 110, 240, 36));
        break;
    case C_RADIO:
        ui.radio_group({"选项一", "选项二", "选项三"}, &s.i2, Rect(70, 90, 220, 100));
        break;
    case C_STEPPER:
        ui.stepper(&s.step_v, 1.0f, 0.0f, 100.0f, Rect(70, 110, 140, 36));
        break;
    case C_TEXTAREA:
        ui.textarea(&s.note, Rect(60, 80, 340, 140), Style().font(13.0f));
        break;
    case C_RATING:
        ui.rating(&s.i3, 5, Rect(70, 115, 150, 28));
        break;

    // ---------------- 展示 ----------------
    case C_AVATAR:
        ui.avatar("张伟", Rect(70, 110, 48, 48));
        break;
    case C_SPINNER:
        ui.spinner(Rect(70, 110, 28, 28));
        break;
    case C_SPINNER_ANIM:
        // 显式 .animate()：无论鼠标在哪都一直转（旧行为的出口）
        ui.spinner(Rect(70, 110, 28, 28), Style().animate());
        break;
    case C_ALERT: {
        const std::string t = "提示";
        const std::string d = "这是一条提示信息，用来验证正文折行与裁剪。";
        const Style st = Style().font(13.0f);
        ui.alert(t, d, Rect(40, 90, 380, ui.alert_height(t, d, 380.0f, st)), st);
        break;
    }
    case C_BREADCRUMB:
        ui.breadcrumb({"首页", "组件", "展示补充"}, Rect(40, 115, 380, 20));
        break;
    case C_KBD:
        ui.kbd("Ctrl", Rect(70, 120, 48, 26));
        break;
    case C_ACCORDION:
        ui.accordion("折叠面板标题", &s.open_accordion, Rect(50, 100, 360, 44));
        break;
    case C_TABLE:
        ui.table(s.heads, s.rows, Rect(40, 90, 380, 130), Style().font(12.0f));
        break;
    case C_SPARKLINE:
        ui.sparkline(s.series, Rect(40, 90, 380, 120));
        break;
    case C_BARS:
        ui.bars(s.series, Rect(40, 90, 380, 120));
        break;

    // ---------------- 浮层 ----------------
    case C_TOOLTIP:
        // 只有鼠标在 anchor 上才画；驱动脚本会先把指针挪进去
        ui.tooltip(Rect(160, 140, 140, 34), "这是一条悬停提示");
        break;
    case C_MENU:
        ui.menu("选择操作", s.items, &s.menu_clicked, Rect(70, 110, 200, 36));
        break;
    case C_DIALOG:
        ui.dialog("确认操作", "这个对话框每帧都是打开态，用于测量遮罩 + 面板 + 正文折行"
                             "的完整绘制成本。", &s.open_dialog,
                  Rect(60, 70, 340, 170));
        break;
    case C_TOAST:
        ui.toast("已保存", Rect(250, 230, 180, 44));
        break;

    // ---------------- 图元 ----------------
    case C_PRIM_RRECT:
        ui.fill_rect(Rect(70, 100, 320, 100), Color(41, 41, 44), 16.0f);
        break;
    case C_PRIM_CIRCLE:
        ui.fill_circle(230.0f, 160.0f, 50.0f, Color(59, 130, 246));
        break;
    case C_PRIM_LINE:
        ui.draw_line(70, 130, 390, 130, Color(41, 41, 44), 2.0f);
        break;
    case C_PRIM_SHADOW:
        ui.drop_shadow(Rect(100, 110, 260, 80), 12.0f, 22.0f, Color(0, 0, 0, 46));
        break;
    case C_PRIM_TEXT:
        ui.draw_text("图元层的 draw_text（无容器装饰）",
                     Rect(40, 120, 380, 40), Color(24, 24, 27),
                     Style().font(14.0f).center().middle());
        break;

    // ---------- 阴影尺寸阶梯：只用于拟合 ns/px，不进主表 ----------
    // 三档保持 blur 不变、只放大矩形，于是"面积差"就等于"阴影面积差"。
    case C_SHADOW_S:
        ui.drop_shadow(Rect(150, 130, 160, 40), 6.0f, 10.0f, Color(0, 0, 0, 46));
        break;
    case C_SHADOW_M:
        ui.drop_shadow(Rect(100, 110, 260, 80), 12.0f, 22.0f, Color(0, 0, 0, 46));
        break;
    case C_SHADOW_L:
        ui.drop_shadow(Rect(60, 70, 340, 170), 14.0f, 22.0f, Color(0, 0, 0, 46), 8.0f);
        break;

    default:
        break;
    }
    (void)W;
}

} // namespace

int main(int argc, char** argv) {
    const char* comp_name = nullptr;
    bool anim = false;
    double warmup = 0.0, dur = 0.0;
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--comp=", 7) == 0) comp_name = argv[i] + 7;
        else if (std::strcmp(argv[i], "--anim") == 0) anim = true;
        else if (std::strncmp(argv[i], "--warmup=", 9) == 0) warmup = std::atof(argv[i] + 9);
        else if (std::strncmp(argv[i], "--dur=", 6) == 0) dur = std::atof(argv[i] + 6);
    }

    Comp comp = C_EMPTY;
    if (comp_name) {
        bool found = false;
        for (const auto& n : kNames) {
            if (std::strcmp(n.name, comp_name) == 0) { comp = n.comp; found = true; break; }
        }
        if (!found) {
            std::fprintf(stderr, "[perf] 未知组件: %s\n", comp_name);
            return 2;
        }
    } else {
        comp_name = "empty";
    }

    State s;
    aino::Window win(460, 300, std::string("aino perf ") + comp_name);
    if (!win.ok()) { std::fprintf(stderr, "[perf] 窗口创建失败\n"); return 3; }

    // 统计累加器
    double sum = 0.0, worst = 0.0;
    long frames = 0;
    bool finished = false;
    const double t_start = qpc_us();

    // 打一行启动信息，驱动脚本用它确认组件名对上了
    std::fprintf(stderr, "[perf] start comp=%s anim=%d\n", comp_name, anim ? 1 : 0);
    std::fflush(stderr);

    win.run([&](Ui& ui) {
        const double t_now = (qpc_us() - t_start) / 1e6;

        const double t0 = qpc_us();
        draw_one(ui, comp, s);
        const double t1 = qpc_us();

        // 预热期照画但不统计：首帧要建字体缓存、分配后备缓冲，
        // 混进统计会把"启动成本"错记成"组件成本"。
        //
        // 统计窗口满了之后**只停统计、不退出**：退出时机交给驱动脚本。
        // 这里曾试过到点直接 win.close()，结果和脚本侧的 CPU 采样窗口
        // 差了 0.1 秒，进程先死、psutil 抛 NoSuchProcess，整条记录作废。
        if (t_now >= warmup && !finished) {
            const double us = t1 - t0;
            sum += us;
            if (us > worst) worst = us;
            ++frames;

            if (frames % 60 == 0) {
                std::fprintf(stderr,
                    "[perf] comp=%s frames=%ld comp_avg_us=%.1f comp_max_us=%.1f counting_s=%.2f\n",
                    comp_name, frames, sum / double(frames), worst, t_now - warmup);
                std::fflush(stderr);
            }
            if (dur > 0.0 && t_now - warmup >= dur) {
                finished = true;
                std::fprintf(stderr,
                    "[perf] final comp=%s frames=%ld comp_avg_us=%.1f comp_max_us=%.1f\n",
                    comp_name, frames, frames ? sum / double(frames) : 0.0, worst);
                std::fflush(stderr);
            }
        }

        if (anim) ui.repaint();
    });

    // 退出时补一条最终统计：事件驱动工况往往一帧都攒不满 60 帧，
    // 只靠周期性输出会拿不到数字，所以这里必须再报一次。
    std::fprintf(stderr, "[perf] final comp=%s frames=%ld comp_avg_us=%.1f comp_max_us=%.1f\n",
                 comp_name, frames, frames ? sum / double(frames) : 0.0, worst);
    std::fflush(stderr);
    return 0;
}
