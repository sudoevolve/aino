// aino 原生组件画廊。目录、搜索、锚点和卡片共用一份清单。
// 版式对照 WorkBuddy / 2026-11-10-14-10-36 / index.html。
#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif
#define AINO_IMPLEMENTATION
#include "../aino.hpp"
#include <algorithm>
#include <array>
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
using aino::Rect;
using aino::Style;
using aino::Ui;
using aino::Vec2;

namespace {
constexpr float kNavW = 252, kTitleH = 60, kGridGap = 14;
constexpr int kPageCount = 15;
enum CardId {
    ButtonVariants,
    ButtonSizes,
    ButtonLoading,
    InputBasic,
    InputSearch,
    InputMultiline,
    Switches,
    Checkboxes,
    Radios,
    Sliders,
    Steppers,
    Segments,
    Tabs,
    Chips,
    Dropdowns,
    Context,
    Tooltips,
    Date,
    Time,
    Palette,
    Accents,
    Toasts,
    Dialogs,
    Badges,
    Avatars,
    Table,
    BarChart,
    LineChart,
    DonutChart,
    Stat,
    Progress,
    ProgressRings,
    ContentCard,
    Settings,
    Accordions,
    Carousel,
    Toolbar,
    Breadcrumb,
    Sidebar,
    VirtualList,
    Markdown,
    Empty,
    Typography,
    CardCount
};
struct Notification {
    std::string message;
    int kind;
    float left = 3.2f;
};
struct BuildRow {
    const char *name;
    int kib;
    float ms;
    const char *status;
};
const std::array<BuildRow, 5> kBuildRows = {{{"aino_demo", 128, 6.5f, "通过"},
                                             {"gallery", 342, 11.2f, "通过"},
                                             {"fltk_hello", 96, 3.1f, "待审"},
                                             {"bench_raster", 214, 26.3f, "回退"},
                                             {"layout_probe", 77, 1.9f, "通过"}}};
struct App {
    bool dark = std::getenv("AINO_DARK") && std::strcmp(std::getenv("AINO_DARK"), "0") != 0;
    int appearance = dark ? 0 : 1, well = 0, page = 0, pending_anchor = 0, nav_page = -1;
    bool nav_open = false, quit = false;
    float nav_w = kNavW, nav_offset = 0;
    std::string query, previous_query, last = "准备就绪";
    int hits = 0;
    std::string project = "", email = "", search, password = "aino-demo", invalid_email = "hello@";
    std::string note = "以原生体验为先，保留细腻的层次与流畅交互。";
    bool password_visible = false;
    bool analytics = true, cloud = false, compact = true, remember = true, follow_tint = true;
    bool readonly = false, privacy = true, running = true;
    int radio = 0, seg = 1, chart_view = 0, tab = 0, combo = 0;
    float vol = 44, opacity = 78, step = 12, second_step = 3, loading_left = 0, progress_phase = 0;
    bool chips[4] = {true, false, false, true}, chip_removed = false;
    bool date_open = false, color_open = false, dlg = false, confirm = false;
    int cy = 2026, cm = 11, cd = 10, hour = 9, minute = 30, menu_pick = -1, ctx_pick = -1;
    int sort_column = -1;
    bool sort_desc = false;
    bool expanded[3] = {true, false, false};
    int pending_accordion = -1;
    int slide = 0, page_no = 3, filter = 0, side_sel = 0, virtual_selected = -1;
    std::vector<Notification> notices;
    std::array<Rect, CardCount> card_rects{};
    std::array<float, kPageCount> section_y{};
    Rect content_view;
    float content_height = 0, content_offset = 0;
    int visible_cards = 0, virtual_drawn = 0;
};
struct Card {
    CardId id;
    const char *name;
    const char *tag;
    const char *keywords;
    bool wide = false;
};
struct Page {
    const char *group;
    const char *name;
    const char *anchor;
    const char *desc;
    int first;
    int count;
    float min_column = 300;
    bool full = false;
};
const Card kCards[] = {
    {ButtonVariants, "语义层级", "5 variants", "button 按钮 继续 次要 标准 描边 ghost 删除 禁用"},
    {ButtonSizes, "尺寸与图标", "size", "button icon 图标 复制 收藏 更多 small regular large"},
    {ButtonLoading, "胶囊与加载态", "pill", "button loading spinner 胶囊 加载"},
    {InputBasic, "基础输入", "focus ring", "input 项目 邮箱 输入"},
    {InputSearch, "搜索与密码", "affix", "search password 搜索 密码 错误"},
    {InputMultiline, "多行输入", "textarea", "textarea markdown 多行 字符"},
    {Switches, "开关", "Switch", "switch toggle 开关 同步"},
    {Checkboxes, "复选框", "Checkbox", "checkbox 记住 强调色 只读"},
    {Radios, "单选组", "Radio", "radio 单选 分辨率 hevc"},
    {Sliders, "滑块", "range", "slider range 音量 透明度"},
    {Steppers, "步进器", "Stepper", "stepper 数量 增减"},
    {Segments, "分段控件", "spring thumb", "segmented 日 周 月 图表 表格"},
    {Tabs, "标签页", "tabs", "tabs 概览 性能 日志"},
    {Chips, "标签", "Chip", "chip 标签 badge 状态"},
    {Dropdowns, "下拉选择", "popover", "select menu 下拉 项目 外观"},
    {Context, "右键菜单", "context", "context 右键 重命名"},
    {Tooltips, "悬浮提示", "tooltip", "tooltip 上传 下载 编辑 删除"},
    {Date, "日期选择", "calendar", "date calendar 日期 日历"},
    {Time, "时间选择", "scroll wheel", "time 时间 小时 分钟 滚轮"},
    {Palette, "调色板", "popover", "color palette 颜色 调色板 主题"},
    {Accents, "系统强调色", "8 swatches", "color accent 强调色 主题"},
    {Toasts, "通知", "toast", "toast 成功 信息 错误 通知"},
    {Dialogs, "对话框", "modal", "dialog modal 标准 危险 删除"},
    {Badges, "徽章与键帽", "Badge / kbd", "badge kbd beta 在线 待同步 冲突"},
    {Avatars, "头像与骨架屏", "loading", "avatar skeleton 头像 骨架"},
    {Table, "构建产物", "sortable", "table 表格 排序 构建"},
    {BarChart, "柱状图", "bar", "chart bars 柱状 帧耗时"},
    {LineChart, "折线图", "area + line", "chart line 内存 曲线"},
    {DonutChart, "环形图", "donut", "chart donut 缓存 渲染 纹理 布局"},
    {Stat, "指标卡", "stat", "stat 指标 渲染 性能"},
    {Progress, "进度条", "determinate", "progress 下载 进度 不确定"},
    {ProgressRings, "环形进度", "ring", "progress ring 环形 75 30"},
    {ContentCard, "内容卡片", "content", "card engine 内容 详情 启动"},
    {Settings, "设置行", "row", "settings 通用 网络 隐私"},
    {Accordions, "折叠面板", "details", "accordion 折叠 面板"},
    {Carousel, "轮播", "scroll-snap", "carousel release design engine 轮播", true},
    {Toolbar, "工具栏", "toolbar", "toolbar 翻页 筛选"},
    {Breadcrumb, "面包屑", "path", "breadcrumb 资料库 设计系统"},
    {Sidebar, "侧边栏条目", "nav", "sidebar nav 工作台 素材 成员"},
    {VirtualList, "虚拟列表", "10 000 rows", "virtual list 一万 列表"},
    {Markdown, "Markdown 渲染", "md", "markdown 标题 粗体 列表 代码"},
    {Empty, "空状态", "empty", "empty 无素材 新建"},
    {Typography, "分隔与排版", "layout", "typography divider 分隔 排版"},
};
const Page kPages[] = {
    {"基础", "按钮", "buttons", "Button · 5 种语义 / 3 种尺寸 / 图标 / 加载态", 0, 3},
    {"基础", "输入框", "inputs", "Text Field · 搜索 / 密码 / 多行", 3, 3},
    {"基础", "开关与选择", "selection", "Selection · 开关 / 复选 / 单选", 6, 3},
    {"基础", "滑块与步进", "sliders", "Value · 连续与离散数值", 9, 2},
    {"基础", "分段与标签", "segmented", "Segmented · Tabs · Chip", 11, 3},
    {"浮层", "菜单", "menus", "Menu · 下拉 / 右键 / 悬浮提示", 14, 3},
    {"浮层", "日期与时间", "datetime", "Date & Time · 原生日历与滚轮", 17, 2},
    {"浮层", "颜色", "color", "Color · 全局强调色联动", 19, 2},
    {"浮层", "反馈", "feedback", "Feedback · 通知 / 对话框 / 状态", 21, 4},
    {"数据", "表格", "table", "Table · 点击表头排序", 25, 1, 300, true},
    {"数据", "图表", "charts", "Charts · 柱状 / 折线 / 环形 / 指标", 26, 4, 420},
    {"数据", "进度", "progress", "Progress · 线性与环形", 30, 2},
    {"容器", "卡片与列表", "cards", "Containers · 内容与设置", 32, 4},
    {"容器", "导航", "navigation", "Navigation · 工具栏与路径", 36, 3},
    {"容器", "高级列表", "advanced", "Advanced · 大数据 / Markdown / 空状态", 39, 4},
};
static_assert(sizeof(kCards) / sizeof(kCards[0]) == CardCount, "card manifest mismatch");
static_assert(sizeof(kPages) / sizeof(kPages[0]) == kPageCount, "section manifest mismatch");

std::string lower_ascii(std::string value) {
    for (char &c : value)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return value;
}
const char *kAccentNames[] = {"Blue", "Indigo", "Purple", "Pink", "Red", "Orange", "Green", "Teal"};
Color accent_color(int index, bool dark = false) {
    const auto th = dark ? aino::Theme::dark() : aino::Theme::light();
    const Color colors[] = {th.blue, th.indigo, th.purple, th.pink, th.red, th.orange, th.green, th.teal};
    return colors[std::max(0, std::min(7, index))];
}
aino::Theme gallery_theme(const App &a) {
    auto th = a.dark ? aino::Theme::dark() : aino::Theme::light();
    th.set_tint(accent_color(a.well, a.dark));
    return th;
}
bool system_dark() {
#ifdef _WIN32
    // 动态读取避免给单头示例增加 advapi32 链接依赖。
    using ReadValue = LSTATUS(WINAPI *)(HKEY, LPCWSTR, LPCWSTR, DWORD, LPDWORD, PVOID, LPDWORD);
    HMODULE dll = LoadLibraryW(L"advapi32.dll");
    if (!dll)
        return true;
    auto read = reinterpret_cast<ReadValue>(GetProcAddress(dll, "RegGetValueW"));
    DWORD value = 0, bytes = sizeof(value);
    LSTATUS status = read ? read(HKEY_CURRENT_USER,
                                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Perso"
                                 L"nalize",
                                 L"AppsUseLightTheme", 0x00000010, nullptr, &value, &bytes)
                          : ERROR_INVALID_FUNCTION;
    FreeLibrary(dll);
    return status != ERROR_SUCCESS || value == 0;
#elif defined(__APPLE__)
    CFPropertyListRef value =
        CFPreferencesCopyValue(CFSTR("AppleInterfaceStyle"), kCFPreferencesAnyApplication,
                               kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
    const bool dark = value && CFGetTypeID(value) == CFStringGetTypeID() &&
                      CFStringCompare(static_cast<CFStringRef>(value), CFSTR("Dark"), 0) == kCFCompareEqualTo;
    if (value)
        CFRelease(value);
    return dark;
#else
    const char *theme = std::getenv("GTK_THEME");
    return theme && lower_ascii(theme).find("dark") != std::string::npos;
#endif
}
void set_appearance(Ui &ui, App &a, int mode) {
    a.appearance = mode;
    a.dark = mode == 0 || (mode == 2 && system_dark());
    ui.set_theme(gallery_theme(a));
    ui.repaint();
}
void set_accent(Ui &ui, App &a, int index) {
    a.well = std::max(0, std::min(7, index));
    ui.set_theme(gallery_theme(a));
    ui.repaint();
}
void notify(Ui &ui, App &a, std::string message, int kind = 0) {
    a.last = message;
    ++a.hits;
    if (a.notices.size() == 3)
        a.notices.erase(a.notices.begin());
    a.notices.push_back({std::move(message), kind, 3.2f});
    ui.repaint();
}
void label(Ui &ui, const std::string &text, Rect r, float size = 13, bool muted = false, int weight = 400) {
    ui.draw_text(text, r, muted ? ui.theme().label3 : ui.theme().label, Style().font(size).weight(weight));
}
void ring(Ui &ui, float cx, float cy, float radius, float fraction, Color color, float thickness = 3,
          float start = -1.57079633f) {
    fraction = std::max(0.f, std::min(1.f, fraction));
    int n = std::max(1, int(120 * fraction));
    for (int i = 0; i < n; ++i) {
        float p = start + 6.2831853f * fraction * i / n, q = start + 6.2831853f * fraction * (i + 1) / n;
        ui.draw_line(cx + radius * std::cos(p), cy + radius * std::sin(p), cx + radius * std::cos(q),
                     cy + radius * std::sin(q), color, thickness);
    }
}
// 索引与 SystemIcon 对应，图形由当前平台的系统符号库提供。
enum Glyph {
    Pointer,
    Text,
    Toggle,
    Slider,
    Segment,
    Menu,
    Calendar,
    PaletteIcon,
    Bell,
    TableIcon,
    Chart,
    Loader,
    CardIcon,
    Navigation,
    List,
    Copy,
    Star,
    More,
    Upload,
    Download,
    Edit,
    Trash,
    Prev,
    Next,
    Moon,
    Sun,
    Auto,
    Close,
    Eye,
    Plus,
    Folder,
    SettingsIcon = int(aino::SystemIcon::Settings),
    NetworkIcon = int(aino::SystemIcon::Network),
    PeopleIcon = int(aino::SystemIcon::People)
};
void glyph(Ui &ui, int type, Rect r, Color c) { ui.system_icon(static_cast<aino::SystemIcon>(type), r, c); }
bool icon(Ui &ui, int type, const char *hint, Rect r, Style style = Style().secondary()) {
    // 命中、焦点和表面复用按钮，符号由系统绘制。
    const bool clicked = ui.icon_button("", r, style);
    glyph(ui, type, r.inset(6),
          style.disabled ? ui.theme().label3 : (!style.fg.is_none() ? style.fg : ui.theme().label));
    if (!ui.measuring())
        ui.tooltip(r, hint);
    return clicked;
}
struct Flow {
    Rect area;
    float x, y, row_h = 0, gap = 10;
    explicit Flow(Rect r) : area(r), x(r.x), y(r.y) {}
    Rect next(float w, float h = 24) {
        w = std::min(w, area.w);
        if (x > area.x && x + w > area.right() + .1f) {
            x = area.x;
            y += row_h + gap;
            row_h = 0;
        }
        Rect r{x, y, w, h};
        x += w + gap;
        row_h = std::max(row_h, h);
        return r;
    }
    float bottom() const { return y + row_h; }
    void line() {
        x = area.x;
        y += row_h + gap;
        row_h = 0;
    }
};
void well(Ui &ui, Rect r) { ui.panel(r, Style().muted_text()); }
void swatches(Ui &ui, App &a, Rect area, float size = 24, float gap = 10) {
    Flow flow(area);
    flow.gap = gap;
    for (int i = 0; i < 8; ++i) {
        Rect r = flow.next(size, size);
        Color c = accent_color(i);
        if (ui.color_well(c, r, Style().corner(size / 2)))
            set_accent(ui, a, i);
        if (a.well == i)
            ui.stroke_rect(r.outset(3), c, size / 2 + 3, 1.5f);
        ui.tooltip(r, kAccentNames[i]);
    }
}
void draw_buttons(Ui &ui, App &a, CardId id, Rect b) {
    Flow flow(b);
    if (id == ButtonVariants) {
        const char *names[] = {"继续", "次要", "标准", "描边", "轻量", "删除", "禁用"};
        for (int i = 0; i < 7; ++i) {
            Style st = Style().font(13).secondary();
            if (i == 0)
                st.primary();

            if (i == 3)
                st.outline();
            if (i == 4)
                st.ghost();
            if (i == 5)
                st.destructive();
            if (i == 6)
                st.off();
            if (ui.button(names[i], flow.next(ui.text_width(names[i], 13) + 30), st))
                notify(ui, a, std::string("已点击 ") + names[i]);
        }
    } else if (id == ButtonSizes) {
        const char *names[] = {"Small", "Regular", "Large"};
        const float h[] = {20, 24, 28};
        for (int i = 0; i < 3; ++i)
            if (ui.button(names[i], flow.next(i == 0 ? 65 : 78, h[i]), Style().secondary()))
                notify(ui, a, names[i]);
        flow.line();
        const char *hints[] = {"复制", "收藏", "更多"};
        const int types[] = {Copy, Star, More};
        for (int i = 0; i < 3; ++i)
            if (icon(ui, types[i], hints[i], flow.next(24, 24)))
                notify(ui, a, hints[i]);
    } else {
        for (const char *name : {"立即生成", "订阅更新"})
            if (ui.button(name, flow.next(104), Style().primary().corner(16)))
                notify(ui, a, std::string(name) + "已提交");
        flow.line();
        Rect r = flow.next(126);
        Style loading = Style().secondary().corner(16);
        if (a.loading_left > 0)
            loading.off();
        if (ui.button(a.loading_left > 0 ? "       加载中…" : "点我加载", r, loading)) {
            a.loading_left = 1.6f;
            ui.repaint();
        }
        const bool loading_hover = ui.hovered(r);
        if (a.loading_left > 0) {
            ui.spinner({r.x + 12, r.y + 4, 16, 16}, Style().color(ui.theme().label).animate(loading_hover));
            if (loading_hover) {
                a.loading_left = std::max(0.f, a.loading_left - ui.frame_dt());
                if (a.loading_left == 0)
                    notify(ui, a, "加载完成");
                ui.repaint();
            }
        }
    }
}
void draw_inputs(Ui &ui, App &a, CardId id, Rect b) {
    const Style field = Style().font(13);
    if (id == InputBasic) {
        label(ui, "项目名称", {b.x, b.y, b.w, 18}, 12, true);
        ui.input(&a.project, {b.x, b.y + 24, b.w, 24}, Style(field).hint("例如：组件预览"));
        label(ui, "工作邮箱", {b.x, b.y + 68, b.w, 18}, 12, true);
        ui.input(&a.email, {b.x, b.y + 92, b.w, 24}, Style(field).hint("name@company.com"));
        label(ui, "用于接收项目协作通知", {b.x, b.y + 128, b.w, 18}, 11, true);
    } else if (id == InputSearch) {
        ui.search_field(&a.search, {b.x, b.y, b.w, 24}, Style(field).hint("搜索文件、命令…"));
        ui.input(&a.password, {b.x, b.y + 44, b.w - 32, 24}, Style(field).secret(!a.password_visible));
        if (icon(ui, Eye, a.password_visible ? "隐藏密码" : "显示密码", {b.right() - 24, b.y + 44, 24, 24},
                 Style().ghost()))
            a.password_visible = !a.password_visible;
        ui.input(&a.invalid_email, {b.x, b.y + 88, b.w, 24}, Style(field).stroke(1, ui.theme().red));
        label(ui, "请输入有效的电子邮箱地址", {b.x, b.y + 124, b.w, 18}, 11, true);
    } else {
        label(ui, "内容", {b.x, b.y, b.w, 18}, 12, true);
        ui.textarea(&a.note, {b.x, b.y + 24, b.w, 104}, Style(field).wrap_text());
        if (aino::utf8_count(a.note) > 500) {
            auto cp = aino::utf8_codepoints(a.note);
            a.note = aino::utf8_from_codepoints(cp, 0, 500);
        }
        label(ui, "纯文本输入", {b.x, b.y + 138, b.w - 66, 18}, 11, true);
        ui.draw_text(std::to_string(aino::utf8_count(a.note)) + " / 500", {b.right() - 65, b.y + 138, 65, 18},
                     ui.theme().label3, Style().font(11).right());
    }
}
void draw_selection(Ui &ui, App &a, CardId id, Rect b) {
    if (id == Switches) {
        bool *values[] = {&a.analytics, &a.cloud, &a.compact};
        const char *labels[] = {"允许分析数据", "iCloud 自动同步", "紧凑尺寸"};
        for (int i = 0; i < 3; ++i) {
            float y = b.y + i * 40;
            label(ui, labels[i], {b.x, y, b.w - 70, 30});
            ui.toggle(values[i],
                      {b.right() - 54, y + (i == 2 ? 5.f : 3.f), i == 2 ? 44.f : 54.f, i == 2 ? 20.f : 24.f});
        }
    } else if (id == Checkboxes) {
        ui.checkbox(&a.remember, "记住选项", {b.x, b.y, b.w, 28});
        ui.checkbox(&a.follow_tint, "跟随强调色", {b.x, b.y + 40, b.w, 28});
        ui.checkbox(&a.readonly, "禁用只读", {b.x, b.y + 80, b.w, 28}, Style().off());
    } else
        ui.radio_group({"原始分辨率", "高效 HEVC", "低带宽"}, &a.radio, {b.x, b.y, b.w, 112});
}
void draw_values(Ui &ui, App &a, CardId id, Rect b) {
    if (id == Sliders) {
        float *values[] = {&a.vol, &a.opacity};
        const char *names[] = {"音量", "窗口透明度"};
        for (int i = 0; i < 2; ++i) {
            float y = b.y + i * 64;
            label(ui, names[i], {b.x, y, b.w - 55, 20}, 12, true);
            label(ui, std::to_string(int(*values[i])) + "%", {b.right() - 48, y, 48, 20}, 12);
            ui.slider(values[i], 0, 100, {b.x + 4, y + 28, b.w - 8, 24});
        }
    } else {
        label(ui, "数量", {b.x, b.y, b.w, 18}, 12, true);
        ui.stepper(&a.step, 1, 0, 999, {b.x, b.y + 27, std::min(b.w, 154.f), 24});
        label(ui, "每次增减 1", {b.x, b.y + 72, b.w, 18}, 12, true);
        ui.stepper(&a.second_step, 1, 0, 99, {b.x, b.y + 99, std::min(b.w, 154.f), 24});
    }
}
void draw_segments(Ui &ui, App &a, CardId id, Rect b) {
    if (id == Segments) {
        ui.segmented({"日", "周", "月"}, &a.seg, {b.x, b.y, b.w, 24});
        ui.segmented({"图表", "表格"}, &a.chart_view, {b.x, b.y + 48, b.w, 24});
    } else if (id == Tabs) {
        ui.tabs({"概览", "性能", "日志"}, &a.tab, {b.x, b.y, b.w, 34});
        well(ui, {b.x, b.y + 48, b.w, 70});
        const char *text[] = {"概览面板：一切已准备就绪。", "性能面板：示例数据，6.5 ms 平均渲染耗时。",
                              "日志面板：构建成功，0 个错误。"};
        ui.draw_text(text[a.tab], {b.x + 14, b.y + 56, b.w - 28, 54}, ui.theme().label2,
                     Style().font(12).wrap_text());
    } else {
        Flow flow(b);
        const char *names[] = {"设计", "开发", "文档", "预览 ×"};
        for (int i = 0; i < 4; ++i) {
            if (i == 3 && a.chip_removed)
                continue;
            Style st = Style()
                           .corner(12)
                           .font(12)
                           .background(a.chips[i] ? ui.theme().tint.scaled_alpha(.2f) : ui.theme().fill)
                           .color(a.chips[i] ? ui.theme().tint : ui.theme().label2)
                           .secondary();
            if (ui.button(names[i], flow.next(i == 3 ? 72 : 58, 26), st)) {
                if (i == 3)
                    a.chip_removed = true;
                else
                    a.chips[i] = !a.chips[i];
            }
        }
        flow.line();
        const char *states[] = {"Live", "通过", "待审", "失败"};
        Color colors[] = {ui.theme().tint, ui.theme().green, ui.theme().orange, ui.theme().red};
        for (int i = 0; i < 4; ++i)
            ui.badge(states[i], flow.next(52, 22),
                     Style().color(colors[i]).background(colors[i].scaled_alpha(.14f)).font(11).corner(11));
    }
}
void draw_menus(Ui &ui, App &a, CardId id, Rect b) {
    if (id == Dropdowns) {
        if (ui.menu("最近项目", {"最近项目", "名称 A–Z", "创建时间", "清空"}, &a.menu_pick,
                    {b.x, b.y, std::min(170.f, b.w), 24}))
            notify(ui, a, "已切换项目排列：" + std::to_string(a.menu_pick + 1));
        int mode = a.appearance;
        if (ui.select({"深色", "浅色", "跟随系统"}, &mode, {b.x, b.y + 48, std::min(170.f, b.w), 24}))
            set_appearance(ui, a, mode);
    } else if (id == Context) {
        Rect r{b.x, b.y, b.w, 100};
        well(ui, r);
        ui.draw_text("在此区域点击鼠标右键", r, ui.theme().label3, Style().font(12).center());
        if (!ui.measuring() && ui.context_menu(r, {"新建文件", "复制路径", "重命名", "删除"}, &a.ctx_pick))
            notify(ui, a, "菜单操作已执行：" + std::to_string(a.ctx_pick + 1));
    } else {
        Flow flow(b);
        const char *names[] = {"上传", "下载", "编辑", "删除"};
        for (int i = 0; i < 4; ++i)
            if (icon(ui, Upload + i, names[i], flow.next(24, 24)))
                notify(ui, a, names[i]);
        label(ui, "悬停查看提示", {b.x, flow.bottom() + 14, b.w, 20}, 11, true);
    }
}
void time_wheel(Ui &ui, int *value, int count, Rect area, const char *name) {
    well(ui, area);
    ui.fill_rect({area.x + 4, area.y + 52, area.w - 8, 26}, ui.theme().fill_hover, 7);
    auto sc = ui.scroll(area, float(area.h));
    if (!ui.measuring() && area.contains(ui.input().mouse.x, ui.input().mouse.y) &&
        !ui.impl->gate_blocked_pointer() && std::fabs(ui.input().wheel) > .01f) {
        int delta = ui.input().wheel > 0 ? -1 : 1;
        *value = (*value + delta + count) % count;
        ui.repaint();
    }
    for (int i = -2; i <= 2; ++i) {
        int v = (*value + i + count) % count;
        char text[8];
        std::snprintf(text, sizeof(text), "%02d", v);
        if (ui.button(text, {area.x + 4, area.y + float(i + 2) * 26, area.w - 8, 26},
                      Style()
                          .ghost()
                          .font(i == 0 ? 20 : 15)
                          .color(i == 0 ? ui.theme().label : ui.theme().label3))) {
            *value = v;
            ui.repaint();
        }
    }
    ui.tooltip(area, name);
}
void draw_datetime(Ui &ui, App &a, CardId id, Rect b) {
    if (id == Date) {
        char text[32];
        std::snprintf(text, sizeof(text), "%04d-%02d-%02d", a.cy, a.cm, a.cd);
        ui.popover(
            text, &a.date_open, {b.x, b.y, std::min(190.f, b.w), 24}, {280, 292},
            [&ui, &a](const Rect &r) {
                int y = a.cy, m = a.cm;
                if (ui.calendar(&a.cy, &a.cm, &a.cd, r) && a.cy == y && a.cm == m) {
                    a.date_open = false;
                    ui.repaint();
                }
            },
            Style().secondary());
        label(ui, "点击日期打开日历", {b.x, b.y + 48, b.w, 20}, 11, true);
    } else {
        float w = std::min(80.f, (b.w - 24) / 2);
        time_wheel(ui, &a.hour, 24, {b.x, b.y, w, 130}, "滚轮选择小时");
        label(ui, ":", {b.x + w, b.y + 52, 24, 26}, 20);
        time_wheel(ui, &a.minute, 60, {b.x + w + 24, b.y, w, 130}, "滚轮选择分钟");
        char text[40];
        std::snprintf(text, sizeof(text), "所选时间：%02d:%02d", a.hour, a.minute);
        label(ui, text, {b.x, b.y + 143, b.w, 20}, 12, true);
    }
}
void draw_colors(Ui &ui, App &a, CardId id, Rect b) {
    if (id == Palette) {
        Color c = accent_color(a.well, a.dark);
        char text[24];
        std::snprintf(text, sizeof(text), "     #%02X%02X%02X", c.r, c.g, c.b);
        Rect trigger{b.x, b.y, std::min(184.f, b.w), 24};
        ui.popover(
            text, &a.color_open, trigger, {232, 104},
            [&ui, &a](const Rect &r) {
                label(ui, "选择全局强调色", {r.x, r.y, r.w, 20}, 12, true);
                for (int i = 0; i < 8; ++i) {
                    Rect cell{r.x + (i % 4) * 56.f, r.y + 34 + (i / 4) * 38.f, 26, 26};
                    if (ui.color_well(accent_color(i), cell, Style().corner(13))) {
                        set_accent(ui, a, i);
                        a.color_open = false;
                    }
                    if (a.well == i)
                        ui.stroke_rect(cell.outset(3), accent_color(i), 16, 1.5f);
                    ui.tooltip(cell, kAccentNames[i]);
                }
            },
            Style().secondary());
        ui.fill_circle(trigger.x + 15, trigger.cy(), 6, c);
        label(ui, "选择后，整个画廊的强调色同步更新", {b.x, b.y + 48, b.w, 24}, 11, true);
    } else {
        swatches(ui, a, {b.x + 4, b.y + 4, b.w - 8, 80});
        Flow flow({b.x + 4, b.y + 4, b.w - 8, 80});
        for (int i = 0; i < 8; ++i)
            flow.next(24, 24);
        label(ui, std::string("当前：") + kAccentNames[a.well], {b.x, flow.bottom() + 16, b.w, 20}, 12, true);
    }
}
void draw_feedback(Ui &ui, App &a, CardId id, Rect b) {
    if (id == Toasts) {
        Flow flow(b);
        const char *names[] = {"成功通知", "信息通知", "错误通知"};
        for (int i = 0; i < 3; ++i)
            if (ui.button(names[i], flow.next(90), Style().secondary()))
                notify(ui, a,
                       i == 0   ? "操作已成功完成"
                       : i == 1 ? "有一条新的项目通知"
                                : "操作失败，请重试",
                       i);
        label(ui, "通知在右上方堆叠，3.2 秒后关闭", {b.x, flow.bottom() + 14, b.w, 32}, 11, true);
    } else if (id == Dialogs) {
        Flow flow(b);
        if (ui.button("标准对话框", flow.next(110), Style().secondary()))
            a.dlg = true;
        if (ui.button("危险操作", flow.next(96), Style().secondary().color(ui.theme().red)))
            a.confirm = true;
        label(ui, "支持 Escape、取消与确认", {b.x, flow.bottom() + 14, b.w, 26}, 11, true);
    } else if (id == Badges) {
        Flow flow(b);
        const char *names[] = {"Beta", "在线", "待同步", "冲突", "v" AINO_VERSION};
        Color colors[] = {ui.theme().tint, ui.theme().green, ui.theme().orange, ui.theme().red,
                          ui.theme().label2};
        for (int i = 0; i < 5; ++i)
            ui.badge(names[i], flow.next(i == 2 ? 64 : 52, 22),
                     Style().color(colors[i]).background(colors[i].scaled_alpha(.14f)).font(11).corner(11));
        flow.line();
        ui.kbd("⌘", flow.next(28, 26));
        ui.kbd("⇧", flow.next(28, 26));
        ui.kbd("P", flow.next(28, 26));
    } else {
        Flow flow(b);
        const char *names[] = {"YL", "MK", "JS", "+9", "DS"};
        for (int i = 0; i < 5; ++i) {
            Rect r = flow.next(34, 34);
            Color c = accent_color(i + 1);
            ui.fill_circle(r.cx(), r.cy(), 17, c.scaled_alpha(.25f));
            ui.draw_text(names[i], r, c, Style().font(11).semi_bold().center());
        }
        flow.line();
        well(ui, {b.x, flow.y, b.w, 66});
        ui.skeleton({b.x + 12, flow.y + 14, 36, 36}, Style().corner(18));
        ui.skeleton({b.x + 60, flow.y + 15, std::max(24.f, b.w - 80), 10});
        ui.skeleton({b.x + 60, flow.y + 35, std::max(20.f, b.w - 112), 8});
    }
}
std::array<int, 5> sorted_build_rows(int column, bool descending) {
    std::array<int, 5> rows = {0, 1, 2, 3, 4};
    if (column < 0)
        return rows;
    std::stable_sort(rows.begin(), rows.end(), [=](int l, int r) {
        const auto &a = kBuildRows[l];
        const auto &b = kBuildRows[r];
        int cmp = 0;
        if (column == 0)
            cmp = std::strcmp(a.name, b.name);
        else if (column == 1)
            cmp = (a.kib > b.kib) - (a.kib < b.kib);
        else if (column == 2)
            cmp = (a.ms > b.ms) - (a.ms < b.ms);
        else
            cmp = std::strcmp(a.status, b.status);
        return descending ? cmp > 0 : cmp < 0;
    });
    return rows;
}
void draw_table(Ui &ui, App &a, Rect b) {
    const float widths[] = {.40f, .20f, .20f, .20f};
    const char *heads[] = {"目标", "体积", "耗时", "状态"};
    float x = b.x;
    for (int i = 0; i < 4; ++i) {
        float w = b.w * widths[i];
        std::string title = heads[i];
        if (a.sort_column == i)
            title += a.sort_desc ? " ↓" : " ↑";
        if (ui.button(title, {x, b.y, w, 30},
                      Style().ghost().left().font(12).padded(8, 0).color(ui.theme().label3))) {
            a.sort_desc = a.sort_column == i ? !a.sort_desc : false;
            a.sort_column = i;
            ui.repaint();
        }
        x += w;
    }
    auto order = sorted_build_rows(a.sort_column, a.sort_desc);
    for (int row = 0; row < 5; ++row) {
        const auto &item = kBuildRows[order[row]];
        float y = b.y + 32 + row * 37.f;
        if (row % 2 == 0)
            ui.fill_rect({b.x, y, b.w, 36}, ui.theme().fill.scaled_alpha(.5f), 6);
        char kib[24], ms[24];
        std::snprintf(kib, sizeof(kib), "%d KiB", item.kib);
        std::snprintf(ms, sizeof(ms), "%.1f ms", item.ms);
        const std::string cells[] = {item.name, kib, ms, item.status};
        x = b.x;
        for (int col = 0; col < 4; ++col) {
            float w = b.w * widths[col];
            if (col == 3) {
                Color c = item.status == std::string("通过") ? ui.theme().green : ui.theme().orange;
                ui.badge(cells[col], {x + 4, y + 8, std::min(50.f, w - 8), 21},
                         Style().font(11).color(c).background(c.scaled_alpha(.14f)).corner(10));
            } else
                label(ui, cells[col], {x + 8, y, w - 12, 36}, col == 0 ? 12 : 11);
            x += w;
        }
    }
    ui.separator({b.x, b.y + 30, b.w, 1});
}
void draw_charts(Ui &ui, App &, CardId id, Rect b) {
    Color c = ui.theme().tint, muted = ui.theme().label3;
    if (id == BarChart) {
        const float values[] = {8.2f, 6.5f, 9.4f, 5.8f, 7.1f, 6.4f, 4.9f};
        float plotw = b.w - 30, ploth = 128;
        for (int i = 0; i <= 3; ++i) {
            float y = b.y + ploth - i * ploth / 3;
            label(ui, std::to_string(i * 4), {b.x, y - 7, 23, 14}, 10, true);
            ui.draw_line(b.x + 28, y, b.right(), y, ui.theme().separator, .6f);
        }
        for (int i = 0; i < 7; ++i) {
            float w = plotw / 7, x = b.x + 30 + w * i, h = ploth * values[i] / 12;
            ui.fill_rect_grad({x + 4, b.y + ploth - h, w - 10, h}, c, c.darken(.2f), 4);
            label(ui, std::to_string(i + 1), {x, b.y + ploth + 7, w, 18}, 10, true);
        }
        label(ui, "近 7 天 · 帧耗时（ms）", {b.x, b.y + 160, b.w, 18}, 11, true);
    } else if (id == LineChart) {
        const float values[] = {54, 62, 58, 75, 69, 85, 81, 91, 87, 103, 98, 112};
        float h = 128, w = b.w - 36;
        for (int i = 0; i <= 3; ++i) {
            float y = b.y + h - h * i / 3;
            label(ui, std::to_string(40 + i * 30), {b.x, y - 7, 27, 14}, 10, true);
            ui.draw_line(b.x + 32, y, b.right(), y, ui.theme().separator, .6f);
        }
        for (int i = 1; i < 12; ++i) {
            float x0 = b.x + 34 + w * (i - 1) / 11, x1 = b.x + 34 + w * i / 11,
                  y0 = b.y + h - (values[i - 1] - 40) / 90 * h, y1 = b.y + h - (values[i] - 40) / 90 * h;
            for (float x = x0; x < x1; x += 2) {
                float yy = y0 + (y1 - y0) * (x - x0) / (x1 - x0);
                ui.fill_rect_grad({x, yy, 2, b.y + h - yy}, c.scaled_alpha(.24f), c.scaled_alpha(.01f));
            }
            ui.draw_line(x0, y0, x1, y1, c, 2.2f);
        }
        label(ui, "内存占用（MiB） · 最近 12 次采样", {b.x, b.y + 160, b.w, 18}, 11, true);
    } else if (id == DonutChart) {
        const float cx = b.x + 66, cy = b.y + 65;
        const float values[] = {47, 25, 22};
        Color colors[] = {c, ui.theme().purple, ui.theme().teal};
        float start = -1.57079633f;
        for (int i = 0; i < 3; ++i) {
            float fraction = values[i] / 94;
            ring(ui, cx, cy, 48, fraction - .012f, colors[i], 13, start);
            start += fraction * 6.2831853f;
        }
        ui.draw_text("62%", {cx - 36, cy - 19, 72, 30}, ui.theme().label,
                     Style().font(24).semi_bold().center());
        ui.draw_text("已缓存", {cx - 30, cy + 10, 60, 20}, muted, Style().font(11).center());
        const char *labels[] = {"渲染  47", "纹理  25", "布局  22"};
        float lx = b.x + 144;
        for (int i = 0; i < 3; ++i) {
            ui.fill_circle(lx + 4, b.y + 30 + i * 30.f, 3, colors[i]);
            label(ui, labels[i], {lx + 15, b.y + 20 + i * 30.f, b.w - 159, 20}, 11, true);
        }
        label(ui, "分布按 47 : 25 : 22 归一化", {b.x, b.y + 143, b.w, 20}, 11, true);
    } else {
        label(ui, "平均渲染耗时", {b.x, b.y, b.w, 22}, 12, true);
        label(ui, "6.5 ms", {b.x, b.y + 30, b.w, 46}, 36, false, 650);
        ui.draw_text("↓ 75.3% 更快", {b.x, b.y + 92, b.w, 24}, ui.theme().green, Style().font(13).medium());
        label(ui, "相较上一版本 · 示例数据", {b.x, b.y + 130, b.w, 20}, 11, true);
    }
}
void draw_progress(Ui &ui, App &a, CardId id, Rect b) {
    if (id == Progress) {
        label(ui, "下载进度", {b.x, b.y, b.w - 60, 20}, 12, true);
        label(ui, "64%", {b.right() - 40, b.y, 40, 20}, 12);
        ui.progress(.64f, {b.x, b.y + 32, b.w, 6});
        label(ui, "不确定进度 · 悬停预览", {b.x, b.y + 61, b.w, 20}, 12, true);
        Rect track{b.x, b.y + 94, b.w, 6};
        ui.fill_rect(track, ui.theme().track, 3);
        if (!ui.measuring()) {
            ui.push_clip(track);
            if (ui.hovered({b.x, b.y + 61, b.w, 44})) {
                a.progress_phase = std::fmod(a.progress_phase + ui.frame_dt() * .35f, 1.4f);
                ui.repaint();
            }
            float x = track.x + (a.progress_phase - .2f) * track.w;
            ui.fill_rect({x, track.y, track.w * .4f, 6}, ui.theme().tint, 3);
            ui.pop_clip();
        }
    } else {
        for (int i = 0; i < 2; ++i) {
            float cx = b.x + 45 + i * 106.f, cy = b.y + 46, f = i == 0 ? .75f : .30f;
            ui.progress_ring(f, {cx - 37, cy - 37, 74, 74}, Style().stroke(6, Color::none()));
            ui.draw_text(i == 0 ? "75%" : "30%", {cx - 30, cy - 14, 60, 28}, ui.theme().label,
                         Style().font(16).semi_bold().center());
        }
    }
}
void draw_cards(Ui &ui, App &a, CardId id, Rect b) {
    if (id == ContentCard) {
        Rect r{b.x, b.y, b.w, 170};
        well(ui, r);
        ui.avatar("AI", {r.x + 14, r.y + 14, 36, 36},
                  Style().background(ui.theme().fill).color(ui.theme().label));
        label(ui, "aino 组件示例", {r.x + 62, r.y + 14, r.w - 76, 22}, 13, false, 600);
        label(ui, "C++17 · 立即模式", {r.x + 62, r.y + 39, r.w - 76, 18}, 11, true);
        ui.badge(a.running ? "运行中" : "已停止", {r.x + 14, r.y + 73, 68, 22},
                 Style().color(a.running ? ui.theme().green : ui.theme().label3).secondary().corner(11));
        Flow flow({r.x + 14, r.y + 118, r.w - 28, 34});
        if (ui.button("详情", flow.next(60), Style().secondary()))
            notify(ui, a, "aino 组件示例 · " AINO_VERSION);
        if (ui.button(a.running ? "停止" : "启动", flow.next(60), Style().primary())) {
            a.running = !a.running;
            ui.repaint();
        }
        Rect tail = flow.next(66);
        label(ui, "刚更新", tail, 11, true);
    } else if (id == Settings) {
        const char *titles[] = {"通用", "网络", "隐私"};
        const char *subs[] = {"语言、外观和默认行为", "连接状态与代理", "保护数据与活动记录"};
        for (int i = 0; i < 3; ++i) {
            float y = b.y + i * 58;
            glyph(ui,
                  i == 0   ? SettingsIcon
                  : i == 1 ? NetworkIcon
                           : Eye,
                  {b.x, y + 10, 22, 22}, ui.theme().tint);
            label(ui, titles[i], {b.x + 34, y, b.w - 100, 23}, 13);
            label(ui, subs[i], {b.x + 34, y + 25, b.w - 100, 20}, 10, true);
            if (i < 2) {
                if (icon(ui, Next, i == 0 ? "通用设置" : "网络设置", {b.right() - 28, y + 10, 28, 28},
                         Style().ghost()))
                    notify(ui, a, std::string(titles[i]) + "设置已打开");
            } else
                ui.toggle(&a.privacy, {b.right() - 54, y + 10, 54, 24});
            if (i < 2)
                ui.separator({b.x, y + 53, b.w, 1});
        }
    } else if (id == Accordions) {
        const char *titles[] = {"如何切换强调色？", "是否依赖第三方库？", "支持哪些平台？"};
        const char *answers[] = {"点击侧边栏底部的色块，或在颜色分类中选择强调色。",
                                 "零第三方依赖，只使用标准 C++17 和系统绘图接口。",
                                 "同一份控件代码支持 Windows、macOS 和 Linux。"};
        float y = b.y;
        for (int i = 0; i < 3; ++i) {
            bool next_open = a.expanded[i];
            if (ui.accordion(titles[i], &next_open, {b.x, y, b.w, 32}))
                a.pending_accordion = i;
            y += 36;
            if (a.expanded[i]) {
                float h = ui.text_height(answers[i], b.w - 24, Style().font(12).wrap_text()) + 12;
                ui.draw_text(answers[i], {b.x + 12, y, b.w - 24, h}, ui.theme().label2,
                             Style().font(12).wrap_text());
                y += h + 6;
            }
            if (i < 2) {
                ui.separator({b.x, y, b.w, 1});
                y += 6;
            }
        }
    } else {
        const char *names[] = {"Release", "Design", "Engine"};
        const char *titles[] = {"为下一次发布做好准备", "让每一次交互都自然", "轻量、原生、零依赖"};
        Rect r{b.x, b.y, b.w, 156};
        ui.panel(r);
        label(ui, names[a.slide], {r.x + 22, r.y + 18, r.w - 44, 22}, 12, true);
        label(ui, titles[a.slide], {r.x + 22, r.y + 50, r.w - 44, 32}, 20, false, 600);
        if (icon(ui, Prev, "上一张", {r.x + 18, r.bottom() - 42, 28, 28}, Style().ghost()))
            a.slide = (a.slide + 2) % 3;
        if (icon(ui, Next, "下一张", {r.right() - 46, r.bottom() - 42, 28, 28}, Style().ghost()))
            a.slide = (a.slide + 1) % 3;
        for (int i = 0; i < 3; ++i) {
            Rect dot{r.cx() - 27 + i * 20.f, r.bottom() - 34, 16, 16};
            if (ui.button("slide" + std::to_string(i), dot,
                          Style().ghost().font(.01f).color(Color(0, 0, 0, 1))))
                a.slide = i;
            ui.fill_circle(dot.cx(), dot.cy(), 3.5f, i == a.slide ? ui.theme().tint : ui.theme().label3);
        }
        if (!ui.measuring() && r.contains(ui.input().mouse.x, ui.input().mouse.y) &&
            !ui.impl->gate_blocked_pointer() && ui.input().key_pressed[aino::KEY_RIGHT])
            a.slide = (a.slide + 1) % 3;
    }
}
void draw_navigation(Ui &ui, App &a, CardId id, Rect b) {
    if (id == Toolbar) {
        well(ui, {b.x, b.y, b.w, 46});
        if (icon(ui, Prev, "上一页", {b.x + 6, b.y + 7, 32, 32},
                 Style().ghost().color(a.page_no == 1 ? ui.theme().label3 : ui.theme().label)))
            a.page_no = std::max(1, a.page_no - 1);
        if (icon(ui, Next, "下一页", {b.x + 42, b.y + 7, 32, 32}, Style().ghost()))
            a.page_no = std::min(12, a.page_no + 1);
        label(ui, std::to_string(a.page_no) + " / 12", {b.x + 88, b.y + 7, b.w - 100, 32}, 12, true);
        ui.segmented({"全部", "进行中", "已完成"}, &a.filter, {b.x, b.y + 64, b.w, 32});
    } else if (id == Breadcrumb) {
        Flow f(b);
        const char *names[] = {"资料库", "设计系统", "组件预览"};
        for (int i = 0; i < 3; ++i) {
            float w = ui.text_width(names[i], 12) + 4;
            if (ui.button(names[i], f.next(w, 24),
                          Style().ghost().font(12).color(i < 2 ? ui.theme().tint : ui.theme().label)))
                notify(ui, a, std::string("已打开 ") + names[i]);
            if (i < 2)
                glyph(ui, Next, f.next(12, 24), ui.theme().label3);
        }
        label(ui, "最后编辑于 2 小时前", {b.x, f.bottom() + 20, b.w, 20}, 11, true);
    } else {
        well(ui, {b.x, b.y, b.w, 128});
        const char *names[] = {"工作台", "素材", "成员"};
        for (int i = 0; i < 3; ++i) {
            Rect r{b.x + 8, b.y + 8 + i * 37.f, b.w - 16, 33};
            if (ui.button(
                    names[i], r,
                    Style()
                        .ghost()
                        .left()
                        .padded(34, 0)
                        .background(a.side_sel == i ? ui.theme().tint.scaled_alpha(.16f) : Color::none())
                        .color(a.side_sel == i ? ui.theme().tint : ui.theme().label2)))
                a.side_sel = i;
            glyph(ui,
                  i == 0   ? CardIcon
                  : i == 1 ? Folder
                           : PeopleIcon,
                  {r.x + 8, r.y + 9, 15, 15}, a.side_sel == i ? ui.theme().tint : ui.theme().label3);
            if (i == 0)
                label(ui, "6", {r.right() - 24, r.y, 20, r.h}, 11, true);
        }
    }
}
void draw_advanced(Ui &ui, App &a, CardId id, Rect b) {
    if (id == VirtualList) {
        Rect list{b.x, b.y, b.w, 186};
        well(ui, list);
        a.virtual_drawn = ui.scroll(list.inset(4), 10000, 29.f, [&](int i, const Rect &row) {
            if (ui.button("项目 " + std::to_string(i + 1), row,
                          Style().ghost().left().padded(10, 0).font(12).background(
                              a.virtual_selected == i ? ui.theme().tint.scaled_alpha(.14f) : Color::none())))
                a.virtual_selected = i;
            ui.draw_text("#" + std::to_string(i + 1), {row.right() - 70, row.y, 60, row.h}, ui.theme().label3,
                         Style().font(10).right());
        });
        label(ui, "10,000 行 · 仅绘制可见区域", {b.x, b.y + 198, b.w, 20}, 11, true);
    } else if (id == Markdown) {
        // 这张固定示例按 Markdown 的块结构组合原生文本，不伪称通用 Markdown
        // 解析器。
        label(ui, "轻量 Markdown", {b.x, b.y, b.w, 24}, 19, false, 650);
        label(ui, "原生控件，也能清晰表达内容。", {b.x, b.y + 42, b.w, 24}, 12, false, 600);
        label(ui, "• 标题、强调与列表的组合展示", {b.x, b.y + 78, b.w, 22}, 12);
        label(ui, "• 与系统字体和强调色一致", {b.x, b.y + 106, b.w, 22}, 12);
        well(ui, {b.x, b.y + 144, b.w, 34});
        ui.draw_mono_text("ui.button(\"Hello\", rect);", b.x + 10, b.y + 154, ui.theme().tint, 11);
    } else if (id == Empty) {
        glyph(ui, Folder, {b.cx() - 18, b.y + 2, 36, 36}, ui.theme().label3);
        ui.draw_text("暂无素材", {b.x, b.y + 53, b.w, 24}, ui.theme().label,
                     Style().font(14).semi_bold().center());
        ui.draw_text("当前没有项目，点击下方按钮预览创建操作", {b.x, b.y + 86, b.w, 24}, ui.theme().label3,
                     Style().font(11).center());
        if (ui.button("新建素材", {b.cx() - 50, b.y + 130, 100, 32}, Style().primary()))
            notify(ui, a, "已创建新素材");
    } else {
        label(ui, "标题", {b.x, b.y, b.w - 80, 32}, 20, false, 600);
        label(ui, "34 项", {b.right() - 52, b.y, 52, 32}, 12, true);
        ui.separator({b.x, b.y + 46, b.w, 1});
        label(ui, "分组", {b.x, b.y + 64, b.w - 85, 26}, 12, true);
        ui.badge("Default", {b.right() - 70, b.y + 67, 66, 22}, Style().secondary().font(11).corner(11));
        label(ui, "Tabular nums", {b.x, b.y + 112, b.w, 20}, 11, true);
        ui.draw_mono_text("1,234.56", b.x, b.y + 142, ui.theme().label, 22);
    }
}
void draw_body(Ui &ui, App &a, CardId id, Rect b) {
    if (id <= ButtonLoading)
        draw_buttons(ui, a, id, b);
    else if (id <= InputMultiline)
        draw_inputs(ui, a, id, b);
    else if (id <= Radios)
        draw_selection(ui, a, id, b);
    else if (id <= Steppers)
        draw_values(ui, a, id, b);
    else if (id <= Chips)
        draw_segments(ui, a, id, b);
    else if (id <= Tooltips)
        draw_menus(ui, a, id, b);
    else if (id <= Time)
        draw_datetime(ui, a, id, b);
    else if (id <= Accents)
        draw_colors(ui, a, id, b);
    else if (id <= Avatars)
        draw_feedback(ui, a, id, b);
    else if (id == Table)
        draw_table(ui, a, b);
    else if (id <= Stat)
        draw_charts(ui, a, id, b);
    else if (id <= ProgressRings)
        draw_progress(ui, a, id, b);
    else if (id <= Carousel)
        draw_cards(ui, a, id, b);
    else if (id <= Sidebar)
        draw_navigation(ui, a, id, b);
    else
        draw_advanced(ui, a, id, b);
}

struct PlacedCard {
    int index, section;
    Rect rect;
};
struct GalleryLayout {
    std::vector<PlacedCard> cards;
    std::array<float, kPageCount> section_y{};
    std::array<int, kPageCount> counts{};
    float height = 0;
};
bool card_matches(const App &a, int section, int index) {
    std::string q = lower_ascii(a.query);
    if (q.empty())
        return true;
    const auto &p = kPages[section];
    const auto &c = kCards[index];
    return lower_ascii(std::string(p.name) + " " + p.anchor + " " + c.name + " " + c.keywords).find(q) !=
           std::string::npos;
}
float body_height(Ui &ui, App &a, CardId id, float width) {
    // 绘制函数即测量函数；状态副本防止虚拟列表等测量路径修改真实应用状态。
    App measured = a;
    // 浮层不占常规内容高度；测量时不登记弹层的输入闸门。
    measured.date_open = false;
    measured.color_open = false;
    ui.begin_measure();
    draw_body(ui, measured, id, {0, 0, width, 400});
    Rect used = ui.end_measure();
    return std::max(44.f, used.bottom() + 4);
}
GalleryLayout layout_gallery(Ui &ui, App &a, float width, bool narrow) {
    GalleryLayout out;
    out.section_y.fill(-1);
    float y = narrow ? 18.f : 26.f;
    for (int section = 0; section < kPageCount; ++section) {
        const auto &page = kPages[section];
        std::vector<int> hits;
        for (int i = page.first; i < page.first + page.count; ++i)
            if (card_matches(a, section, i))
                hits.push_back(i);
        out.counts[section] = int(hits.size());
        if (hits.empty())
            continue;
        out.section_y[section] = y;
        y += narrow ? 62 : 44;
        int columns =
            page.full || narrow ? 1 : std::max(1, int((width + kGridGap) / (page.min_column + kGridGap)));
        float cw = (width - kGridGap * (columns - 1)) / columns, row_h = 0;
        int col = 0;
        for (int index : hits) {
            bool span = kCards[index].wide && columns >= 2 && ui.width() >= 1080;
            int slots = span ? 2 : 1;
            if (col + slots > columns) {
                y += row_h + kGridGap;
                col = 0;
                row_h = 0;
            }
            float w = span ? 2 * cw + kGridGap : cw;
            float h = 16 + 18 + 14 + body_height(ui, a, kCards[index].id, w - 32) + 18;
            out.cards.push_back({index, section, {float(col) * (cw + kGridGap), y, w, h}});
            row_h = std::max(row_h, h);
            col += slots;
            if (col == columns) {
                y += row_h + kGridGap;
                col = 0;
                row_h = 0;
            }
        }
        if (col)
            y += row_h + kGridGap;
        y += 34 - kGridGap;
    }
    out.height = y + (narrow ? 90 : 120);
    return out;
}
float nav_item_y(int page) {
    float y = 76;
    const char *group = "";
    for (int i = 0; i <= page; ++i) {
        if (std::strcmp(group, kPages[i].group) != 0) {
            if (i)
                y += 14;
            y += 24;
            group = kPages[i].group;
        }
        if (i == page)
            return y + 16;
        y += 33;
    }
    return y;
}
void draw_nav(Ui &ui, App &a, const GalleryLayout &layout) {
    Rect nav{0, 0, kNavW, ui.height()};
    ui.fill_rect(nav, ui.theme().sidebar_bg);
    ui.separator({kNavW - 1, 0, 1, ui.height()});
    glyph(ui, Navigation, {22, 27, 22, 22}, ui.theme().tint);
    label(ui, "aino", {58, 21, 174, 23}, 15, false, 600);
    label(ui, "组件总览 · 43 项", {58, 44, 174, 18}, 11, true);
    float bottom = ui.height() - 108;
    {
        auto sc = ui.scroll({12, 76, 228, std::max(40.f, bottom - 76)}, 633);
        if (a.nav_page != a.page) {
            const float top = nav_item_y(a.page) - 16;
            if (top - sc.offset < 76)
                sc.to(top - 76);
            else if (top + 32 - sc.offset > bottom)
                sc.to(top + 32 - bottom);
            a.nav_page = a.page;
        }
        a.nav_offset = sc.offset;
        const char *group = "";
        float y = 76 - sc.offset;
        for (int i = 0; i < kPageCount; ++i) {
            if (std::strcmp(group, kPages[i].group) != 0) {
                if (i)
                    y += 14;
                label(ui, kPages[i].group, {22, y, 206, 24}, 10.5f, true, 600);
                y += 24;
                group = kPages[i].group;
            }
            Rect row{12, y, 228, 32};
            const bool active = a.page == i;
            const Color fg = ui.theme().label;
            if (ui.nav_item(kPages[i].name, active, row, Style().font(13).padding(34))) {
                a.query.clear();
                a.pending_anchor = i;
                a.page = i;
                if (ui.width() <= 900)
                    a.nav_open = false;
                ui.repaint();
            }
            glyph(ui, i, {22, y + 8, 15, 15}, ui.theme().tint);
            char count[12];
            std::snprintf(count, sizeof(count), "%02d", layout.counts[i]);
            ui.draw_text(count, {204, y, 22, 32}, fg.scaled_alpha(.65f), Style().font(10.5f).right());
            y += 33;
        }
    }
    ui.separator({18, bottom + 4, 216, 1});
    Rect modes{18, bottom + 15, 216, 32};
    well(ui, modes);
    for (int i = 0; i < 3; ++i) {
        Rect r{modes.x + 3 + i * 71.f, modes.y + 3, 68, 26};
        if (a.appearance == i)
            ui.fill_rect(r, ui.theme().fill_hover, 6);
        const char *hint = i == 0 ? "深色" : i == 1 ? "浅色" : "跟随系统";
        if (icon(ui, Moon + i, hint, r, Style().ghost()))
            set_appearance(ui, a, i);
    }
    swatches(ui, a, {22, bottom + 63, 208, 22}, 17, 10);
}
void draw_toolbar(Ui &ui, App &a, bool narrow) {
    float left = narrow ? 0 : kNavW, pad = narrow ? 14.f : 26.f;
    ui.fill_rect({left, 0, ui.width() - left, kTitleH}, ui.theme().title_bg);
    float tx = left + pad;
    if (narrow) {
        if (icon(ui, List, "显示侧边栏", {tx, 14, 32, 32}, Style().ghost())) {
            a.nav_open = !a.nav_open;
            ui.repaint();
        }
        tx += 40;
    }
    label(ui, "控制台总览", {tx, 0, 136, kTitleH}, 17, false, 650);
    if (!narrow && ui.width() - left > 760)
        label(ui, "43 个原生风格组件 · 零依赖", {tx + 136, 0, 260, kTitleH}, 13, true);
    float sw = narrow ? 128.f : 188.f;
    if (ui.search_field(&a.query, {ui.width() - pad - sw, 18, sw, 24}, Style().hint("搜索组件…").font(12))) {
        a.pending_anchor = -1;
        ui.repaint();
    }
}
void draw_card(Ui &ui, App &a, const Card &card, Rect r) {
    label(ui, card.name, {r.x + 16, r.y + 16, r.w - 32, 22}, 15, false, 600);
    draw_body(ui, a, card.id, {r.x + 16, r.y + 48, r.w - 32, r.h - 66});
}
void draw_content(Ui &ui, App &a, const GalleryLayout &layout, Rect view, bool narrow) {
    float pad = narrow ? 14.f : 26.f;
    auto sc = ui.scroll(view, layout.height);
    if (a.query != a.previous_query) {
        sc.to(0);
        a.previous_query = a.query;
    }
    if (a.pending_anchor >= 0 && a.pending_anchor < kPageCount && layout.section_y[a.pending_anchor] >= 0) {
        int index = a.pending_anchor;
        sc.to(std::max(0.f, layout.section_y[index] - 8));
        a.pending_anchor = -1;
    }
    a.content_offset = sc.offset;
    a.content_height = layout.height;
    a.content_view = view;
    a.section_y = layout.section_y;
    a.visible_cards = 0;
    a.card_rects.fill({});
    int active = -1;
    for (int i = 0; i < kPageCount; ++i)
        if (layout.section_y[i] >= 0 && layout.section_y[i] <= sc.offset + 28)
            active = i;
    if (sc.offset > 0 && sc.offset >= sc.max_offset - 1)
        for (int i = 0; i < kPageCount; ++i)
            if (layout.section_y[i] >= 0)
                active = i;
    if (active >= 0)
        a.page = active;
    if (layout.cards.empty()) {
        ui.draw_text("没有匹配的组件", {view.x + pad, view.y + 90, view.w - pad * 2, 32}, ui.theme().label,
                     Style().font(18).center());
        ui.draw_text("试试 button、颜色、日历或其他名称", {view.x + pad, view.y + 134, view.w - pad * 2, 26},
                     ui.theme().label3, Style().font(12).center());
        return;
    }
    for (int section = 0; section < kPageCount; ++section) {
        if (layout.section_y[section] < 0)
            continue;
        float y = view.y + layout.section_y[section] - sc.offset;
        if (y + 60 < view.y || y > view.bottom())
            continue;
        const auto &p = kPages[section];
        float tx = view.x + pad + 4;
        label(ui, p.name, {tx, y, 240, 28}, 20, false, 680);
        if (narrow)
            label(ui, p.desc, {tx, y + 30, view.w - pad * 2 - 50, 22}, 11, true);
        else {
            float w = ui.text_width(p.name, 20, 680);
            label(ui, p.desc, {tx + w + 10, y + 4, std::max(0.f, view.w - pad * 2 - w - 85), 24}, 12.5f,
                  true);
        }
        ui.draw_text(std::to_string(layout.counts[section]) + " 组件",
                     {view.right() - pad - 64, y + 4, 60, 24}, ui.theme().label3, Style().font(11).right());
    }
    for (const auto &placed : layout.cards) {
        Rect r = placed.rect;
        r.x += view.x + pad;
        r.y += view.y - sc.offset;
        a.card_rects[kCards[placed.index].id] = r;
        if (r.bottom() < view.y - 18 || r.y > view.bottom() + 18)
            continue;
        ++a.visible_cards;
        draw_card(ui, a, kCards[placed.index], r);
    }
}
void draw_overlays(Ui &ui, App &a) {
    float y = 74;
    for (size_t i = 0; i < a.notices.size(); ++i) {
        auto &n = a.notices[i];
        Rect r{std::max(12.f, ui.width() - 324), y, std::min(300.f, ui.width() - 24), 54};
        ui.toast("", r);
        Color c = n.kind == 0 ? ui.theme().green : n.kind == 1 ? ui.theme().tint : ui.theme().red;
        ui.fill_circle(r.x + 18, r.cy(), 4, c);
        label(ui, n.message, {r.x + 34, r.y + 7, r.w - 68, 40}, 12);
        if (icon(ui, Close, "关闭通知", {r.right() - 30, r.y + 13, 26, 26}, Style().ghost()))
            n.left = 0;
        y += 64;
    }
    if (a.dlg || a.confirm) {
        Rect r{(ui.width() - std::min(420.f, ui.width() - 32)) / 2, (ui.height() - 210) / 2,
               std::min(420.f, ui.width() - 32), 210};
        bool danger = a.confirm;
        bool *open = danger ? &a.confirm : &a.dlg;
        if (ui.alert_dialog(danger ? "删除此项目？" : "保存更改？",
                            danger ? "此操作无法撤销，请确认需要删除的内容。"
                                   : "你的设置已准备好，可以保存并继续。",
                            danger ? "删除" : "保存", "取消", open, r,
                            danger ? Style().destructive().background(ui.theme().popover)
                                   : Style().background(ui.theme().popover)))
            notify(ui, a, danger ? "项目已删除" : "设置已保存", danger ? 2 : 0);
    }
}
void build_ui(Ui &ui, App &a) {
    ui.set_theme(gallery_theme(a));
    ui.clear(ui.theme().background);
    // 布局相关状态先结算，再测量；展开操作不会在当前帧挤入下一张卡片。
    if (a.pending_accordion >= 0) {
        a.expanded[a.pending_accordion] = !a.expanded[a.pending_accordion];
        a.pending_accordion = -1;
    }
    if (a.nav_open && ui.input().key_pressed[aino::KEY_ESC]) {
        a.nav_open = false;
        ui.repaint();
    }
    for (auto &n : a.notices)
        n.left -= ui.frame_dt();
    a.notices.erase(
        std::remove_if(a.notices.begin(), a.notices.end(), [](const Notification &n) { return n.left <= 0; }),
        a.notices.end());
    if (!a.notices.empty())
        ui.repaint();
    bool narrow = ui.width() <= 900;
    a.nav_w = narrow ? 0 : kNavW;
    float pad = narrow ? 14.f : 26.f;
    GalleryLayout layout = layout_gallery(ui, a, std::max(1.f, ui.width() - a.nav_w - 2 * pad), narrow);
    const std::string layout_query = a.query;
    if (!narrow)
        draw_nav(ui, a, layout);
    draw_toolbar(ui, a, narrow);
    if (a.query != layout_query)
        layout = layout_gallery(ui, a, std::max(1.f, ui.width() - a.nav_w - 2 * pad), narrow);
    draw_content(
        ui, a, layout,
        {a.nav_w, kTitleH, std::max(1.f, ui.width() - a.nav_w), std::max(1.f, ui.height() - kTitleH)},
        narrow);
    if (narrow && a.nav_open) {
        aino::Ui::Impl::SeqFrameEnd layer(ui.impl);
        ui.impl->gate_claim({0, 0, ui.width(), ui.height()}, aino::Ui::Impl::Z_FRAME_END);
        if (ui.button("", {kNavW, 0, ui.width() - kNavW, ui.height()}, Style().ghost()))
            a.nav_open = false;
        // 抽屉本帧已在最后绘制；闸门让背后控件在后续帧不再接收输入。
        draw_nav(ui, a, layout);
    }
    draw_overlays(ui, a);
}
void dump_layout_report(Ui &ui, App &a, float width) {
    const char *path = std::getenv("AINO_LAYOUT_REPORT");
    if (!path || !*path)
        return;
    std::FILE *f = std::fopen(path, "w");
    if (!f)
        return;
    auto layout = layout_gallery(ui, a, width, false);
    std::fprintf(f, "# sections=%d cards=%d content_height=%.1f\n", kPageCount, int(layout.cards.size()),
                 layout.height);
    for (const auto &c : layout.cards)
        std::fprintf(f, "%s / %s: x=%.1f y=%.1f w=%.1f h=%.1f\n", kPages[c.section].name,
                     kCards[c.index].name, c.rect.x, c.rect.y, c.rect.w, c.rect.h);
    std::fclose(f);
}

void open_quickstart_window() {
    // 子窗口的诊断口：WIN32 子系统没有 stderr，只能落文件。
    auto clog = [](const char *msg) {
        if (const char *f = std::getenv("AINO_CHILD_LOG"))
            if (std::FILE *fp = std::fopen(f, "a")) {
                std::fprintf(fp, "%s\n", msg);
                std::fclose(fp);
            }
    };
    std::thread([clog] {
        // 这里刻意用 WindowConfig 而不是三参数构造：演示一下新加的配置项
        // （最小尺寸、居中）。标题里的版本号顺便当个"这窗口是谁开的"标记。
        aino::WindowConfig cfg;
        cfg.width = 460;
        cfg.height = 320;
        cfg.title = "aino 最小示例 · " AINO_VERSION;
        cfg.min_width = 380;
        cfg.min_height = 260;
        cfg.center = true;

        clog("cfg 就绪，开始建窗口");
        aino::Window win(cfg);
        clog(win.ok() ? "窗口已创建" : "窗口创建失败 (ok=false)");
        if (!win.ok())
            return;

        std::string email;
        bool notify = true;
        int saved = 0;

        win.run([&](Ui &ui) {
            ui.clear(ui.theme().background);
            ui.panel(Rect(0.0f, 0.0f, ui.width(), ui.height()));
            auto c = ui.vbox(Rect(0.0f, 0.0f, ui.width(), ui.height()), 10.0f, 20.0f);
            ui.heading("设置", c.next(34.0f));
            ui.separator(c.next(1.0f));
            ui.input(&email, c.next(24.0f));
            ui.checkbox(&notify, "邮件通知", c.next(22.0f));
            if (ui.button("保存", c.next(24.0f), Style().primary()))
                ++saved;
            ui.muted_text(email.empty() ? std::string("输入邮箱后点保存")
                                        : "已保存 " + std::to_string(saved) + " 次：" + email,
                          c.next(20.0f), Style().left().font(11.5f));
        });
    }).detach();
}

} // namespace

// 工具用的起始页：AINO_START_PAGE=<序号|名字>。截图 / 冒烟脚本要逐页拍，
// 靠点导航得先算滚动偏移和分组折叠状态，太容易跟着版式一起坏。
int start_page_from_env() {
    const char *sp = std::getenv("AINO_START_PAGE");
    if (!sp || !*sp)
        return 0;
    if (const int n = std::atoi(sp); n > 0 && n < kPageCount)
        return n;
    for (int i = 0; i < kPageCount; ++i) {
        if (std::string(kPages[i].name) == sp || std::string(kPages[i].anchor) == sp)
            return i;
    }
    return 0;
}

// 工具钩子：AINO_SHOT_BMP=<路径> —— 把后备缓冲存成 24bit BMP。
// xvfb 里没有交互桌面，Linux 上想看"真画成什么样"只能这么取帧；
// Windows 上平时不用（有 shot_pages.py 走 PrintWindow）。
void dump_canvas_bmp(aino::Window &win, const char *path) {
    aino::Canvas &cv = win.impl->canvas;
    if (!cv.px || cv.w <= 0 || cv.h <= 0 || cv.stride <= 0)
        return;
    std::FILE *f = std::fopen(path, "wb");
    if (!f)
        return;
    const int w = cv.w, h = cv.h;
    const int pad = (4 - (w * 3) % 4) % 4;
    const int row = w * 3 + pad;
    const uint32_t data = uint32_t(row) * uint32_t(h);
    const uint32_t file = 54u + data, off = 54u, bisize = 40u;
    const uint16_t planes = 1, bpp = 24;
    uint8_t hd[54] = {};
    hd[0] = 'B';
    hd[1] = 'M';
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
    for (int y = h - 1; y >= 0; --y) { // BMP 自下而上
        const uint8_t *src = &cv.px[size_t(y) * size_t(cv.stride)];
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

static const char *app_name_from_env() {
    static std::string name;
    if (name.empty()) {
        const char *p = std::getenv("AINO_START_PAGE");
        name = p ? p : "概览";
    }
    return name.c_str();
}

int main() {
    aino::WindowConfig cfg;
    cfg.width = 1180;
    cfg.height = 760;
    cfg.min_width = 640;
    cfg.min_height = 480;
    cfg.title = "aino components";
    aino::Window win(cfg);
    if (!win.ok())
        return 1;
    App app;
    app.page = start_page_from_env();
    app.pending_anchor = app.page;
    if (const char *open = std::getenv("AINO_OPEN")) {
        app.date_open = std::strcmp(open, "calendar") == 0;
        app.color_open = std::strcmp(open, "color") == 0;
        app.dlg = std::strcmp(open, "dialog") == 0;
    }
    // 截图钩子：搜索框预置关键字 / 侧边栏预置收起，省得脚本去点
    if (const char *q = std::getenv("AINO_QUERY"))
        app.query = q;
    if (const char *n = std::getenv("AINO_NAV"))
        app.nav_open = std::atoi(n) != 0;
    app.nav_w = app.nav_open ? kNavW : 0.0f;
    // 性能钩子：AINO_BENCH=N 连续重绘 N 帧（不打地鼠式鼠标事件），
    // 把每帧耗时写进 AINO_FRAME_LOG 指的文件。用来回答"交互卡是不是帧太慢"。
    if (const char *bn = std::getenv("AINO_BENCH")) {
        const int n = std::max(1, std::atoi(bn));
        std::FILE *out = nullptr;
        if (const char *fl = std::getenv("AINO_FRAME_LOG"))
            if (!(fl[0] == '1' && fl[1] == 0))
                out = std::fopen(fl, "w");
        std::vector<double> us;
        us.reserve(size_t(n));
        // 前 3 帧丢掉：首帧要建字形缓存、量布局，不代表稳态
        for (int i = 0; i < n + 3; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            win.render();
            const auto t1 = std::chrono::steady_clock::now();
            if (i >= 3)
                us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        std::sort(us.begin(), us.end());
        auto pct = [&](double p) { return us[size_t(p * double(us.size() - 1))] / 1000.0; };
        double sum = 0.0;
        for (double v : us)
            sum += v;
        const double mean = sum / double(us.size()) / 1000.0;
        std::fprintf(out ? out : stderr,
                     "bench %s  n=%d\n  均 %.2f ms (%.0f fps)  p50 %.2f  p95 %.2f  "
                     "最差 %.2f\n",
                     app_name_from_env(), int(us.size()), mean, 1000.0 / mean, pct(0.50), pct(0.95),
                     us.back() / 1000.0);
        if (out) {
            std::fflush(out);
            std::fclose(out);
        }
        return 0;
    }
    // AINO_BENCH_RUN=N：走**真实主循环**连排 N 帧，记录每帧的开始时刻。
    // 单独测绘制的 AINO_BENCH 看不到 Sleep 节流 —— 而"卡不卡"就是帧间隔决定的。
    int bench_run = 0;
    if (const char *br = std::getenv("AINO_BENCH_RUN"))
        bench_run = std::max(1, std::atoi(br));
    std::vector<double> stamp;
    bool dumped = false;
    int frames = 0;
    // 截图钩子：在**下一帧开头**取上一帧的后备缓冲。
    // 弹层（下拉 / 菜单 / 对话框）都是 deferred 到帧末才画的，回调里立刻取会
    // 全部漏掉 —— 之前截出来的"下拉根本没展开"就是这个原因。
    // 复现钩子：AINO_CHILD_AT=N —— 第 N
    // 帧打开"在新窗口运行这段代码"那个演示窗口。 子窗口在这里建（帧回调查内部 =
    // 主线程正在绘制），才是"点按钮那一下"的时序；
    // 脚本去点按钮得靠像素找坐标，布局一改就点空。
    int child_at = -1;
    if (const char *ca = std::getenv("AINO_CHILD_AT"))
        child_at = std::atoi(ca);
    bool child_fired = false;
    const char *shot = std::getenv("AINO_SHOT_BMP");
    const bool shot_every = std::getenv("AINO_SHOT_EVERY") != nullptr;
    bool shot_pending = false;
    win.run([&](Ui &ui) {
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
        if (app.quit)
            win.close(); // 红绿灯的红
        ++frames;
        shot_pending = true;
        if (bench_run > 0) {
            static const auto t_start = std::chrono::steady_clock::now();
            const auto t = std::chrono::steady_clock::now();
            stamp.push_back(std::chrono::duration<double, std::milli>(t - t_start).count());
            if (int(stamp.size()) < bench_run) {
                ui.repaint(); // 连着要下一帧
            } else {
                std::FILE *out = stderr;
                std::FILE *f = nullptr;
                if (const char *fl = std::getenv("AINO_FRAME_LOG"))
                    if (!(fl[0] == '1' && fl[1] == 0))
                        f = std::fopen(fl, "a"); // 追加：别盖掉帧日志
                if (f)
                    out = f;
                std::fprintf(out, "bench-run %s  n=%d\n", app_name_from_env(), int(stamp.size()));
                double prev = 0.0, sum = 0.0, worst = 0.0;
                int over20 = 0, over33 = 0;
                for (double v : stamp) {
                    const double d = v - prev;
                    prev = v;
                    if (d <= 0.0)
                        continue;
                    sum += d;
                    if (d > worst)
                        worst = d;
                    if (d > 20.0)
                        ++over20;
                    if (d > 33.0)
                        ++over33;
                }
                const int n = int(stamp.size()) - 1;
                std::fprintf(out,
                             "  帧间隔 均 %.2f ms (%.1f fps)  最差 %.2f ms  >20ms %d "
                             "帧  >33ms %d 帧\n",
                             sum / n, 1000.0 * n / sum, worst, over20, over33);
                std::fprintf(out, "  序列 ms:");
                for (size_t i = 1; i < stamp.size() && i < 41; ++i)
                    std::fprintf(out, " %.0f", stamp[i] - stamp[i - 1]);
                std::fprintf(out, "\n");
                if (f) {
                    std::fflush(f);
                    std::fclose(f);
                }
                win.close();
            }
        }
    });
    return 0;
}
