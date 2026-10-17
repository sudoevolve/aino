// =============================================================================
//  aino.hpp  —  单头文件跨平台 C++ GUI 库
// -----------------------------------------------------------------------------
//  设计取向
//    · 思路来自 FLTK   : 无第三方依赖、事件驱动、空闲 0% CPU、静态链接极小
//    · 写法来自 eui-neo: 链式语义化组件 + 主题 token + hover/pressed 自动状态
//    · 视觉来自 shadcn : 扁平、极简、中性色板、8px 圆角、克制的描边
//
//  核心取舍
//    · 立即模式(immediate mode)：UI 每帧重建，状态由调用方的变量持有。
//      没有控件树、没有场景图、没有每控件状态机 —— 这是"体积小"的根本原因。
//      布局只有一层顺序切分器(Stack)，边画边切，不做约束求解。
//    · 几何图形自绘  ：圆角矩形/圆/线由本库 CPU 光栅化，带覆盖率抗锯齿
//                      （GDI/Qt 的 RoundRect 都不抗锯齿，shadcn 的圆角会糊）。
//    · 文字交给系统  ：Win32=GDI / Linux=X11 / macOS=CoreGraphics，
//                      天然支持中文与系统字体回退，不需要内嵌字体引擎。
//
//  用法（两个 TU 也行，一个 TU 最省事）
//      #define AINO_IMPLEMENTATION       // 只能在一个 .cpp 里定义
//      #include "aino.hpp"
//
//      int main() {
//          aino::Window win(880, 560, "aino");
//          win.run([&](aino::Ui& ui) {
//              ui.panel({24, 24, 400, 240});
//              aino::Stack col = ui.vbox({24, 24, 400, 240}, 10.0f, 18.0f);  // 间距 + 内边距
//              ui.heading("设置", col.next(28), aino::Style().left());
//              ui.input(&email, col.next(36), aino::Style().hint("you@example.com"));
//              ui.checkbox(&notify, "邮件通知", col.next(22));
//              if (ui.button("保存", col.next(36), aino::Style().primary())) save();
//          });
//      }
//
//  编译
//      MSVC : cl /std:c++17 /EHsc demo.cpp user32.lib gdi32.lib
//      GCC  : g++ -std=c++17 demo.cpp -lX11
//      Clang: clang++ -std=c++17 demo.cpp -framework Cocoa -framework CoreText
//
//  编码（重要，两个独立的问题）
//    1) 源码读取：本文件注释是中文 UTF-8 且带 BOM。BOM 是为了让 MSVC 不按系统
//       代码页(GBK)去读源码 —— 否则中文注释的多字节序列会吃掉落单字节，把后面
//       的代码"吞掉"，报出一堆莫名其妙的"uint8_t 缺少类型说明符"。
//       若你的工具链会剥掉 BOM，请加 /utf-8。
//    2) 字面量编码：没有 /utf-8 时，你自己代码里的中文字面量会被编成 GBK，
//       而库内部按 UTF-8 解。to_wide() 对此做了兼容（严格 UTF-8 解码失败则
//       回退 CP_ACP），所以加不加 /utf-8 都能正确显示中文。
//
//  作者: sudoevolve <sudoevolve@gmail.com>
//  许可: Apache-2.0（见仓库根 LICENSE）
// =============================================================================

#ifndef AINO_HPP
#define AINO_HPP

#define AINO_VERSION_MAJOR 0
#define AINO_VERSION_MINOR 2
#define AINO_VERSION_PATCH 2
#define AINO_VERSION "0.2.2"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <functional>

// -----------------------------------------------------------------------------
//  平台探测
// -----------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
#  define AINO_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#  define AINO_PLATFORM_MACOS 1
#elif defined(__linux__) || defined(__unix__)
#  define AINO_PLATFORM_X11 1
#else
#  error "aino: 未支持的平台"
#endif

namespace aino {

// =============================================================================
//  一、基础类型
// =============================================================================

// 8 位通道颜色。内部混合在 sRGB 空间做（GUI 不追求物理正确的线性混合）。
struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    Color() = default;
    Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255) : r(r_), g(g_), b(b_), a(a_) {}

    // 0xRRGGBB
    static Color hex(uint32_t rgb, uint8_t alpha = 255) {
        return Color(uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb), alpha);
    }
    static Color rgba(int r_, int g_, int b_, int a_ = 255) {
        return Color(uint8_t(r_), uint8_t(g_), uint8_t(b_), uint8_t(a_));
    }
    // 全透明，也叫"未设置"哨兵 —— Style 里用它表示"沿用主题默认值"
    static Color none() { return Color(0, 0, 0, 0); }
    static Color white() { return Color(255, 255, 255); }
    static Color black() { return Color(0, 0, 0); }
    static Color transparent() { return Color(0, 0, 0, 0); }

    bool is_none() const { return a == 0; }

    Color with_alpha(uint8_t alpha) const { return Color(r, g, b, alpha); }
    // 按倍率缩放当前 alpha（0..1）—— 名字刻意与 with_alpha 区分，避免 int 实参二义
    Color scaled_alpha(float factor) const {
        float v = float(a) * factor;
        if (v < 0.0f) v = 0.0f;
        if (v > 255.0f) v = 255.0f;
        return Color(r, g, b, uint8_t(v + 0.5f));
    }
    // alpha 直接给 0..255
    Color alpha255(int value) const {
        int v = value < 0 ? 0 : (value > 255 ? 255 : value);
        return Color(r, g, b, uint8_t(v));
    }

    // t=0 返回自身，t=1 返回 other
    Color mix(const Color& other, float t) const {
        if (t <= 0.0f) return *this;
        if (t >= 1.0f) return other;
        float inv = 1.0f - t;
        return Color(uint8_t(r * inv + other.r * t + 0.5f),
                     uint8_t(g * inv + other.g * t + 0.5f),
                     uint8_t(b * inv + other.b * t + 0.5f),
                     uint8_t(a * inv + other.a * t + 0.5f));
    }
    Color lighten(float amount) const {
        return Color(uint8_t(r + (255 - r) * amount + 0.5f),
                     uint8_t(g + (255 - g) * amount + 0.5f),
                     uint8_t(b + (255 - b) * amount + 0.5f), a);
    }
    Color darken(float amount) const {
        return Color(uint8_t(r * (1.0f - amount) + 0.5f),
                     uint8_t(g * (1.0f - amount) + 0.5f),
                     uint8_t(b * (1.0f - amount) + 0.5f), a);
    }
    // 感知亮度 0..1，用于自动判断该配深色还是浅色前景
    float luminance() const {
        return (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;
    }
    uint32_t rgb24() const { return (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b); }

    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const Color& o) const { return !(*this == o); }
};

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    Rect() = default;
    Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
    static Rect from_ltrb(float l, float t, float r, float b) { return Rect(l, t, r - l, b - t); }
    static Rect centered(float cx, float cy, float w, float h) {
        return Rect(cx - w * 0.5f, cy - h * 0.5f, w, h);
    }

    float left()   const { return x; }
    float top()    const { return y; }
    float right()  const { return x + w; }
    float bottom() const { return y + h; }
    float cx()     const { return x + w * 0.5f; }
    float cy()     const { return y + h * 0.5f; }

    bool empty() const { return w <= 0.0f || h <= 0.0f; }
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    bool contains(const Vec2& p) const { return contains(p.x, p.y); }
    bool intersects(const Rect& o) const {
        return !(o.x >= right() || o.right() <= x || o.y >= bottom() || o.bottom() <= y);
    }
    Rect inset(float d) const { return Rect(x + d, y + d, w - 2 * d, h - 2 * d); }
    Rect inset(float dx, float dy) const { return Rect(x + dx, y + dy, w - 2 * dx, h - 2 * dy); }
    Rect offset(float dx, float dy) const { return Rect(x + dx, y + dy, w, h); }
    Rect offset(const Vec2& d) const { return Rect(x + d.x, y + d.y, w, h); }
    Rect expand(float d) const { return inset(-d); }
    // 左侧切走 w0 宽的一条，剩下部分返回（用于做水平布局）
    Rect cut_left(float amount) { Rect r(x, y, amount, h); x += amount; w -= amount; return r; }
    Rect cut_right(float amount) { w -= amount; return Rect(x + w, y, amount, h); }
    Rect cut_top(float amount) { Rect r(x, y, w, amount); y += amount; h -= amount; return r; }
    Rect cut_bottom(float amount) { h -= amount; return Rect(x, y + h, w, amount); }
    // 水平 / 垂直内边距后的可用区
    Rect pad(float dx, float dy) const { return inset(dx, dy); }
};

enum class Align  { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom };

// 鼠标光标形状
enum class Cursor { Arrow, Hand, IBeam, ResizeH, ResizeV, Text };

// 键盘修饰键 / 常用键位（与平台无关的抽象码）
enum Key {
    KEY_NONE = 0,
    KEY_BACKSPACE = 8, KEY_TAB = 9, KEY_ENTER = 13, KEY_ESC = 27, KEY_SPACE = 32,
    KEY_DELETE = 127,
    KEY_LEFT = 0x1000, KEY_RIGHT, KEY_UP, KEY_DOWN,
    KEY_HOME, KEY_END, KEY_PAGEUP, KEY_PAGEDOWN,
    KEY_SHIFT, KEY_CTRL, KEY_ALT, KEY_SUPER,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_COUNT
};

// =============================================================================
//  二、主题 —— 色值直接对标 shadcn/ui 的 CSS 变量（zinc 基调）
// =============================================================================
struct Theme {
    // 语义色
    Color background;
    Color foreground;

    Color card;
    Color card_foreground;

    Color popover;
    Color popover_foreground;

    Color primary;
    Color primary_foreground;

    Color secondary;
    Color secondary_foreground;

    Color muted;
    Color muted_foreground;

    Color accent;
    Color accent_foreground;

    Color destructive;
    Color destructive_foreground;

    Color border;
    Color input;
    Color ring;

    // 度量 —— 对标 shadcn 的 --radius 与 tailwind 的间距刻度
    float radius      = 8.0f;   // 基准圆角 (rounded-md ~ 6px / rounded-lg ~ 8px)
    float radius_sm   = 4.0f;
    float radius_lg   = 10.0f;
    float radius_full = 9999.0f;

    float font_size   = 14.0f;  // 基准字号 (text-sm)
    float line_height = 20.0f;
    float control_h   = 36.0f;  // 标准控件高度 (h-9 = 36px)
    float control_h_sm= 32.0f;

    std::string font_family;    // 空 = 用平台默认无衬线字体
    std::string font_family_mono;

    // 值比较：Window::set_theme 用它判断"主题真的变了吗"。
    // 没有它，在帧回调里每帧调用 set_theme（很自然的写法：切主题后让本帧就用新配色）
    // 会无条件要帧 → 主循环再也回不到 WaitMessage → 空闲吃满一个核。
    bool same_as(const Theme& o) const {
        auto ceq = [](const Color& a, const Color& b) {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        };
        return ceq(background, o.background) && ceq(foreground, o.foreground)
            && ceq(card, o.card) && ceq(card_foreground, o.card_foreground)
            && ceq(popover, o.popover) && ceq(popover_foreground, o.popover_foreground)
            && ceq(primary, o.primary) && ceq(primary_foreground, o.primary_foreground)
            && ceq(secondary, o.secondary) && ceq(secondary_foreground, o.secondary_foreground)
            && ceq(muted, o.muted) && ceq(muted_foreground, o.muted_foreground)
            && ceq(accent, o.accent) && ceq(accent_foreground, o.accent_foreground)
            && ceq(destructive, o.destructive) && ceq(destructive_foreground, o.destructive_foreground)
            && ceq(border, o.border) && ceq(input, o.input) && ceq(ring, o.ring)
            && radius == o.radius && radius_sm == o.radius_sm
            && radius_lg == o.radius_lg && radius_full == o.radius_full
            && font_size == o.font_size && line_height == o.line_height
            && control_h == o.control_h && control_h_sm == o.control_h_sm
            && font_family == o.font_family && font_family_mono == o.font_family_mono;
    }

    // 浅色主题：白底 + 近黑前景，primary 是深灰(不是彩色)，这是 shadcn 的标志性做法
    static Theme light();
    // 深色主题：近黑底 + 近白前景
    static Theme dark();
};

// =============================================================================
//  三、Style —— 组件修饰器（可选参数），链式调用
//    每种组件会读取它关心的字段；未被设置的字段（Color::none / 负数值）
//    表示"沿用主题默认"。
// =============================================================================
struct Style {
    // --- 通用外观 ---
    Color bg      = Color::none();
    Color fg      = Color::none();
    Color border_color = Color::none();
    float border_width = -1.0f;   // <0 = 不描边（除非组件自身有默认描边）
    float radius  = -1.0f;
    float pad_x   = -1.0f;
    float pad_y   = -1.0f;
    float font_size = -1.0f;
    int   font_weight = -1;       // 100..900, -1 = 默认；>=600 走粗体
    bool  italic  = false;
    bool  disabled = false;

    // --- 组件语义变体（对应 shadcn 的 variant prop）---
    enum Variant { Default, Secondary, Outline, Ghost, Destructive, Link };
    int variant = -1;             // -1 = 由组件决定默认变体

    // --- 文本 ---
    Align  align  = Align::Left;
    VAlign valign = VAlign::Middle;
    bool   align_set  = false;    // 区分"用户显式指定"与"组件的默认值"
    bool   valign_set = false;
    bool   muted  = false;        // 用 muted_foreground 作为前景色
    std::string placeholder;      // input 用
    // 多行：`\n` 永远生效；wrap = true 时再按 box 宽度自动折行。
    // 折行规则：CJK 逐字断，西文优先退到最近的空格 —— 没有空格才硬断。
    bool  wrap = false;
    float line_gap = 0.0f;        // 行距附加量（0 = 直接用字体的 line_height）

    // 显式要求"这个组件自己会一直动"。
    // 目前只有 skeleton 读它。默认 false 是刻意的：aino 的头号承诺是"没有动画就
    // 空闲 0% CPU"，所以"持续 repaint"必须由调用方点名要求，不能由组件偷偷加。
    bool  animated = false;

    // --- 交互 ---
    float press_scale = 1.0f;     // 按压缩放，1.0 = 不缩放

    // -------- 链式修饰 --------
    Style& color(const Color& c) { fg = c; return *this; }
    Style& text_color(const Color& c) { fg = c; return *this; }
    Style& background(const Color& c) { bg = c; return *this; }
    Style& fill(const Color& c) { bg = c; return *this; }
    Style& stroke(float w, const Color& c) { border_width = w; border_color = c; return *this; }
    Style& corner(float r) { radius = r; return *this; }
    Style& padded(float x, float y) { pad_x = x; pad_y = y; return *this; }
    Style& padding(float v) { pad_x = v; pad_y = v; return *this; }
    Style& font(float size) { font_size = size; return *this; }
    Style& weight(int w) { font_weight = w; return *this; }
    Style& bold() { font_weight = 700; return *this; }
    Style& semi_bold() { font_weight = 600; return *this; }
    Style& medium() { font_weight = 500; return *this; }
    Style& normal_weight() { font_weight = 400; return *this; }
    Style& italicized() { italic = true; return *this; }
    Style& left()   { align = Align::Left;   align_set = true; return *this; }
    Style& center() { align = Align::Center; align_set = true; return *this; }
    Style& right()  { align = Align::Right;  align_set = true; return *this; }
    Style& top()    { valign = VAlign::Top;    valign_set = true; return *this; }
    Style& middle() { valign = VAlign::Middle; valign_set = true; return *this; }
    Style& bottom() { valign = VAlign::Bottom; valign_set = true; return *this; }
    Style& muted_text() { muted = true; return *this; }
    Style& hint(const std::string& h) { placeholder = h; return *this; }
    Style& off() { disabled = true; return *this; }
    Style& scale_on_press(float s) { press_scale = s; return *this; }
    Style& wrap_text(bool v = true) { wrap = v; return *this; }
    Style& line_spacing(float g) { line_gap = g; return *this; }
    Style& animate(bool v = true) { animated = v; return *this; }

    // 语义变体
    Style& primary()     { variant = Default;     return *this; }
    Style& secondary()   { variant = Secondary;   return *this; }
    Style& outline()     { variant = Outline;     return *this; }
    Style& ghost()       { variant = Ghost;       return *this; }
    Style& destructive() { variant = Destructive; return *this; }
    Style& link()        { variant = Link;        return *this; }
};

// =============================================================================
//  四、输入状态 —— 每帧刷新，立即模式读取它
// =============================================================================
struct Input {
    Vec2 mouse;                 // 逻辑坐标（已除以 DPI 缩放）
    Vec2 mouse_prev;
    Vec2 mouse_delta;
    float wheel = 0.0f;         // 本帧滚轮增量，向上为正

    bool down[3]     = {false, false, false};   // 左中右是否按住
    bool pressed[3]  = {false, false, false};   // 本帧按下
    bool released[3] = {false, false, false};   // 本帧抬起
    bool double_click = false;
    int  click_count = 0;

    // 键盘
    bool key_down[KEY_COUNT] = {};
    bool key_pressed[KEY_COUNT] = {};
    // 每个键**按下那一刻**的修饰键状态。为什么需要单独存：同一批消息里
    // Ctrl 的 WM_KEYUP 可能排在 WM_KEYDOWN('Z') 之后、但在这帧之前被处理掉，
    // 于是帧里读 ctrl() 已经是 false —— Ctrl+Z 就收不到了（实测快速按键必现）。
    bool key_ctrl[KEY_COUNT]  = {};
    bool key_shift[KEY_COUNT] = {};
    bool key_alt[KEY_COUNT]   = {};
    std::string text_input;     // 本帧输入的 UTF-8 文本（已过滤控制字符）
    bool shift() const { return key_down[KEY_SHIFT]; }
    bool ctrl()  const { return key_down[KEY_CTRL]; }
    bool alt()   const { return key_down[KEY_ALT]; }
    bool super() const { return key_down[KEY_SUPER]; }

    // 组合键判定：ctrl_of(k) = "本帧按下的 k 当时 Ctrl 是按住状态"。
    // 注意它**自带** key_pressed[k] 判定 —— 写成"key_pressed[k] && ctrl_of(k)"
    // 是多余的，而写成 ctrl_of(k) 单独用也不会误触发（曾经退回 live 状态，
    // 结果按住 Ctrl 的任意一帧都会当成按了 V，直接粘一段空剪贴板）。
    bool ctrl_of(int k)  const { return key_pressed[k] && key_ctrl[k]; }
    bool alt_of(int k)   const { return key_pressed[k] && key_alt[k]; }
    bool shift_of(int k) const { return key_pressed[k] ? key_shift[k] : key_down[KEY_SHIFT]; }
    bool any_ctrl_key()  const {
        for (int i = 0; i < KEY_COUNT; ++i) if (key_pressed[i] && key_ctrl[i]) return true;
        return false;
    }
    bool any_alt_key()   const {
        for (int i = 0; i < KEY_COUNT; ++i) if (key_pressed[i] && key_alt[i]) return true;
        return false;
    }

    Vec2 mouse_drag_start;
    bool dragging = false;
};

// =============================================================================
//  五、Stack —— 顺序布局器
//
//  立即模式没有控件树，"画完再排版"这条路走不通，只能边画边切。所以这里的
//  做法是：给一个矩形和一条主方向，让调用方按顺序把它切开。
//
//      Stack col = ui.vbox({x, y, w, h}, 10);   // 纵向，元素间距 10
//      ui.heading("标题", col.next(32));
//      ui.button("确定",  col.next(36));
//      ui.button("更多",  col.fill());          // 吃掉剩余
//
//  和手写 yy 游标相比，这里多出四件事：
//    · gap 只在两个元素之间生效（同 CSS 的 gap 语义），所以 fill() 拿到的是
//      精确的剩余空间，不会白少一个间距；
//    · next_frac / part 做比例与等分，不用自己算 (pw - 12) * 0.5；
//    · next_text 按文字真实像素宽度定宽 —— 系统字体引擎可以直接量，这是
//      "宽度自适应"能做而"高度自适应"不能做的原因；
//    · 子布局可以嵌套，父子之间继承 gap。
//
//  刻意不做：内容自适应高度、自动换行、网格、约束求解。它们要么需要两趟
//  遍历，要么需要尺寸缓存依赖图，与"单头文件 + 110KB"的目标直接冲突。
//  布局器本身是纯值语义（一个 Rect + 一个方向 + 一个间距），不注册任何
//  状态，所以嵌套/丢弃/复制都没有代价。
// =============================================================================
class Ui;

// -----------------------------------------------------------------------------
//  ScrollScope —— 一个滚动视口的句柄（`ui.scroll()` 的返回值）
//
//  滚动只有一件事：**一个视口 + 一段内容高度**。虚拟列表不是另一套东西，
//  只是"内容高度 = 行数 × 行高"的特例。所以这里只有一种句柄，通用与虚拟
//  共用：同一份偏移记忆（按 box 记忆）、同一根滚动条、同一套裁剪。
//
//  句柄析构时自动收尾（弹裁剪、还原布局偏移、画滚动条）—— 于是不需要
//  begin/end 手工配对；提前想收尾就调 close()（可重复调用）。
// -----------------------------------------------------------------------------
struct ScrollScope {
    Ui*      ui         = nullptr;
    uint64_t id         = 0;
    Rect     box;
    float    content_h  = 0.0f;
    float    max_offset = 0.0f;
    float    offset     = 0.0f;     // 本帧生效的偏移
    bool     active     = false;

    ScrollScope() = default;
    ScrollScope(const ScrollScope&) = delete;
    ScrollScope& operator=(const ScrollScope&) = delete;
    ScrollScope(ScrollScope&& o) noexcept { *this = std::move(o); }
    ScrollScope& operator=(ScrollScope&& o) noexcept {
        if (this == &o) return *this;
        ui = o.ui; id = o.id; box = o.box; content_h = o.content_h;
        max_offset = o.max_offset; offset = o.offset; active = o.active;
        o.active = false; o.ui = nullptr;
        return *this;
    }
    ~ScrollScope();

    void  close();              // 收尾（幂等；也可等析构）
    void  content(float h);     // 改内容高度（先排后量的场景）
    void  to(float v);          // 程序化滚动
    float y() const { return offset; }
};

struct Stack {
    Ui*   ui   = nullptr;
    Rect  area;                  // 剩余可用区
    float gap  = 0.0f;           // 元素间距

    Stack() = default;
    Stack(Ui* u, const Rect& box, bool vertical, float gap_)
        : ui(u), area(box), gap(gap_), vertical_(vertical) {}

    bool  vertical() const { return vertical_; }
    // 剩余区域 —— 已结算"待垫的 gap"，即"下一个元素的起始位置"；只读不消费
    Rect  rest()     const;
    float remain()   const { const Rect r = rest(); return vertical_ ? r.h : r.w; }
    bool  valid()    const { return area.w > 0.0f && area.h > 0.0f; }

    // ---- 沿主方向切走一块 ----
    Rect next(float size);                  // 固定尺寸；超出剩余时收窄，不溢出
    Rect next_frac(float fraction);         // 按剩余空间的比例
    Rect next_text(const std::string& s, float extra = 0.0f,
                   float font_size = 0.0f, int weight = -1);   // 按文字宽度 + 额外留白
    Rect fill();                            // 吃掉剩余全部
    void skip(float amount);                // 只推进游标，不产出

    // ---- 不改游标：把剩余区等分成 count 份，取第 index 份（份间留 gap）----
    Rect part(int index, int count) const;

    // ---- 在指定区域里开子布局；g < 0 表示继承当前 gap ----
    Stack vbox(const Rect& box, float g = -1.0f) const;
    Stack hbox(const Rect& box, float g = -1.0f) const;
    Stack vbox(float size, float g = -1.0f);    // 切一块并立刻开子布局
    Stack hbox(float size, float g = -1.0f);

private:
    bool vertical_ = true;       // true = 沿 y 推进
    bool pending_  = false;      // 已有元素产出，下一个之前要垫一个 gap
    void advance(float d);
};

// =============================================================================
//  五之二、动画与变换
//
//  动画：本库**没有**常驻动画系统（那会让空闲 CPU 从 0% 变 100%），只有两种
//  明确的东西：
//    · tween()  —— 目标驱动的补间。目标一变就从当前显示值重新起跑，跑完自动
//                   停止要帧；停下来的那一刻起 CPU 回到 0。
//    · smooth() —— 指数平滑的纯函数，适合跟随鼠标这类"没有终点"的量。
//  两者都不注册任何东西、不依赖控件树 —— 立即模式的本分。
//
//  变换：push_transform / pop_transform 叠加一个仿射变换（平移 → 旋转 → 缩放
//  作用于局部坐标）。它只影响**绘制**，不影响布局与命中判定：布局仍然算逻辑
//  坐标，"看起来变了"和"点得到"是两件事（CSS transform 也是这个语义）。
// =============================================================================
using Easing = float (*)(float);

namespace easing {
inline float linear(float t) { return t; }
inline float out_cubic(float t) { const float u = 1.0f - t; return 1.0f - u * u * u; }
inline float out_quint(float t) { const float u = 1.0f - t; return 1.0f - u * u * u * u * u; }
inline float in_out_cubic(float t) {
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - 4.0f * (1.0f - t) * (1.0f - t) * (1.0f - t);
}
inline float out_expo(float t) {
    return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
}
// 轻微过冲（1.0 之后回弹一点点），做"弹一下"的强调动效用
inline float out_back(float t) {
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}
} // namespace easing

// 仿射变换参数。语义与 CSS transform 一致：
//   p' = translate + rotate(rotate°) * scale * (p - origin)
struct Transform {
    Vec2  translate{0.0f, 0.0f};   // 逻辑单位的平移
    float scale = 1.0f;            // 缩放（绕 origin）
    float rotate = 0.0f;           // 旋转角度，**度**，顺时针为正（屏幕坐标）
    Vec2  origin{0.0f, 0.0f};      // 缩放/旋转的中心（逻辑坐标）
};

// =============================================================================
//  六、Ui —— 每帧的绘制与组件入口
// =============================================================================
class Ui {
public:
    Ui();
    ~Ui();

    // ---------------- 帧生命周期（由 Window::run 调用，通常不用手动调）-------
    void begin_frame(float width, float height, float dt, float dpi_scale);
    void end_frame();

    // ---------------- 环境查询 ----------------
    const Input& input() const;
    const Theme& theme() const;
    void  set_theme(const Theme& t);
    float dpi_scale() const;              // 例如 1.5
    float width() const;                  // 逻辑宽
    float height() const;                 // 逻辑高
    float time() const;                   // 秒
    float frame_dt() const;

    // 请求下一帧再重绘一次（动画用）。默认事件驱动，静止时不重绘。
    void repaint();
    // 声明"接下来 N 秒内持续动画"，让主循环从阻塞模式切到连续帧模式
    void set_animating(bool on);
    bool animating() const;

    void set_cursor(Cursor c);
    Cursor cursor() const;

    // 焦点与剪贴板（输入框用；剪贴板由平台后端提供）
    void* focused_id() const;
    void  set_focus(void* id);

    // ---------------- 图元 ----------------
    void clear(const Color& c);
    void fill_rect(const Rect& r, const Color& c, float radius = 0.0f);
    void stroke_rect(const Rect& r, const Color& c, float radius = 0.0f, float width = 1.0f);
    void fill_circle(float cx, float cy, float radius, const Color& c);
    // 月牙（图标用）：圆 (cx,cy,r) 减去圆 (cut_cx,cut_cy,cut_r)。
    // **不是**用背景色挖洞 —— 减法是几何上的，底下是什么色都不会穿帮。
    void fill_crescent(float cx, float cy, float radius,
                       float cut_cx, float cut_cy, float cut_radius, const Color& c);
    void draw_line(float x0, float y0, float x1, float y1, const Color& c, float width = 1.0f);
    // 画文本；Style 只读 align/valign/font_size/font_weight/bold
    void draw_text(const std::string& text, const Rect& box, const Color& c, const Style& st = Style());
    // 度量
    float text_width(const std::string& text, float font_size, int weight = -1) const;
    float line_height(float font_size) const;
    // 按 st.wrap / '\n' 的真实折行结果算出这段文字要占多高（逻辑单位）。
    // 和 text_width 对称：aino 不做"高度自适应"，但高度是**可以量**的 ——
    // 折行规则和执行绘制时用的是同一套（layout_wrapped_lines），不是估算。
    // 所以给组件留高度时可以 width = text_width(...)、height = text_height(...)。
    float text_height(const std::string& text, float width, const Style& st = Style()) const;
    // alert 在给定宽度下需要多高（含内边距与标题行）。给 alert 留高度就用它。
    float alert_height(const std::string& title, const std::string& description,
                       float width, const Style& st = Style()) const;

    // 渐变 / 阴影（可选，不依赖即可）
    void drop_shadow(const Rect& r, float radius, float blur, const Color& c, float dy = 1.0f);

    // ---------------- 动画 ----------------
    // 目标驱动补间：duration 秒内从"当前显示值"过渡到 target，缓动为 e。
    //   float t = ui.tween(make_id("w", rect), open ? 240.0f : 0.0f, 0.24f);
    //   ui.fill_rect(Rect(x, y, t, 4), ui.theme().primary);
    // 运行期间自动要帧；到达目标后不再要帧（空闲 CPU 依旧 0%）。
    float tween(uint64_t id, float target, float duration = 0.22f, Easing e = easing::out_cubic);
    Color tween_color(uint64_t id, const Color& target, float duration = 0.22f,
                      Easing e = easing::out_cubic);
    // 纯函数：指数平滑，和帧率无关（rate 越大跟得越紧）。没有终点时用它。
    float smooth(float current, float target, float rate = 14.0f) const;
    // 显隐淡入淡出：visible 为 true 归 1，false 归 0，返回本帧的值（0..1）
    float fade(uint64_t id, bool visible, float duration = 0.18f, Easing e = easing::out_cubic);

    // ---------------- 变换 ----------------
    // 压入一个仿射变换，作用范围到对应的 pop_transform()。
    // 只影响绘制（几何 + 文字），不影响布局坐标与命中判定。
    // 支持嵌套：内层在外层的局部坐标里再变换一次。
    void push_transform(const Transform& t);
    void pop_transform();
    Transform transform() const;            // 当前累计变换（近似值，R/S/T 已合成）
    bool  transformed() const;              // 当前是否有非单位变换

    // ---------------- 组件 ----------------
    // 文本类
    void label(const std::string& text, const Rect& box, const Style& st = Style());
    void heading(const std::string& text, const Rect& box, const Style& st = Style());
    void subheading(const std::string& text, const Rect& box, const Style& st = Style());
    void muted_text(const std::string& text, const Rect& box, const Style& st = Style());
    void code_text(const std::string& text, const Rect& box, const Style& st = Style());

    // 容器类
    void panel(const Rect& box, const Style& st = Style());          // 卡片 / 面板
    void card(const Rect& box, const Style& st = Style());           // panel 的别名语义
    void separator(const Rect& box, const Style& st = Style());      // 水平或垂直细线
    void badge(const std::string& text, const Rect& box, const Style& st = Style());
    void progress(float value, const Rect& box, const Style& st = Style());   // value 0..1
    void skeleton(const Rect& box, const Style& st = Style());       // 占位骨架（默认静态，.animate() 才呼吸）

    // 交互类 —— 返回 true 表示本帧发生了"值变化"或"点击"
    bool button(const std::string& text, const Rect& box, const Style& st = Style());
    bool icon_button(const std::string& glyph, const Rect& box, const Style& st = Style());
    // 侧边栏条目：左对齐 + 选中底色。button 是居中的，拿它做导航一行行都会飘。
    bool nav_item(const std::string& label, bool active, const Rect& box, const Style& st = Style());
    bool input(std::string* value, const Rect& box, const Style& st = Style());
    bool checkbox(bool* value, const std::string& label, const Rect& box, const Style& st = Style());
    bool toggle(bool* value, const Rect& box, const Style& st = Style());
    bool slider(float* value, float min_value, float max_value, const Rect& box, const Style& st = Style());
    bool tabs(const std::vector<std::string>& labels, int* index, const Rect& box, const Style& st = Style());
    bool segmented(const std::vector<std::string>& items, int* index, const Rect& box, const Style& st = Style());
    bool select(const std::vector<std::string>& items, int* index, const Rect& box, const Style& st = Style());

    // --- 表单补充 ---
    // 单选组：纵向等分 box.h，每行一个圆点 + 标签
    bool radio_group(const std::vector<std::string>& items, int* index, const Rect& box, const Style& st = Style());
    // 数值步进器：[−] 数值 [+] 三段式，点击左右按钮 ±step
    bool stepper(float* value, float step, float min_value, float max_value, const Rect& box, const Style& st = Style());
    // 多行文本框：换行由 '\n' 决定，不做软换行（超宽的行横向裁掉）
    bool textarea(std::string* value, const Rect& box, const Style& st = Style());
    // 星级评分：点击第 i 颗星 → *value = i+1；再点当前值可清零
    bool rating(int* value, int max_stars, const Rect& box, const Style& st = Style());

    // --- 展示补充 ---
    void avatar(const std::string& text, const Rect& box, const Style& st = Style());   // 圆形头像，取首字
    void spinner(const Rect& box, const Style& st = Style());                           // 旋转指示器
    void alert(const std::string& title, const std::string& description, const Rect& box, const Style& st = Style());
    void breadcrumb(const std::vector<std::string>& items, const Rect& box, const Style& st = Style());
    void kbd(const std::string& text, const Rect& box, const Style& st = Style());       // 按键提示
    bool accordion(const std::string& title, bool* open, const Rect& box, const Style& st = Style());
    void table(const std::vector<std::string>& headers,
               const std::vector<std::vector<std::string>>& rows,
               const Rect& box, const Style& st = Style());
    void sparkline(const std::vector<float>& values, const Rect& box, const Style& st = Style());
    void bars(const std::vector<float>& values, const Rect& box, const Style& st = Style());

    // --- 浮层类（自己负责盖在其它组件之上）---
    // 悬停提示：鼠标进入 anchor 时在它上/下方弹一个气泡（帧末绘制）
    void tooltip(const Rect& anchor, const std::string& text, const Style& st = Style());
    // 下拉菜单：box 是触发按钮，点开后弹列表；选中某项时返回 true 并写入 *clicked_index
    bool menu(const std::string& label, const std::vector<std::string>& items,
              int* clicked_index, const Rect& box, const Style& st = Style());
    // 模态框：打开时铺满窗口的遮罩 + box 处的面板，返回"本帧是否处于打开态"。
    // 要在遮罩之上画自己的按钮，就紧接着 dialog() 之后画（此时已在遮罩之上）。
    bool dialog(const std::string& title, const std::string& body, bool* open,
                const Rect& box, const Style& st = Style());
    // 轻提示条（通常自己摆在右下角；生命周期由调用方用 bool 控制）
    void toast(const std::string& text, const Rect& box, const Style& st = Style());

    // 把绘制推迟到帧末（弹出层用 —— 保证它盖在其它组件之上）
    void deferred(std::function<void()> fn);

    // 布局辅助（立即模式下的轻量自动布局）
    Rect row(float height);            // 在"当前容器"里切一行
    Rect column(float width);
    void push_padding(float x, float y);
    void pop_padding();
    void begin_group(const Rect& box);
    void end_group();

    // 顺序布局器 —— 新代码优先用这两个，上面那组是早期接口（保留兼容）
    Stack vbox(const Rect& box, float gap = 0.0f, float pad = 0.0f);
    Stack hbox(const Rect& box, float gap = 0.0f, float pad = 0.0f);

    // 裁剪（几何与文字都会被限制在区域内）
    void push_clip(const Rect& r);
    void pop_clip();

    // ---- 滚动 ----
    // 只有一件事：**一个视口 + 一段内容高度**。虚拟列表不是第二个 API，只是
    // "内容高度 = 行数 × 行高"的特例：可见区间能 O(1) 算出来，于是不可见项连
    // 构建都跳过。两者共用同一份偏移（按 box 记忆）、同一根滚动条、同一套裁剪。
    //
    //   通用（内容高度自己给，内部随便排）：
    //     { auto sc = ui.scroll(viewport, content_h);
    //         ui.label(...); ui.row(24); ...
    //     }                                   // 析构自动收尾
    //
    //   虚拟（固定行高，只对可见行调回调）：
    //     int drawn = ui.scroll(list, 100000, 26.0f, [&](int i, const Rect& row) {
    //         ui.label("第 " + std::to_string(i + 1) + " 行", row);
    //     });
    //
    // 滚轮只在鼠标位于 box 内时生效；返回的 drawn 可以直接断言"只画了一屏"。
    ScrollScope scroll(const Rect& box, float content_h);
    template <class F>
    int scroll(const Rect& box, int count, float item_h, F&& draw) {
        ScrollScope sc = scroll(box, float(std::max(count, 0)) * item_h);
        int drawn = 0;
        if (count > 0 && item_h > 0.01f) {
            int i0 = int(std::floor(sc.offset / item_h));
            int i1 = int(std::ceil((sc.offset + box.h) / item_h)) - 1;
            if (i0 < 0) i0 = 0;
            if (i0 > count - 1) i0 = count - 1;
            if (i1 > count - 1) i1 = count - 1;
            for (int i = i0; i <= i1; ++i, ++drawn)
                draw(i, Rect(box.x, box.y + float(i) * item_h - sc.offset, box.w, item_h));
        }
        return drawn;
    }

    // 显式写法：和 scroll() 完全同一套实现，只是不返回句柄 —— 用 end_scroll() 收尾。
    // 老代码 `ui.begin_scroll(...); ...; ui.end_scroll();` 照旧成立。
    void  begin_scroll(const Rect& box, float content_h);
    void  end_scroll();                     // 关掉最内层（close() 的显式版）
    float scroll_y() const;                 // 当前偏移（0 = 顶部）
    void  scroll_to(float offset);          // 程序化滚动

    // 低层零件：自己算可见区间、自己画，只要偏移与滚动条
    int   scroll_visible(const Rect& box, int count, float item_h, int* first, int* last);
    void  scroll_bar(const Rect& box, float content_h);
    float scroll_offset(const Rect& box) const;
    void  scroll_to_index(const Rect& box, int index, float item_h, int count);

    struct Impl;
    Impl* impl = nullptr;
};

// =============================================================================
//  七、Window —— 窗口与主循环
// =============================================================================
class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    bool ok() const;                   // 创建是否成功
    Ui&  ui();

    void set_theme(const Theme& t);
    void set_title(const std::string& title);
    void set_size(int width, int height);
    int  width() const;
    int  height() const;
    float dpi_scale() const;
    bool should_close() const;
    void close();

    void request_repaint();

    // 常驻动画（spinner / 光标闪烁 / hover 插值）的重绘帧率上限，默认 60fps。
    // 软件光栅化下，一个"一直在动"的组件会让整窗每帧重画；窗口越大越贵。
    // 把它调到 30 或 20 能近乎线性地砍掉这部分 CPU（代价：动画略顿）。
    void set_anim_fps(float fps);

    // 主循环：事件驱动。有事件就处理，没事件就阻塞在内核等待 —— 空闲 0% CPU。
    // frame 回调只在"需要重绘"时被调用，所以它可以放心地重建整个 UI。
    void run(const std::function<void(Ui&)>& frame);

    // 单步：给需要自己控制循环的人用
    bool poll();                       // 处理一批消息，返回是否仍存活
    void render();

    // 剪贴板（input 组件的复制粘贴用）
    void set_clipboard(const std::string& text);
    std::string clipboard() const;

    struct Impl;
    Impl* impl = nullptr;
};

// =============================================================================
//  八、内置图标（用 Unicode 符号，免依赖；也可换成你自己的字体图标）
// =============================================================================
namespace icons {
    extern const char* check;
    extern const char* chevron_down;
    extern const char* chevron_right;
    extern const char* close;
    extern const char* plus;
    extern const char* minus;
    extern const char* search;
    extern const char* settings;
    extern const char* sun;
    extern const char* moon;
    extern const char* dot;
}

} // namespace aino

#endif // AINO_HPP

// =============================================================================
//                          实 现 部 分 开 始
// =============================================================================
#ifdef AINO_IMPLEMENTATION
#ifndef AINO_IMPLEMENTED
#define AINO_IMPLEMENTED

// -----------------------------------------------------------------------------
//  平台头文件（只在实现单元里引入，头文件本身保持干净）
// -----------------------------------------------------------------------------
#ifdef AINO_PLATFORM_WINDOWS
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <windowsx.h>
#elif defined(AINO_PLATFORM_X11)
#  include <X11/Xlib.h>
#  include <X11/Xutil.h>
#  include <X11/keysym.h>
#  include <X11/Xatom.h>
#  include <unistd.h>
#  include <time.h>
#elif defined(AINO_PLATFORM_MACOS)
#  include <ApplicationServices/ApplicationServices.h>
#  include <CoreText/CoreText.h>
#  include <objc/objc.h>
#  include <objc/message.h>
#  include <unistd.h>
#  include <time.h>
#endif

#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <unordered_map>

namespace aino {
namespace detail {

// =============================================================================
//  8.1  基础工具
// =============================================================================

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int   clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// FNV-1a 64 位：给立即模式的组件生成稳定 ID
inline uint64_t hash_string(const char* s) {
    uint64_t h = 1469598103934665603ull;
    while (s && *s) { h ^= uint8_t(*s++); h *= 1099511628211ull; }
    return h;
}
inline uint64_t hash_combine(uint64_t a, uint64_t b) {
    return a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2));
}
// 用组件名 + 矩形坐标合成 ID —— 立即模式里没有控件对象，ID 必须可复现
inline uint64_t make_id(const char* kind, const Rect& r, uint64_t extra = 0) {
    uint64_t h = hash_string(kind);
    int ix = int(r.x * 2.0f), iy = int(r.y * 2.0f);
    h = hash_combine(h, uint64_t(uint32_t(ix)) << 32 | uint32_t(iy));
    if (extra) h = hash_combine(h, extra);
    return h;
}

// -----------------------------------------------------------------------------
//  UTF-8 <-> 码点（文本编辑必须按码点走，否则中文会被截断）
// -----------------------------------------------------------------------------
inline int utf8_decode(const char* s, uint32_t* out) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
    if (p[0] < 0x80) { *out = p[0]; return 1; }
    if ((p[0] & 0xE0) == 0xC0) { *out = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu); return 2; }
    if ((p[0] & 0xF0) == 0xE0) { *out = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu); return 3; }
    if ((p[0] & 0xF8) == 0xF0) {
        *out = ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
        return 4;
    }
    *out = 0xFFFD; return 1;
}
inline int utf8_encode(uint32_t cp, char* out) {
    if (cp < 0x80) { out[0] = char(cp); return 1; }
    if (cp < 0x800) { out[0] = char(0xC0 | (cp >> 6)); out[1] = char(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = char(0xE0 | (cp >> 12)); out[1] = char(0x80 | ((cp >> 6) & 0x3F)); out[2] = char(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = char(0xF0 | (cp >> 18)); out[1] = char(0x80 | ((cp >> 12) & 0x3F));
    out[2] = char(0x80 | ((cp >> 6) & 0x3F)); out[3] = char(0x80 | (cp & 0x3F));
    return 4;
}
inline std::vector<uint32_t> utf8_codepoints(const std::string& s) {
    std::vector<uint32_t> cps;
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = 0;
        int n = utf8_decode(s.c_str() + i, &cp);
        cps.push_back(cp);
        i += size_t(n < 1 ? 1 : n);
    }
    return cps;
}
inline std::string utf8_from_codepoints(const std::vector<uint32_t>& cps, size_t from = 0, size_t count = SIZE_MAX) {
    std::string out;
    char buf[8];
    size_t end = (count == SIZE_MAX) ? cps.size() : std::min(cps.size(), from + count);
    for (size_t i = from; i < end; ++i) {
        int n = utf8_encode(cps[i], buf);
        out.append(buf, size_t(n));
    }
    return out;
}
inline int utf8_count(const std::string& s) { return int(utf8_codepoints(s).size()); }
// 取第 idx 个码点之前的字节偏移
inline size_t utf8_byte_offset(const std::string& s, int idx) {
    size_t i = 0; int k = 0;
    while (i < s.size() && k < idx) { uint32_t cp; i += size_t(utf8_decode(s.c_str() + i, &cp)); ++k; }
    return i;
}

// =============================================================================
//  8.2  像素画布 + CPU 光栅化
//     内存布局：4 字节/像素。Windows / X11 是 BGRA，macOS 是 RGBA。
// =============================================================================
#ifdef AINO_PLATFORM_MACOS
static const int CH_R = 0, CH_G = 1, CH_B = 2, CH_A = 3;
#else
static const int CH_B = 0, CH_G = 1, CH_R = 2, CH_A = 3;
#endif

// 小端机上"4 字节像素"可以整个当 uint32 写 —— 比逐通道赋值快得多，
// 而且是 BGRA / RGBA 都适用（通道只是字节偏移，不关心谁在前）。
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#  define AINO_BIG_ENDIAN 1
#else
#  define AINO_BIG_ENDIAN 0
#endif

// 把不透明色打包成整像素值（仅在 !AINO_BIG_ENDIAN 时用于快速填充）
inline uint32_t pack_opaque(const Color& c) {
    uint8_t b[4];
    b[CH_R] = c.r; b[CH_G] = c.g; b[CH_B] = c.b; b[CH_A] = 255;
    uint32_t v = 0;
    std::memcpy(&v, b, 4);
    return v;
}

struct Canvas {
    uint8_t* px = nullptr;
    int w = 0, h = 0;
    int stride = 0;                 // 字节
    void*  platform_dc = nullptr;   // Windows: HDC; macOS: CGContextRef; X11: Display*
    void*  platform_gc = nullptr;   // X11: GC

    // 裁剪区（物理像素）。clipped=false 时是整块画布。
    bool clipped = false;
    int clip_x0 = 0, clip_y0 = 0, clip_x1 = 0, clip_y1 = 0;

    inline void set_clip(int x0, int y0, int x1, int y1) {
        clipped = true;
        clip_x0 = clampi(x0, 0, w); clip_y0 = clampi(y0, 0, h);
        clip_x1 = clampi(x1, 0, w); clip_y1 = clampi(y1, 0, h);
    }
    inline void clear_clip() { clipped = false; }

    inline void blend(int x, int y, const Color& c, float coverage) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        if (clipped && (x < clip_x0 || x >= clip_x1 || y < clip_y0 || y >= clip_y1)) return;
        float sa = (float(c.a) * (1.0f / 255.0f)) * coverage;
        if (sa <= 0.0015f) return;
        uint8_t* p = px + size_t(y) * size_t(stride) + size_t(x) * 4u;
        if (sa >= 0.9985f) {
            p[CH_R] = c.r; p[CH_G] = c.g; p[CH_B] = c.b; p[CH_A] = 255;
            return;
        }
        float inv = 1.0f - sa;
        p[CH_R] = uint8_t(c.r * sa + p[CH_R] * inv + 0.5f);
        p[CH_G] = uint8_t(c.g * sa + p[CH_G] * inv + 0.5f);
        p[CH_B] = uint8_t(c.b * sa + p[CH_B] * inv + 0.5f);
        p[CH_A] = 255;   // 不透明窗口：alpha 恒为 255，避免与 GDI 文字合成时出问题
    }

    // 不透明色整行快填（尊重裁剪区）。常用于圆角矩形的实心内区。
    inline void fill_row_fast(int y, int x0, int x1, const Color& c) {
        if (y < 0 || y >= h) return;
        if (clipped) {
            if (y < clip_y0 || y >= clip_y1) return;
            if (x0 < clip_x0) x0 = clip_x0;
            if (x1 > clip_x1) x1 = clip_x1;
        }
        if (x0 < 0) x0 = 0;
        if (x1 > w) x1 = w;
        if (x1 <= x0) return;
        uint8_t* base = px + size_t(y) * size_t(stride);
        if (!AINO_BIG_ENDIAN) {
            uint32_t* p = reinterpret_cast<uint32_t*>(base) + size_t(x0);
            const uint32_t v = pack_opaque(c);
            for (int i = x1 - x0; i > 0; --i, ++p) *p = v;
            return;
        }
        uint8_t* p = base + size_t(x0) * 4u;
        for (int x = x0; x < x1; ++x, p += 4) {
            p[CH_R] = c.r; p[CH_G] = c.g; p[CH_B] = c.b; p[CH_A] = 255;
        }
    }

    // 覆盖率恒为 1 的整行填充。只有不透明色才能走 memcpy 式的快填 ——
    // 半透明色覆盖率 1 也还是"混合"，必须逐像素 blend，否则会把颜色写实。
    inline void fill_row_opaque(int y, int x0, int x1, const Color& c) {
        if (c.a == 255) { fill_row_fast(y, x0, x1, c); return; }
        if (y < 0 || y >= h) return;
        if (x0 < 0) x0 = 0;
        if (x1 > w) x1 = w;
        for (int x = x0; x < x1; ++x) blend(x, y, c, 1.0f);
    }

    void clear(const Color& c) {
        if (w <= 0 || h <= 0 || !px) return;
        // 先铺满第 0 行的 w 个像素，其余行整块拷贝 —— 3 MB 的 memcpy 比
        // 逐像素循环快一个量级（1080×700 从 ~3 ms 降到 ~0.2 ms）。
        uint8_t* row0 = px;
        if (!AINO_BIG_ENDIAN) {
            uint32_t* p = reinterpret_cast<uint32_t*>(row0);
            const uint32_t v = pack_opaque(c);
            for (int x = 0; x < w; ++x) p[x] = v;
        } else {
            for (int x = 0; x < w; ++x) {
                row0[size_t(x) * 4u + CH_R] = c.r; row0[size_t(x) * 4u + CH_G] = c.g;
                row0[size_t(x) * 4u + CH_B] = c.b; row0[size_t(x) * 4u + CH_A] = 255;
            }
        }
        const size_t row_bytes = size_t(w) * 4u;
        for (int y = 1; y < h; ++y)
            std::memcpy(px + size_t(y) * size_t(stride), row0, row_bytes);
    }};

// --- 圆角矩形在给定 y 处的横跨区间 -------------------------------------------
struct Span { float xl, xr; };

inline Span rrect_span(float y, const Rect& r, float rad) {
    Span s{r.x, r.right()};
    if (y < r.y || y > r.bottom()) { s.xl = 1.0f; s.xr = 0.0f; return s; }  // 空
    if (rad <= 0.0f) return s;
    rad = std::min(rad, std::min(r.w, r.h) * 0.5f);
    float top_center = r.y + rad;
    float bot_center = r.bottom() - rad;
    if (y < top_center) {
        float dy = top_center - y;
        float dx = rad - std::sqrt(std::max(0.0f, rad * rad - dy * dy));
        s.xl = r.x + dx; s.xr = r.right() - dx;
    } else if (y > bot_center) {
        float dy = y - bot_center;
        float dx = rad - std::sqrt(std::max(0.0f, rad * rad - dy * dy));
        s.xl = r.x + dx; s.xr = r.right() - dx;
    }
    return s;
}

// 4 倍垂直子采样 × 水平解析覆盖 —— 半径 8px 的圆角能得到干净的边缘
static const int AINO_SS = 4;

// 在 outer 区域铺色，若 inner 非空则挖掉 inner（用于描边）
inline void raster_rrect(Canvas& cv, const Rect& r, const Color& c, float rad) {
    int y0 = (int)std::floor(r.y), y1 = (int)std::ceil(r.bottom());
    if (y1 > cv.h) y1 = cv.h;
    if (y0 < 0) y0 = 0;
    if (y1 <= y0) return;
    // 半径按短边折半，和 rrect_span 内部一致；先算好才能推出"必定被覆盖"的内区
    const float rad_eff = (rad > 0.0f) ? std::min(rad, std::min(r.w, r.h) * 0.5f) : 0.0f;

    for (int py = y0; py < y1; ++py) {
        if (cv.clipped && (py < cv.clip_y0 || py >= cv.clip_y1)) continue;
        float xls[AINO_SS], xrs[AINO_SS];
        float mn = 1e9f, mx = -1e9f;
        float inl = -1e9f, inr = 1e9f;      // 4 条子采样线都覆盖到的区间
        bool all_inside = true;             // 4 条子采样线是否都落在矩形内
        for (int s = 0; s < AINO_SS; ++s) {
            float sy = float(py) + (float(s) + 0.5f) / float(AINO_SS);
            Span sp = rrect_span(sy, r, rad_eff);
            xls[s] = sp.xl; xrs[s] = sp.xr;
            // rrect_span 对"行在矩形外"返回空哨兵 (xl=1, xr=0)。哨兵会把
            // min/max 带偏，所以必须用「是否真落在矩形内」显式判定。
            if (!(sp.xr > sp.xl)) all_inside = false;
            if (sp.xl < mn) mn = sp.xl;
            if (sp.xr > mx) mx = sp.xr;
            if (sp.xl > inl) inl = sp.xl;
            if (sp.xr < inr) inr = sp.xr;
        }
        if (mx <= mn) continue;
        int px0 = std::max(int(std::floor(mn)), 0);
        int px1 = std::min(int(std::ceil(mx)), cv.w);
        if (px1 <= px0) continue;

        // ---- 实心内区：整段覆盖率恒为 1，整行快填 ----
        // 只有在 4 条子采样线都真落在矩形内时，[ceil(inl), floor(inr)) 才必定
        // 被完整覆盖；有任何一条在外（顶/底边缘行）就不能走这条快路径。
        // 这一条几乎吃掉了所有面积（面板/卡片内部、radius=0 的普通矩形全部命中），
        // 只有左右各 1~2 个边缘像素才需要走 4 倍子采样。
        const int ix0 = int(std::ceil(inl));
        const int ix1 = int(std::floor(inr));
        const int fx0 = std::max(ix0, px0);
        const int fx1 = std::min(ix1, px1);
        const bool band = all_inside && (fx1 > fx0);
        if (band) cv.fill_row_opaque(py, fx0, fx1, c);

        // ---- 两端边缘：逐像素 4 倍垂直子采样 × 水平解析覆盖 ----
        // 注意：两段 AA 区间必须互不相交、且合起来正好是 [px0,px1) 减掉实心带。
        auto edge = [&](int x) {
            float cov = 0.0f;
            const float a = float(x), b = float(x + 1);
            for (int s = 0; s < AINO_SS; ++s) {
                float ov = std::min(b, xrs[s]) - std::max(a, xls[s]);
                if (ov > 0.0f) cov += ov;
            }
            cov *= (1.0f / float(AINO_SS));
            if (cov > 0.0005f) cv.blend(x, py, c, clampf(cov, 0.0f, 1.0f));
        };
        const int ax1 = band ? fx0 : px1;      // 左段 [px0, ax1)
        for (int x = px0; x < ax1; ++x) edge(x);
        if (band) for (int x = fx1; x < px1; ++x) edge(x);
    }
}

// 描边：outer 覆盖 - inner 覆盖
// 同样有"恒 1 区"快路径：左右两条竖边里，coverage 恒为 1（外框整覆盖、内框不沾边），
// 一整条 1px 边框按逐像素 4 倍子采样跑会非常贵 —— 大面板的边框就是靠这条吃掉的。
// 注意 rrect_span 对"行在矩形外"返回空哨兵 (xl=1, xr=0)，直接用 min/max 会被带偏
// （内框在某行为空时 inr 会变成 0，右竖边下界退化成 0 → 整行被当成边框填满），
// 所以下面一律按「有效样本」分别求边界。
inline void raster_rrect_stroke(Canvas& cv, const Rect& r, const Color& c, float rad, float width) {
    float w = std::max(width, 0.5f);
    Rect inner = r.inset(w);
    float inner_rad = std::max(0.0f, rad - w);
    const bool has_inner = inner.w > 0.0f && inner.h > 0.0f;
    int y0 = (int)std::floor(r.y), y1 = (int)std::ceil(r.bottom());
    if (y1 > cv.h) y1 = cv.h;
    if (y0 < 0) y0 = 0;
    if (y1 <= y0) return;

    for (int py = y0; py < y1; ++py) {
        if (cv.clipped && (py < cv.clip_y0 || py >= cv.clip_y1)) continue;
        float ol[AINO_SS], orr[AINO_SS], il[AINO_SS], irr[AINO_SS];
        float mn = 1e9f, mx = -1e9f;
        bool outer_all = true;          // 4 条子采样线都落在外框内
        bool inner_all = has_inner;     // 4 条子采样线都落在内框内
        float oin_l = -1e9f, oin_r = 1e9f;   // 外框：4 线都覆盖到的区间
        float iin_l = -1e9f, iin_r = 1e9f;   // 内框：4 线都覆盖到的区间
        float inl_min = 1e9f, inr_max = -1e9f;   // 内框有效样本的 min 左 / max 右
        for (int s = 0; s < AINO_SS; ++s) {
            float sy = float(py) + (float(s) + 0.5f) / float(AINO_SS);
            Span so = rrect_span(sy, r, rad);
            const bool ok = (so.xr > so.xl);
            ol[s] = so.xl; orr[s] = so.xr;
            if (!ok) outer_all = false;
            if (so.xl < mn) mn = so.xl;
            if (so.xr > mx) mx = so.xr;
            if (ok) {
                if (so.xl > oin_l) oin_l = so.xl;
                if (so.xr < oin_r) oin_r = so.xr;
            }
            Span si = has_inner ? rrect_span(sy, inner, inner_rad) : Span{1.0f, 0.0f};
            const bool iok = has_inner && (si.xr > si.xl);
            il[s] = si.xl; irr[s] = si.xr;
            if (!iok) { inner_all = false; }
            else {
                if (si.xl < inl_min) inl_min = si.xl;
                if (si.xr > inr_max) inr_max = si.xr;
                if (si.xl > iin_l) iin_l = si.xl;
                if (si.xr < iin_r) iin_r = si.xr;
            }
        }
        if (mx <= mn) continue;
        int px0 = std::max(int(std::floor(mn)), 0);
        int px1 = std::min(int(std::ceil(mx)), cv.w);
        if (px1 <= px0) continue;

        // 恒覆盖（cov==1）的整段：[s0a,s1a) 左竖边、[s0b,s1b) 右竖边。
        // 若这一行根本没有有效的内框（内框为空，或描边宽到把内框吃掉了），
        // ci 恒为 0，此时只有"整条外框实心带"是恒 1 的，左右边缘仍要 AA。
        int s0a = 0, s1a = 0, s0b = 0, s1b = 0;
        bool has_a = false, has_b = false;
        if (outer_all) {
            const int oi0 = int(std::ceil(oin_l));      // 外框左边界：右侧 co==1
            const int oi1 = int(std::floor(oin_r));     // 外框右边界：左侧 co==1
            if (inl_min < 1e8f && inr_max > -1e8f) {
                // 左竖边：x+1 <= 内框最小的左边界 → ci==0
                const int ahi = int(std::floor(inl_min - 1.0f)) + 1;
                if (ahi > oi0) { s0a = std::max(px0, oi0); s1a = std::min(ahi, px1); has_a = s1a > s0a; }
                // 右竖边：x >= 内框最大的右边界 → ci==0
                const int blo = int(std::ceil(inr_max));
                if (oi1 > blo) { s0b = std::max(px0, blo); s1b = std::min(oi1, px1); has_b = s1b > s0b; }
            } else if (oi1 > oi0) {
                s0a = std::max(px0, oi0); s1a = std::min(oi1, px1); has_a = s1a > s0a;
            }
        }
        if (has_a) cv.fill_row_opaque(py, s0a, s1a, c);
        if (has_b) cv.fill_row_opaque(py, s0b, s1b, c);

        // 两条竖边之间是"外框内、内框内"→ 覆盖率恒为 0，整段跳过。
        // 只有 4 条子采样线都落在内框内时，ci 才可能恒为 1。
        int z0 = px0, z1 = px0;
        if (outer_all && inner_all) {
            z0 = std::max(px0, std::max(int(std::ceil(oin_l)), int(std::ceil(iin_l))));
            z1 = std::min(px1, std::min(int(std::floor(oin_r)), int(std::floor(iin_r))));
        }

        // 剩下的（左右 1~2 px 边缘 + 圆角带）才走 4 倍子采样；单次线性扫描，不重复混合
        auto edge = [&](int x) {
            float co = 0.0f, ci = 0.0f;
            const float a = float(x), b = float(x + 1);
            for (int s = 0; s < AINO_SS; ++s) {
                float ovo = std::min(b, orr[s]) - std::max(a, ol[s]);
                if (ovo > 0.0f) co += ovo;
                float ovi = std::min(b, irr[s]) - std::max(a, il[s]);
                if (ovi > 0.0f) ci += ovi;
            }
            co *= (1.0f / float(AINO_SS));
            ci *= (1.0f / float(AINO_SS));
            float cov = clampf(co, 0.0f, 1.0f) - clampf(ci, 0.0f, 1.0f);
            if (cov > 0.0005f) cv.blend(x, py, c, cov);
        };
        for (int x = px0; x < px1; ) {
            if (has_a && x >= s0a && x < s1a) { x = s1a; continue; }
            if (has_b && x >= s0b && x < s1b) { x = s1b; continue; }
            if (z1 > z0 && x >= z0 && x < z1) { x = z1; continue; }
            edge(x);
            ++x;
        }
    }
}

// 多边形填充（奇偶规则）—— 任意简单多边形，凹的也行，星形就靠它。
// 做法与 raster_rrect 完全一致：4 倍垂直子采样把"是否在内部"变成覆盖率，
// 每条扫描线先解析求出所有交点，再按奇偶配对成实心段。所以小尺寸下边缘
// 与圆角矩形一样干净，而实现只有 40 行、不引入任何新的数据结构。
inline void raster_poly(Canvas& cv, const std::vector<Vec2>& pts, const Color& c) {
    const size_t n = pts.size();
    if (n < 3 || c.a == 0) return;
    float ymin = pts[0].y, ymax = pts[0].y;
    for (size_t i = 1; i < n; ++i) {
        ymin = std::min(ymin, pts[i].y);
        ymax = std::max(ymax, pts[i].y);
    }
    const int y0 = std::max(0, int(std::floor(ymin)));
    const int y1 = std::min(cv.h, int(std::ceil(ymax)) + 1);
    if (y1 <= y0) return;

    std::vector<float> xs;            // 本子采样行的交点
    std::vector<Vec2>  spans;         // 配对后的实心段 (x=xl, y=xr)
    std::vector<float> cov;           // 本扫描线的水平覆盖累加
    for (int py = y0; py < y1; ++py) {
        spans.clear();
        float mn = 1e9f, mx = -1e9f;
        for (int s = 0; s < AINO_SS; ++s) {
            const float sy = float(py) + (float(s) + 0.5f) / float(AINO_SS);
            xs.clear();
            for (size_t i = 0; i < n; ++i) {
                const Vec2& a = pts[i];
                const Vec2& b = pts[(i + 1) % n];
                // 半开区间判定，避免顶点被重复计数（否则奇偶会翻转错）
                if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy)) {
                    const float t = (sy - a.y) / (b.y - a.y);
                    xs.push_back(a.x + t * (b.x - a.x));
                }
            }
            if (xs.size() < 2) continue;
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2) {
                if (xs[k + 1] <= xs[k]) continue;
                spans.push_back(Vec2(xs[k], xs[k + 1]));
                mn = std::min(mn, xs[k]);
                mx = std::max(mx, xs[k + 1]);
            }
        }
        if (mx <= mn) continue;
        const int px0 = std::max(0, int(std::floor(mn)));
        const int px1 = std::min(cv.w, int(std::ceil(mx)));
        if (px1 <= px0) continue;

        // 恒覆盖带：所有实心段都完整盖住 [x, x+1] 的那一段。
        // 覆盖率是"各段重叠量之和 / 子采样数"，所以被全部段完整覆盖时它恒为
        // max(1, 段数/4) —— 段数 ≥ 4（= 4 条子采样线都贡献了段）时它必然是 1，
        // 于是这一整段可以直接整行快填，不用逐像素累加再混合。
        // 条形图/面积填充的内部几乎全是这一段，是热点所在。
        int bx0 = px0, bx1 = px0;
        if (spans.size() >= size_t(AINO_SS)) {
            float cl = -1e9f, cr = 1e9f;
            for (const Vec2& sp : spans) {
                if (sp.x > cl) cl = sp.x;
                if (sp.y < cr) cr = sp.y;
            }
            if (cr > cl) {
                bx0 = std::max(px0, int(std::ceil(cl)));
                bx1 = std::min(px1, int(std::floor(cr - 1.0f)) + 1);
                if (bx1 > bx0) cv.fill_row_opaque(py, bx0, bx1, c);
            }
        }

        cov.assign(size_t(px1 - px0), 0.0f);
        // 有了快填带就只需要累加带外的像素，省掉绝大部分浮点乘加
        if (bx1 <= bx0) {
            for (const Vec2& sp : spans) {
                for (int x = px0; x < px1; ++x) {
                    const float ov = std::min(float(x + 1), sp.y) - std::max(float(x), sp.x);
                    if (ov > 0.0f) cov[size_t(x - px0)] += ov;
                }
            }
        } else {
            for (const Vec2& sp : spans) {
                for (int x = px0; x < bx0; ++x) {
                    const float ov = std::min(float(x + 1), sp.y) - std::max(float(x), sp.x);
                    if (ov > 0.0f) cov[size_t(x - px0)] += ov;
                }
                for (int x = bx1; x < px1; ++x) {
                    const float ov = std::min(float(x + 1), sp.y) - std::max(float(x), sp.x);
                    if (ov > 0.0f) cov[size_t(x - px0)] += ov;
                }
            }
        }
        const float inv = 1.0f / float(AINO_SS);
        for (int x = px0; x < px1; ) {
            if (bx1 > bx0 && x >= bx0 && x < bx1) { x = bx1; continue; }   // 已整段填过
            const float v = cov[size_t(x - px0)] * inv;
            if (v > 0.0005f) cv.blend(x, py, c, clampf(v, 0.0f, 1.0f));
            ++x;
        }
    }
}

// 生成一个正 n 角星的顶点（外/内半径交替），第一个尖朝正上方。
inline std::vector<Vec2> star_points(float cx, float cy, int spikes, float r_out, float r_in) {
    std::vector<Vec2> pts;
    if (spikes < 3) return pts;
    pts.reserve(size_t(spikes) * 2);
    const float pi  = 3.14159265358979f;
    const float step = pi / float(spikes);
    float ang = -pi * 0.5f;
    for (int i = 0; i < spikes * 2; ++i) {
        const float r = (i % 2 == 0) ? r_out : r_in;
        pts.push_back(Vec2(cx + std::cos(ang) * r, cy + std::sin(ang) * r));
        ang += step;
    }
    return pts;
}

// 圆：距离场近似（1px 过渡带）
inline void raster_circle(Canvas& cv, float cx, float cy, float radius, const Color& c) {
    int y0 = std::max(0, int(std::floor(cy - radius - 1)));
    int y1 = std::min(cv.h, int(std::ceil(cy + radius + 1)));
    for (int y = y0; y < y1; ++y) {
        float dy = (float(y) + 0.5f) - cy;
        float half = std::sqrt(std::max(0.0f, radius * radius - dy * dy));
        int x0 = std::max(0, int(std::floor(cx - half - 1)));
        int x1 = std::min(cv.w, int(std::ceil(cx + half + 1)));
        for (int x = x0; x < x1; ++x) {
            float dx = (float(x) + 0.5f) - cx;
            float d = std::sqrt(dx * dx + dy * dy);
            float cov = clampf(radius + 0.5f - d, 0.0f, 1.0f);
            if (cov > 0.0005f) cv.blend(x, y, c, cov);
        }
    }
}

// 月牙 = 圆 A 减去圆 B。
//
// 为什么单独做一个原语：常见画法是"先画实心圆，再用背景色画一个圆挖掉"——
// 那个"挖"的圆是**不透明的底色**，于是控件一换背景（悬停高亮、别的主题、
// 图片/渐变底）就露出一块错的色块，也就是"穿帮"。这里改成距离场的 CSG 差集：
//
//     dA = |p - A| - rA        （A 内部为负）
//     dB = |p - B| - rB        （B 内部为负）
//     d  = max(dA, -dB)        （A 且 非 B）
//
// 再做 1px 过渡带 → 每个像素只 blend 一次，**完全不需要知道背景色**。
// 代价是一次包围盒扫描（只扫 A 的框），和画一个实心圆一个量级。
inline void raster_crescent(Canvas& cv, float cx, float cy, float radius,
                            float cut_cx, float cut_cy, float cut_radius, const Color& c) {
    if (c.a == 0 || radius <= 0.0f) return;
    int y0 = std::max(0, int(std::floor(cy - radius - 1)));
    int y1 = std::min(cv.h, int(std::ceil(cy + radius + 1)));
    for (int y = y0; y < y1; ++y) {
        float dy = (float(y) + 0.5f) - cy;
        int x0 = std::max(0, int(std::floor(cx - radius - 1)));
        int x1 = std::min(cv.w, int(std::ceil(cx + radius + 1)));
        for (int x = x0; x < x1; ++x) {
            float dx = (float(x) + 0.5f) - cx;
            const float dA = std::sqrt(dx * dx + dy * dy) - radius;
            if (dA > 0.5f) continue;                       // 在 A 外面，省掉第二次开方
            float d = dA;
            if (cut_radius > 0.0f) {
                float kx = (float(x) + 0.5f) - cut_cx;
                float ky = (float(y) + 0.5f) - cut_cy;
                const float dB = std::sqrt(kx * kx + ky * ky) - cut_radius;
                d = std::max(dA, -dB);
            }
            float cov = clampf(0.5f - d, 0.0f, 1.0f);
            if (cov > 0.0005f) cv.blend(x, y, c, cov);
        }
    }
}

// 圆环 / 圆弧：一次遍历环的包围盒，用「径向距离」求覆盖率、「极角」求 alpha，
// 每个像素只 blend 一次 —— 于是根本不存在接缝。
//
// 为什么不用「N 段短线拼一圈」：raster_line 的两端是圆头，相邻段在接缝处相互
// 重叠，同一个像素被 blend 两次、颜色被叠深。段数越多接缝越密，最后看起来
// 就是一节一节的"拼接块"，弧越长越明显。而且 N 段 = N 次独立包围盒扫描，
// 成本也是 N 倍（实测拼 84 段比这版慢 8 倍）。
//
//   a_sweep <= 0  → 整圈（底环）。角度不参与计算，alpha 恒为 c.a。
//   a_sweep >  0  → 弧：从 a_start 起跨 a_sweep 弧度；两端各补一个半圆帽。
//   a_from        → 尾端（t=0）的 alpha 比例。
//   a_fade        → 尾部渐隐占弧长的比例（默认 1 = 整条弧线性渐变）。
//                   取 0.4 就是"尾巴 40% 淡出，前面 60% 恒定满色"——
//                   loading 环要的就是这个：尾巴柔和消失，弧身不再有长渐变带。
inline void raster_arc(Canvas& cv, float cx, float cy, float radius, float width,
                       const Color& c, float a_start, float a_sweep, float a_from,
                       float a_fade = 1.0f) {
    if (c.a == 0 || radius <= 0.0f) return;
    const float hw    = std::max(width, 0.5f) * 0.5f;
    const float reach = radius + hw + 1.0f;
    const int y0 = std::max(0, int(std::floor(cy - reach)));
    const int y1 = std::min(cv.h, int(std::ceil(cy + reach)));
    const int x0 = std::max(0, int(std::floor(cx - reach)));
    const int x1 = std::min(cv.w, int(std::ceil(cx + reach)));
    if (y0 >= y1 || x0 >= x1) return;

    const float TAU   = 6.2831853f;
    const bool  full  = a_sweep <= 0.0f;
    const float sweep = full ? TAU : std::min(a_sweep, TAU);
    float st = std::fmod(a_start, TAU);
    if (st < 0.0f) st += TAU;

    // 弧两端的圆心（补半圆帽用），以及"靠近端点"的角度窗口
    float cap0x = 0.0f, cap0y = 0.0f, cap1x = 0.0f, cap1y = 0.0f;
    float cap_win = 0.0f;
    if (!full) {
        cap0x = cx + std::cos(st) * radius;
        cap0y = cy + std::sin(st) * radius;
        cap1x = cx + std::cos(st + sweep) * radius;
        cap1y = cy + std::sin(st + sweep) * radius;
        cap_win = hw / std::max(radius, 0.001f) + 0.02f;
    }

    for (int y = y0; y < y1; ++y) {
        const float py = (float(y) + 0.5f) - cy;
        for (int x = x0; x < x1; ++x) {
            const float px = (float(x) + 0.5f) - cx;
            const float d  = std::sqrt(px * px + py * py);
            float cov = clampf(hw + 0.5f - std::fabs(d - radius), 0.0f, 1.0f);
            if (cov <= 0.0005f) continue;

            float t = 1.0f;                       // alpha 在弧上的插值位置 0..1
            if (!full) {
                float rel = std::atan2(py, px) - st;
                if (rel < 0.0f) rel += TAU;
                if (rel >= TAU) rel -= TAU;
                if (rel <= sweep) {
                    t = rel / sweep;
                    // 端点附近补圆帽：环带被角度切断是平头，加上圆形才能和
                    // 直立的"胶囊"端点看起来一致
                    if (rel <= cap_win || rel >= sweep - cap_win) {
                        const float ax = px - (cap0x - cx), ay = py - (cap0y - cy);
                        const float bx = px - (cap1x - cx), by = py - (cap1y - cy);
                        const float c0 = clampf(hw + 0.5f - std::sqrt(ax * ax + ay * ay), 0.0f, 1.0f);
                        const float c1 = clampf(hw + 0.5f - std::sqrt(bx * bx + by * by), 0.0f, 1.0f);
                        cov = std::max(cov, std::max(c0, c1));
                        if (c1 > c0) t = 1.0f;
                    }
                } else {
                    // 不在角度区间内：只有两端的半圆帽还能覆盖到
                    const float ax = px - (cap0x - cx), ay = py - (cap0y - cy);
                    const float bx = px - (cap1x - cx), by = py - (cap1y - cy);
                    const float c0 = clampf(hw + 0.5f - std::sqrt(ax * ax + ay * ay), 0.0f, 1.0f);
                    const float c1 = clampf(hw + 0.5f - std::sqrt(bx * bx + by * by), 0.0f, 1.0f);
                    if (c0 <= 0.0005f && c1 <= 0.0005f) continue;
                    cov = std::max(c0, c1);
                    t = (c1 > c0) ? 1.0f : 0.0f;
                }
            }

            Color cc = c;
            if (a_from < 1.0f) {
                // 把 t 压到 [0,a_fade] 区间里做插值：a_fade=1 时就是整弧线性，
                // a_fade=0.4 时前 60% 直接饱和到满色，只有尾巴在淡出。
                const float u = (a_fade >= 1.0f)
                              ? clampf(t, 0.0f, 1.0f)
                              : clampf(t / std::max(a_fade, 0.001f), 0.0f, 1.0f);
                cc.a = uint8_t(float(c.a) * (a_from + (1.0f - a_from) * u));
            }
            cv.blend(x, y, cc, cov);
        }
    }
}

// 拖尾环要逐像素求极角 + 余弦。直接调 atan2/cos 的话，128px 的 spinner 单帧
// 要 785µs —— 全部耗在这两个三角函数上（环带约 6000 像素 × 130ns）。
// 这两张表把单帧压到 ~230µs：
//   atan_  → atan(i/256)，i=0..256，0..π/4
//   cos_   → cos(i/1024 * 2π)，i=0..1024
// atan 用八象限折叠（|x|,|y| 先折到第一象限），误差 < 0.002 rad ≈ 0.12px。
struct RingLut {
    float atan_[257];
    float cos_[1025];
    RingLut() {
        for (int i = 0; i <= 256; ++i)  atan_[i] = std::atan(float(i) * (1.0f / 256.0f));
        for (int i = 0; i <= 1024; ++i) cos_[i]  = std::cos(float(i) * (6.2831853f / 1024.0f));
    }
};
inline const RingLut& ring_lut() { static const RingLut t; return t; }

// 整圈「拖尾环」：**没有端点**，亮度沿角度按周期函数循环。
//
// 为什么 spinner 不能用带端点的弧：端点无论做成圆头、平头还是渐隐，都是一个
// "断口"。转到起点时尾端重新长出来，视觉上就是断一下；圆头还会在环上留下
// 一个明显的"头"，看起来像两个圆截出来的月牙。
// 连续几何的正确解法是**不画端点**：整圈都画，用沿角度连续分布的 alpha 表示
// 方向。COS 型分布是周期的 —— 最亮处与最暗处首尾相接且数值相等，所以绕多少圈
// 都不会出现接缝或断口。
//
//   a_phase  最亮处所在的极角
//   a_min    最暗处的 alpha 比例。给 0 会让环在那一段彻底消失（看起来像缺口），
//            所以默认留一点底，环始终是完整的一圈。
inline void raster_ring_tail(Canvas& cv, float cx, float cy, float radius, float width,
                             const Color& c, float a_phase, float a_min) {
    if (c.a == 0 || radius <= 0.0f) return;
    const float hw    = std::max(width, 0.5f) * 0.5f;
    const float reach = radius + hw + 1.0f;
    const int y0 = std::max(0, int(std::floor(cy - reach)));
    const int y1 = std::min(cv.h, int(std::ceil(cy + reach)));
    const int x0 = std::max(0, int(std::floor(cx - reach)));
    const int x1 = std::min(cv.w, int(std::ceil(cx + reach)));
    if (y0 >= y1 || x0 >= x1) return;

    const float TAU = 6.2831853f;
    // Bayer 4x4：alpha 是 8bit 的，连续的渐变会被量化成一条条等值线（马赫带），
    // 小尺寸下看着就像"拼接的色块"。加 ±0.5 级的抖动把台阶打散成噪点 ——
    // 只在这条渐变路径上抖，不动 Canvas::blend 的通用路径。
    static const int bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    const RingLut& lut = ring_lut();

    for (int y = y0; y < y1; ++y) {
        const float py = (float(y) + 0.5f) - cy;
        for (int x = x0; x < x1; ++x) {
            const float px = (float(x) + 0.5f) - cx;
            const float d  = std::sqrt(px * px + py * py);
            const float cov = clampf(hw + 0.5f - std::fabs(d - radius), 0.0f, 1.0f);
            if (cov <= 0.0005f) continue;

            // 极角：八象限折叠后查表（见 RingLut 的注释），不调 atan2
            const float ax = std::fabs(px), ay = std::fabs(py);
            float a;
            if (ax >= ay) a = lut.atan_[int(ay / (ax + 1e-9f) * 256.0f)];
            else          a = 1.5707963f - lut.atan_[int(ax / (ay + 1e-9f) * 256.0f)];
            if (px < 0.0f) a = 3.14159265f - a;
            if (py < 0.0f) a = 6.2831853f - a;

            float rel = a - a_phase;
            if (rel < 0.0f) rel += TAU;
            int ci = int(rel * (1024.0f / TAU));
            if (ci > 1024) ci = 1024;
            const float cs = 0.5f + 0.5f * lut.cos_[ci];     // 1 在头，0 在正对面
            const float a01 = cs * cs;                       // 压一下，让亮区更集中
            const float af  = a_min + (1.0f - a_min) * a01;

            const int dith = bayer[((y & 3) << 2) | (x & 3)];
            Color cc = c;
            cc.a = uint8_t(clampf(af * float(c.a) + (float(dith) * 0.0625f - 0.5f), 0.0f, 255.0f));
            cv.blend(x, y, cc, cov);
        }
    }
}

// 线段：距离场 + 圆头，任意宽度都抗锯齿
inline void raster_line(Canvas& cv, float x0, float y0, float x1, float y1, const Color& c, float width) {
    float hw = std::max(width, 0.5f) * 0.5f;
    float ex = hw + 0.5f;
    int bx0 = std::max(0, int(std::floor(std::min(x0, x1) - ex)));
    int bx1 = std::min(cv.w, int(std::ceil(std::max(x0, x1) + ex)));
    int by0 = std::max(0, int(std::floor(std::min(y0, y1) - ex)));
    int by1 = std::min(cv.h, int(std::ceil(std::max(y0, y1) + ex)));
    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    for (int y = by0; y < by1; ++y) {
        for (int x = bx0; x < bx1; ++x) {
            float pxc = float(x) + 0.5f, pyc = float(y) + 0.5f;
            float t = len2 > 0.0001f ? clampf(((pxc - x0) * dx + (pyc - y0) * dy) / len2, 0.0f, 1.0f) : 0.0f;
            float qx = x0 + dx * t, qy = y0 + dy * t;
            float d = std::sqrt((pxc - qx) * (pxc - qx) + (pyc - qy) * (pyc - qy));
            float cov = clampf(hw + 0.5f - d, 0.0f, 1.0f);
            if (cov > 0.0005f) cv.blend(x, y, c, cov);
        }
    }
}

// 圆角矩形的"外侧有符号距离"（内部 <= 0，边界 = 0）。
// 这就是 SDF 的标准写法：先把点折到第一象限的"圆心矩形"上，再减半径。
inline float rrect_sdf(float px, float py, const Rect& r, float rad) {
    const float rr = std::min(rad, std::min(r.w, r.h) * 0.5f);
    const float qx = std::fabs(px - r.cx()) - (r.w * 0.5f - rr);
    const float qy = std::fabs(py - r.cy()) - (r.h * 0.5f - rr);
    const float mx = std::max(qx, 0.0f), my = std::max(qy, 0.0f);
    return std::sqrt(mx * mx + my * my) + std::min(std::max(qx, qy), 0.0f) - rr;
}

// 阴影：对"外扩 blur"的包围盒逐像素求到圆角矩形边界的有符号距离，
// 再用一条平方衰减求 alpha：alpha = A * (1 - d/blur)^2。
//
// 为什么不用"叠 N 层放大的圆角矩形"：blend 是**累积**的，靠近边缘的地方
// 十几层叠在同一个像素上。传进来的 alpha 是 90，实际渲出来接近 148 ——
// 比设计值深 60%，看上去就是一圈发黑、边界很硬的脏影。
// 距离场版本一次遍历就够，alpha 曲线直接可控，也不再随 blur 变化而变味。
inline void raster_shadow(Canvas& cv, const Rect& r, float rad, float blur, const Color& c, float dy) {
    if (blur <= 0.5f || c.a == 0) return;
    const Rect s = r.offset(0.0f, dy);
    const float A = float(c.a);
    const float inv_blur = 1.0f / blur;

    const int x0 = std::max(0, int(std::floor(s.x - blur)));
    const int x1 = std::min(cv.w, int(std::ceil(s.right() + blur)));
    const int y0 = std::max(0, int(std::floor(s.y - blur)));
    const int y1 = std::min(cv.h, int(std::ceil(s.bottom() + blur)));
    if (x0 >= x1 || y0 >= y1) return;

    for (int y = y0; y < y1; ++y) {
        const float pyc = float(y) + 0.5f;
        for (int x = x0; x < x1; ++x) {
            const float d = rrect_sdf(float(x) + 0.5f, pyc, s, rad);
            // d <= 0 在形状内部：给满值（面板会盖住它；半透明面板才看得见）。
            // d >= blur 在衰减范围外：什么都不画。
            if (d >= blur) continue;
            const float t = d <= 0.0f ? 1.0f : (1.0f - d * inv_blur);
            const float a = A * t * t;
            if (a < 0.6f) continue;
            cv.blend(x, y, c, t * t);   // coverage 是"衰减系数"，alpha 由 blend 里的 c.a 负责
        }
    }
}

// =============================================================================
//  8.3  文字渲染接口
//    几何图形我们自绘（要抗锯齿），但文字必须交给系统 —— 只有系统字体引擎
//    才能正确覆盖中日韩、阿拉伯文的字形与回退链，且不增加二进制体积。
// =============================================================================
struct TextRenderer {
    virtual ~TextRenderer() {}
    virtual void  init(float dpi_scale, const std::string& family, const std::string& mono) = 0;
    virtual float measure(const std::string& utf8, float pixel_size, int weight) = 0;
    virtual float ascent(float pixel_size, int weight) = 0;
    virtual float line_height(float pixel_size, int weight) = 0;
    // x / y_top 已是物理像素；y_top 是行盒顶部；clip 为物理像素裁剪区（w<0 表示不裁）
    virtual void  draw(Canvas& cv, const std::string& utf8, float x, float y_top,
                       float pixel_size, int weight, const Color& c, const Rect& clip) = 0;
    // 旋转文字：(cx, cy) 是**行盒中心**（物理像素），degrees 顺时针为正。
    // 默认实现退化成不旋转的 draw() —— 其它后端没实现旋转时至少不会缺字，
    // 只是位置会落在中心左上角那半边。GDI 后端用 lfEscapement 原生旋转。
    virtual void  draw_rot(Canvas& cv, const std::string& utf8, float cx, float cy,
                           float pixel_size, int weight, const Color& c, const Rect& clip,
                           float degrees) {
        (void)degrees;
        const float w = measure(utf8, pixel_size, weight);
        const float lh = line_height(pixel_size, weight);
        draw(cv, utf8, cx - w * 0.5f, cy - lh * 0.5f, pixel_size, weight, c, clip);
    }
    virtual void  release() = 0;
};

#ifdef AINO_PLATFORM_WINDOWS
// -----------------------------------------------------------------------------
//  Windows: GDI 文字
// -----------------------------------------------------------------------------
// 复用调用方缓冲，避免每次绘制文字都分配一个 wstring。
inline void to_wide_into(const std::string& s, std::wstring& out) {
    out.clear();
    if (s.empty()) return;
    // 库内部一律按 UTF-8 处理，但 MSVC 在没有 /utf-8 时会把源码里的中文字面量
    // 编成系统 ANSI（简中机器上是 GBK）。那种字节序列不是合法 UTF-8，严格模式
    // 会直接失败 —— 于是回退到 CP_ACP 再试一次。
    // 这样"加不加 /utf-8 都能正确显示"，是 drop-in 的前提；
    // 代价只是一次失败探测，命中 UTF-8 时完全不走回退分支。
    UINT cp = CP_UTF8;
    int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                   s.c_str(), int(s.size()), nullptr, 0);
    if (need <= 0) {
        cp = CP_ACP;
        need = MultiByteToWideChar(cp, 0, s.c_str(), int(s.size()), nullptr, 0);
        if (need <= 0) return;
    }
    out.resize(size_t(need));
    MultiByteToWideChar(cp, 0, s.c_str(), int(s.size()), &out[0], need);
}

inline std::wstring to_wide(const std::string& s) {
    std::wstring out;
    to_wide_into(s, out);
    return out;
}

// -----------------------------------------------------------------------------
//  选一个"看起来和系统一样"的字体族
//
//  踩过的坑：这里原来写死 "Microsoft YaHei UI"。中文看着没问题（雅黑本来就是
//  Windows 的中文界面字体），但**拉丁字母用的是雅黑的西文部分** —— 比系统 UI
//  的 Segoe UI 更宽、更圆，一眼就能看出"不是系统那个字体"。
//
//  Windows 自己的做法是：正文用 Segoe UI（Win11 是 Segoe UI Variable Text），
//  中文字形交给 GDI 的**字体链接**（注册表 SystemLink 里 Segoe UI 链到 msyh），
//  于是拉丁用 Segoe、中文用雅黑，混排时逐字替换、基线对齐。
//  我们照抄这个链：先试 Segoe UI Variable Text，退到 Segoe UI，
//  最后才去问系统自己的消息字体（SPI_GETNONCLIENTMETRICS）。
//
//  为什么不用 SPI 的结果当首选：它报的是**旧式控件**的字体，Win11 的资源管理器
//  和设置界面其实走 DirectWrite + Segoe UI Variable，两者字面并不一样。
// -----------------------------------------------------------------------------
inline bool font_face_installed(const wchar_t* face) {
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    std::wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
    bool found = false;
    HDC dc = GetDC(nullptr);
    if (dc) {
        EnumFontFamiliesExW(dc, &lf,
            [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM p) -> int {
                *reinterpret_cast<bool*>(p) = true;
                return 0;                       // 找到一个就够了
            },
            reinterpret_cast<LPARAM>(&found), 0);
        ReleaseDC(nullptr, dc);
    }
    return found;
}

inline std::string from_wide(const std::wstring& s) {
    if (s.empty()) return std::string();
    int need = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), int(s.size()), &out[0], need, nullptr, nullptr);
    return out;
}

inline std::string default_ui_font_family() {
    static const char* kCandidates[] = {
        "Segoe UI Variable Text",   // Win11 正文
        "Segoe UI Variable",        // Win11 旧命名
        "Segoe UI",                 // Win7+ 一定有
    };
    for (const char* c : kCandidates) {
        const std::wstring w = to_wide(c);
        if (font_face_installed(w.c_str())) return std::string(c);
    }
    // 兜底：问系统它自己的消息字体（简中机器上多半就是 Microsoft YaHei UI）
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)
        && ncm.lfMessageFont.lfFaceName[0]) {
        return from_wide(ncm.lfMessageFont.lfFaceName);
    }
    return "Microsoft YaHei UI";
}

class GdiTextRenderer : public TextRenderer {
public:
    void init(float dpi, const std::string& family, const std::string& mono) override {
        dpi_ = dpi <= 0.0f ? 1.0f : dpi;
        family_ = family.empty() ? default_ui_font_family() : family;
        mono_   = mono.empty() ? std::string("Consolas") : mono;
        metric_dc_ = CreateCompatibleDC(nullptr);
        SetBkMode(metric_dc_, TRANSPARENT);
    }
    void release() override {
        for (auto& kv : fonts_) if (kv.second) DeleteObject(kv.second);
        fonts_.clear();
        measure_cache_.clear();
        lh_cache_.clear();
        if (metric_dc_) { DeleteDC(metric_dc_); metric_dc_ = nullptr; }
    }

    float measure(const std::string& utf8, float size, int weight) override {
        if (utf8.empty()) return 0.0f;
        // 同一帧里同一段文字会被反复测量（居中/右对齐/省略号试排），跨帧更是
        // 完全重复。缓存掉这次 GDI 往返，命中率接近 100%。
        const uint64_t fk = font_key(size, weight);
        const uint64_t h = hash_key(fk, utf8);
        auto it = measure_cache_.find(h);
        if (it != measure_cache_.end() && it->second.first == utf8)
            return it->second.second;
        HFONT f = font(size, weight);
        HGDIOBJ old = SelectObject(metric_dc_, f);
        std::wstring& w = wide_scratch();
        to_wide_into(utf8, w);
        SIZE sz{0, 0};
        GetTextExtentPoint32W(metric_dc_, w.c_str(), int(w.size()), &sz);
        SelectObject(metric_dc_, old);
        if (measure_cache_.size() >= 2048) measure_cache_.clear();
        measure_cache_.emplace(h, std::make_pair(utf8, float(sz.cx)));
        return float(sz.cx);
    }

    float ascent(float size, int weight) override {
        TEXTMETRICW tm{};
        HFONT f = font(size, weight);
        HGDIOBJ old = SelectObject(metric_dc_, f);
        GetTextMetricsW(metric_dc_, &tm);
        SelectObject(metric_dc_, old);
        return float(tm.tmAscent);
    }
    float line_height(float size, int weight) override {
        const uint64_t fk = font_key(size, weight);
        auto it = lh_cache_.find(fk);
        if (it != lh_cache_.end()) return it->second;
        TEXTMETRICW tm{};
        HFONT f = font(size, weight);
        HGDIOBJ old = SelectObject(metric_dc_, f);
        GetTextMetricsW(metric_dc_, &tm);
        SelectObject(metric_dc_, old);
        const float v = float(tm.tmHeight + tm.tmExternalLeading);
        lh_cache_[fk] = v;
        return v;
    }

    void draw(Canvas& cv, const std::string& utf8, float x, float y_top,
              float size, int weight, const Color& c, const Rect& clip) override {
        if (utf8.empty()) return;
        HDC dc = reinterpret_cast<HDC>(cv.platform_dc);
        if (!dc) return;
        HFONT f = font(size, weight);
        HGDIOBJ old_font = SelectObject(dc, f);
        int old_mode = SetBkMode(dc, TRANSPARENT);
        COLORREF old_color = SetTextColor(dc, RGB(c.r, c.g, c.b));

        std::wstring& w = wide_scratch();
        to_wide_into(utf8, w);
        const int tx = int(std::floor(x + 0.5f)), ty = int(std::floor(y_top + 0.5f));
        if (clip.w >= 0.0f) {
            // ETO_CLIPPED 让 GDI 自己裁 —— 省掉 SaveDC/IntersectClipRect/RestoreDC
            // 三次 DC 往返（文字是每帧调用最多的图元，这三次很有感）。
            RECT rc{ int(std::floor(clip.x)), int(std::floor(clip.y)),
                     int(std::ceil(clip.right())), int(std::ceil(clip.bottom())) };
            ExtTextOutW(dc, tx, ty, ETO_CLIPPED, &rc, w.c_str(), UINT(w.size()), nullptr);
        } else {
            ExtTextOutW(dc, tx, ty, 0, nullptr, w.c_str(), UINT(w.size()), nullptr);
        }

        SetTextColor(dc, old_color);
        SetBkMode(dc, old_mode);
        SelectObject(dc, old_font);
    }

    // 旋转文字：GDI 用 lfEscapement 原生旋转（比"画到位图再采样"干净得多，
    // 边缘仍是灰度 AA，不会糊）。
    //   · 角度取负：屏幕 y 向下，而 lfEscapement 以 y 向上的逆时针为正，
    //     要让"顺时针为正"成立就得取反；
    //   · ExtTextOut 的参考点是**行盒左上角**，且该点在文字坐标系里定义，
    //     所以先把中心沿旋转后的 x/y 轴各退半宽半高，得到那个角。
    void draw_rot(Canvas& cv, const std::string& utf8, float cx, float cy,
                  float size, int weight, const Color& c, const Rect& clip, float degrees) override {
        if (utf8.empty()) return;
        HDC dc = reinterpret_cast<HDC>(cv.platform_dc);
        if (!dc) return;
        const float w  = measure(utf8, size, weight);
        const float lh = line_height(size, weight);
        const float rad = degrees * 3.14159265358979f / 180.0f;
        const float cs = std::cos(rad), sn = std::sin(rad);
        const float hw = w * 0.5f, hh = lh * 0.5f;
        const float ax = cx - (hw * cs - hh * sn);
        const float ay = cy - (hw * sn + hh * cs);

        const int esc10 = int(-degrees * 10.0f);
        HGDIOBJ old_font = SelectObject(dc, font(size, weight, esc10));
        int old_mode = SetBkMode(dc, TRANSPARENT);
        COLORREF old_color = SetTextColor(dc, RGB(c.r, c.g, c.b));

        std::wstring& ws = wide_scratch();
        to_wide_into(utf8, ws);
        RECT rc{ int(std::floor(clip.x)), int(std::floor(clip.y)),
                 int(std::ceil(clip.right())), int(std::ceil(clip.bottom())) };
        const UINT flags = clip.w >= 0.0f ? ETO_CLIPPED : 0;
        ExtTextOutW(dc, int(std::floor(ax + 0.5f)), int(std::floor(ay + 0.5f)),
                    flags, clip.w >= 0.0f ? &rc : nullptr, ws.c_str(), UINT(ws.size()), nullptr);

        SetTextColor(dc, old_color);
        SetBkMode(dc, old_mode);
        SelectObject(dc, old_font);
    }

private:
    static std::wstring& wide_scratch() {
        thread_local std::wstring b;
        return b;
    }
    static uint64_t hash_key(uint64_t fk, const std::string& s) {
        uint64_t h = 1469598103934665603ull ^ fk;
        for (uint8_t ch : s) { h ^= ch; h *= 1099511628211ull; }
        return h;
    }
    uint64_t font_key(float size, int weight, int esc10 = 0) const {
        int q = int(size * 4.0f + 0.5f);
        if (q < 4) q = 4;
        return (uint64_t(uint32_t(q)) << 32)
             | (uint64_t(uint32_t(esc10 & 0xFFF)) << 16)      // 旋转角度（1/10 度）
             | uint32_t(weight < 0 ? 400 : weight);
    }
    HFONT font(float size, int weight, int esc10 = 0) {
        const uint64_t key = font_key(size, weight, esc10);
        auto it = fonts_.find(key);
        if (it != fonts_.end()) return it->second;
        int px = int(std::floor(float(int32_t(key >> 32)) / 4.0f + 0.5f));
        HFONT hf = CreateFontW(-px, 0, esc10, esc10, weight < 0 ? FW_NORMAL : weight,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                               // 灰度 AA，不是 ClearType。试过 CLEARTYPE_QUALITY：
                               // 在这条"画进 32bpp DIB 再 BitBlt"的路径上，GDI 给出的
                               // 次像素边纹过重 —— 实测 21px 中文的最大通道差 174、
                               // 8.7% 的像素带明显橙蓝彩边，比系统的 ClearType 脏得多。
                               // 纯灰度反而干净。
                               ANTIALIASED_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, to_wide(family_).c_str());
        fonts_[key] = hf;
        return hf;
    }
    HDC metric_dc_ = nullptr;
    float dpi_ = 1.0f;
    std::string family_, mono_;
    std::unordered_map<uint64_t, HFONT> fonts_;
    std::unordered_map<uint64_t, std::pair<std::string, float>> measure_cache_;
    std::unordered_map<uint64_t, float> lh_cache_;
};
#endif // AINO_PLATFORM_WINDOWS

} // namespace detail
} // namespace aino

// 从这里开始回到 aino 的顶层：主题、图标、Ui、Window 都是公开 API 层级
namespace aino {

// Canvas / TextRenderer 在 detail 里实现，但它们是可替换的扩展点，所以提上来
using detail::Canvas;
using detail::TextRenderer;
using detail::utf8_codepoints;
using detail::utf8_from_codepoints;
using detail::utf8_count;
using detail::utf8_byte_offset;
using detail::clampf;
using detail::lerpf;

namespace detail {
// 平台后端的入口在下面才定义，这里先声明（Window 构造函数要用）
#ifdef AINO_PLATFORM_WINDOWS
void w32_create(Window* self, int w, int h, const std::string& title);
void w32_destroy(Window* self);
#elif defined(AINO_PLATFORM_X11)
void x11_create(Window* self, int w, int h, const std::string& title);
void x11_destroy(Window* self);
#elif defined(AINO_PLATFORM_MACOS)
void cocoa_create(Window* self, int w, int h, const std::string& title);
void cocoa_destroy(Window* self);
#endif
} // namespace detail

// =============================================================================
//  8.4  主题与图标
// =============================================================================
inline Theme Theme::light() {
    Theme t;
    t.background            = Color::hex(0xFFFFFF);
    t.foreground            = Color::hex(0x09090B);
    t.card                  = Color::hex(0xFFFFFF);
    t.card_foreground       = Color::hex(0x09090B);
    t.popover               = Color::hex(0xFFFFFF);
    t.popover_foreground    = Color::hex(0x09090B);
    t.primary               = Color::hex(0x18181B);
    t.primary_foreground    = Color::hex(0xFAFAFA);
    t.secondary             = Color::hex(0xF4F4F5);
    t.secondary_foreground  = Color::hex(0x18181B);
    t.muted                 = Color::hex(0xF4F4F5);
    t.muted_foreground      = Color::hex(0x71717A);
    t.accent                = Color::hex(0xF4F4F5);
    t.accent_foreground     = Color::hex(0x18181B);
    t.destructive           = Color::hex(0xEF4444);
    t.destructive_foreground= Color::hex(0xFAFAFA);
    t.border                = Color::hex(0xE4E4E7);
    t.input                 = Color::hex(0xE4E4E7);
    t.ring                  = Color::hex(0x18181B);
    return t;
}

inline Theme Theme::dark() {
    Theme t;
    t.background            = Color::hex(0x09090B);
    t.foreground            = Color::hex(0xFAFAFA);
    t.card                  = Color::hex(0x101012);   // 比背景微亮一点，卡片才有层次
    t.card_foreground       = Color::hex(0xFAFAFA);
    t.popover               = Color::hex(0x131316);
    t.popover_foreground    = Color::hex(0xFAFAFA);
    t.primary               = Color::hex(0xFAFAFA);
    t.primary_foreground    = Color::hex(0x18181B);
    t.secondary             = Color::hex(0x27272A);
    t.secondary_foreground  = Color::hex(0xFAFAFA);
    t.muted                 = Color::hex(0x27272A);
    t.muted_foreground      = Color::hex(0xA1A1AA);
    t.accent                = Color::hex(0x27272A);
    t.accent_foreground     = Color::hex(0xFAFAFA);
    t.destructive           = Color::hex(0xDC2626);
    t.destructive_foreground= Color::hex(0xFAFAFA);
    t.border                = Color::hex(0x27272A);
    t.input                 = Color::hex(0x2E2E33);
    t.ring                  = Color::hex(0xD4D4D8);
    return t;
}

namespace icons {
const char* check         = "\xE2\x9C\x93";              // ✓
const char* chevron_down  = "\xE2\x8C\x84";              // ⌄
const char* chevron_right = "\xE2\x80\xBA";              // ›
const char* close         = "\xE2\x9C\x95";              // ✕
const char* plus          = "+";
const char* minus         = "\xE2\x88\x92";              // −
const char* search        = "\xE2\x8C\x95";              // ⌕
const char* settings      = "\xE2\x9A\x99";              // ⚙
const char* sun           = "\xE2\x98\x80";              // ☀
const char* moon          = "\xE2\x98\xBE";              // ☾
const char* dot           = "\xE2\x80\xA2";              // •
} // namespace icons

namespace detail {
// 交互态配色：亮底压暗、暗底提亮 —— 两条路径都保证对比度单调变化
inline Color hover_color(const Color& c) { return c.luminance() > 0.5f ? c.darken(0.07f) : c.lighten(0.14f); }
inline Color press_color(const Color& c) { return c.luminance() > 0.5f ? c.darken(0.13f) : c.lighten(0.24f); }
} // namespace detail

// =============================================================================
//  8.5  Ui 内部状态
// =============================================================================
struct Ui::Impl {
    Theme theme = Theme::light();
    Input input;
    float width = 0.0f, height = 0.0f;
    float dt = 1.0f / 60.0f;
    float scale = 1.0f;
    float clock = 0.0f;
    bool  animating = false;
    Cursor cursor = Cursor::Arrow;

    Canvas* canvas = nullptr;
    TextRenderer* text = nullptr;

    // 立即模式交互状态（跨帧存活）
    uint64_t hot = 0;             // 本帧鼠标悬停的控件
    uint64_t active = 0;          // 正在被按住的控件
    uint64_t next_hot = 0;
    void* focus = nullptr;        // 当前获得键盘焦点的控件
    uint64_t focus_id = 0;
    bool  focus_consumed = false; // 本帧 Focus 是否已被某控件认领

    // 文本编辑状态
    int   caret = 0;              // 光标位置（码点索引）
    int   sel_anchor = -1;        // 选区锚点
    float caret_blink = 0.0f;

    // 组合键按下后的一小段时间内丢弃文本字符。真实 Ctrl+Z 会送 WM_CHAR(0x1A)
    // 并被后端过滤，但某些输入法/布局会送可打印字符 —— 那就变成"撤销一次顺手
    // 打了个 Z"。150ms 足够覆盖一次按键，又不会影响正常打字。
    float text_veto = 0.0f;

    // 撤销栈（每个编辑控件一份，按 id 记忆）
    struct EditSnap { std::string text; int caret = 0; int anchor = -1; };
    struct EditStack {
        std::vector<EditSnap> undo, redo;
        float last = -1e9f;       // 上次压栈时间（合并连续输入用）
        uint64_t kind = 0;
    };
    std::unordered_map<uint64_t, EditStack> edit_stack;

    // hover 等状态的插值动画表
    std::unordered_map<uint64_t, float> anim;
    // 常驻动画的**局部相位**（spinner 的转角、skeleton 的呼吸）。和 clock 的区别：
    // 只在"该动"的时候按 dt 累加，停下就冻结 —— 所以鼠标移开再移回来是
    // "接着动"，而不是跳到全局时钟对应的相位。
    std::unordered_map<uint64_t, float> phase;
    // 离散的跨帧状态（如 select 是否展开）
    std::unordered_map<uint64_t, float> flags;

    // ---- 文本折行缓存 ----
    // 折行是 O(字符数) 的逐字量宽。一段 20 万字的文档，每帧重排一次就是
    // 十几毫秒 —— 于是"大块文本"在立即模式里必然卡。这里把**整段**折行结果
    // 按 (文本地址, 长度, 宽度, 字号, 字重, wrap) 缓存起来，命中就是一次
    // 哈希 + 一次 memcmp。
    //
    // 为什么用地址当键：以内容做键要先把整串哈希一遍（20 万字 ≈ 200µs），
    // 每帧都付不起。地址在调用方的 string 不扩容时是稳定的，配合 entry 里
    // 存的原文本做 memcmp 校验，碰撞/复用地址都不会出错（只是退化成重排）。
    struct WrapEntry {
        std::string text;
        float max_w = 0.0f, pfs = 0.0f;
        int weight = 0;
        bool wrap = false;
        std::vector<std::string> lines;
        uint64_t used = 0;
        size_t bytes = 0;
    };
    std::unordered_map<uint64_t, WrapEntry> wrap_cache;
    size_t   wrap_bytes = 0;
    uint64_t wrap_clock = 0;
    size_t   wrap_evictions = 0;      // 统计：被淘汰的段数（探针用）

    // 单字宽度缓存：首次折行时每个码点只问一次 GDI。
    std::unordered_map<uint64_t, float> adv_cache;

    std::vector<std::function<void()>> defer;

    // 布局栈
    std::vector<Rect> group;
    std::vector<Vec2> pad;
    std::vector<Rect> clips;
    // begin_scroll 的嵌套上下文（支持滚动区套滚动区）
    struct ScrollCtx { uint64_t id = 0; Rect box; float offset = 0.0f; float max_offset = 0.0f; };
    std::vector<ScrollCtx> scroll_stack;
    float last_scroll = 0.0f;    // 最近一个滚动区的偏移（end_scroll 之后 scroll_y() 仍可读）
    Rect effective_clip = Rect(0, 0, -1, -1);   // 物理像素；w<0 表示不裁

    void recompute_clip() {
        if (clips.empty()) { effective_clip = Rect(0, 0, -1, -1); }
        else {
            Rect c = clips[0];
            for (size_t i = 1; i < clips.size(); ++i) {
                float x0 = std::max(c.x, clips[i].x), y0 = std::max(c.y, clips[i].y);
                float x1 = std::min(c.right(), clips[i].right()), y1 = std::min(c.bottom(), clips[i].bottom());
                c = Rect(x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0));
            }
            effective_clip = c;
        }
        if (canvas) {
            if (effective_clip.w < 0.0f) canvas->clear_clip();
            else canvas->set_clip(int(effective_clip.x), int(effective_clip.y),
                                  int(effective_clip.right()), int(effective_clip.bottom()));
        }
    }

    // 平台回调
    std::function<void()> request_repaint;
    std::function<void(const std::string&)> set_clipboard;
    std::function<std::string()> get_clipboard;
    // 把输入法的组合窗 / 候选窗挪到光标处（参数是**物理客户区**像素：
    // caret_x, 输入框上沿, 输入框下沿）。平台后端绑定；未绑定就什么都不做。
    std::function<void(float, float, float)> ime_pos;

    // 把逻辑坐标矩形转成物理像素矩形
    Rect phys(const Rect& r) const {
        return Rect(r.x * scale, r.y * scale, r.w * scale, r.h * scale);
    }
    float px(float v) const { return v * scale; }

    // ---- 仿射变换（只作用于绘制）----
    // x' = a*x + c*y + e ; y' = b*x + d*y + f   （之后统一乘 DPI 缩放）
    struct Affine { float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0; };
    Affine xf;
    std::vector<Affine> xf_stack;

    bool xf_identity() const {
        return xf.a == 1.0f && xf.b == 0.0f && xf.c == 0.0f && xf.d == 1.0f && xf.e == 0.0f && xf.f == 0.0f;
    }
    bool  xf_rotated() const { return xf.b != 0.0f || xf.c != 0.0f; }
    float xf_scale()   const { return std::sqrt(std::fabs(xf.a * xf.d - xf.b * xf.c)); }
    float xf_angle()   const { return std::atan2(xf.b, xf.a); }        // 弧度，顺时针

    Vec2 xf_map(float x, float y) const {
        return Vec2((xf.a * x + xf.c * y + xf.e) * scale,
                    (xf.b * x + xf.d * y + xf.f) * scale);
    }
    // 逻辑矩形 → 物理像素矩形。无旋转时精确，有旋转时返回包围盒（裁剪/阴影用）
    Rect xf_phys(const Rect& r) const {
        if (xf_identity()) return phys(r);
        if (!xf_rotated()) {
            const Vec2 p0 = xf_map(r.x, r.y), p1 = xf_map(r.right(), r.bottom());
            return Rect(p0.x, p0.y, p1.x - p0.x, p1.y - p0.y);
        }
        const Vec2 p0 = xf_map(r.x, r.y),       p1 = xf_map(r.right(), r.y);
        const Vec2 p2 = xf_map(r.right(), r.bottom()), p3 = xf_map(r.x, r.bottom());
        const float x0 = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
        const float x1 = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
        const float y0 = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
        const float y1 = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
        return Rect(x0, y0, x1 - x0, y1 - y0);
    }
    float xf_len(float v) const { return v * xf_scale() * scale; }

    // ---- 补间状态 ----
    struct Tween {
        float from = 0.0f, to = 0.0f;
        float t = 0.0f, dur = 0.2f;
        bool  init = false;
        Easing e = nullptr;
    };
    std::unordered_map<uint64_t, Tween> tweens;

    struct ColorTween {
        Color from{}, to{};
        float t = 0.0f, dur = 0.2f;
        bool  init = false;
        Easing e = nullptr;
    };
    std::unordered_map<uint64_t, ColorTween> color_tweens;

    float tween_now(const Tween& tw) const {
        Easing e = tw.e ? tw.e : easing::out_cubic;
        const float k = tw.dur > 0.0001f ? clampf(tw.t / tw.dur, 0.0f, 1.0f) : 1.0f;
        return tw.from + (tw.to - tw.from) * e(k);
    }
};

// -----------------------------------------------------------------------------
//  Ui 基础
// -----------------------------------------------------------------------------
inline Ui::Ui() : impl(new Impl()) {}
inline Ui::~Ui() { delete impl; impl = nullptr; }

inline void Ui::begin_frame(float w, float h, float dt, float dpi) {
    impl->width = w;
    impl->height = h;
    impl->dt = (dt > 0.0f && dt < 1.0f) ? dt : (1.0f / 60.0f);
    impl->scale = dpi > 0.0f ? dpi : 1.0f;
    impl->clock += impl->dt;
    impl->caret_blink += impl->dt;
    if (impl->text_veto > 0.0f) impl->text_veto -= impl->dt;
    if (impl->input.any_ctrl_key() || impl->input.any_alt_key()) impl->text_veto = 0.15f;
    impl->focus_consumed = false;
    impl->defer.clear();
    impl->hot = impl->next_hot;
    impl->next_hot = 0;
    if (impl->group.empty()) impl->group.push_back(Rect(0, 0, w, h));
    impl->group.resize(1);
    impl->group[0] = Rect(0, 0, w, h);
    impl->pad.resize(1);
    impl->clips.clear();
    impl->recompute_clip();
    impl->cursor = Cursor::Arrow;
}

inline void Ui::end_frame() {
    // 帧末统一绘制弹出层，保证它盖在所有常规组件之上
    for (size_t i = 0; i < impl->defer.size(); ++i) {
        if (impl->defer[i]) impl->defer[i]();
    }
    impl->defer.clear();

    // 清掉"本帧一次性"的输入事件
    Input& in = impl->input;
    for (int i = 0; i < 3; ++i) { in.pressed[i] = false; in.released[i] = false; }
    in.wheel = 0.0f;
    in.double_click = false;
    in.click_count = 0;
    in.text_input.clear();
    for (int i = 0; i < KEY_COUNT; ++i) in.key_pressed[i] = false;
    in.mouse_delta = Vec2(0, 0);
    in.mouse_prev = in.mouse;
}

inline void Ui::deferred(std::function<void()> fn) { impl->defer.push_back(std::move(fn)); }

inline const Input& Ui::input() const { return impl->input; }
inline const Theme& Ui::theme() const { return impl->theme; }
inline void  Ui::set_theme(const Theme& t) { impl->theme = t; }
inline float Ui::dpi_scale() const { return impl->scale; }
inline float Ui::width() const { return impl->width; }
inline float Ui::height() const { return impl->height; }
inline float Ui::time() const { return impl->clock; }
inline float Ui::frame_dt() const { return impl->dt; }
inline void  Ui::repaint() { if (impl->request_repaint) impl->request_repaint(); }
inline void  Ui::set_animating(bool on) { impl->animating = on; if (on) repaint(); }
inline bool  Ui::animating() const { return impl->animating; }
inline void  Ui::set_cursor(Cursor c) { impl->cursor = c; }
inline Cursor Ui::cursor() const { return impl->cursor; }
inline void* Ui::focused_id() const { return impl->focus; }
inline void  Ui::set_focus(void* id) { impl->focus = id; if (id) impl->caret_blink = 0.0f; }

// -----------------------------------------------------------------------------
//  hover 等状态的帧间插值（eui-neo 的 visualState 思路，这里做成通用小工具）
// -----------------------------------------------------------------------------
namespace detail {
inline float animate_value(Ui::Impl* im, uint64_t id, float target, float speed = 14.0f) {
    float& cur = im->anim[id];
    float d = target - cur;
    if (std::fabs(d) < 0.0015f) {
        if (cur != target) { cur = target; if (im->request_repaint) im->request_repaint(); }
        return cur;
    }
    cur += d * clampf(im->dt * speed, 0.0f, 1.0f);
    if (im->request_repaint) im->request_repaint();   // 动画没结束，继续要帧
    return cur;
}

struct Interact {
    bool hovered = false;
    bool pressed = false;    // 本帧刚按下
    bool held = false;       // 持续按住
    bool clicked = false;    // 本帧完成一次点击
    bool double_clicked = false;
};

inline Interact interact(Ui::Impl* im, uint64_t id, const Rect& r, bool enabled) {
    Interact res;
    const Input& in = im->input;
    bool inside = enabled && r.contains(in.mouse.x, in.mouse.y);
    if (inside) {
        res.hovered = true;
        if (im->active == 0) im->next_hot = id;
        if (in.pressed[0]) { im->active = id; res.pressed = true; }
    }
    if (im->active == id) {
        res.held = in.down[0];
        if (in.released[0]) {
            if (inside) { res.clicked = true; res.double_clicked = in.double_click; }
            im->active = 0;
        }
    }
    return res;
}

// 把已解析的交互态换算成"该用哪个颜色画"
inline Color state_fill(const Color& base, const Color& hover, const Color& press, float t_hover, float t_press) {
    Color c = base.mix(hover, t_hover);
    c = c.mix(press, t_press);
    return c;
}

// 圆角矩形的多边形顶点（逻辑坐标 → 经当前变换映射到物理像素）。
// 只在旋转时用：无旋转走 raster_rrect 的专用路径，快而且边缘质量更好。
inline std::vector<Vec2>& poly_scratch() {
    static thread_local std::vector<Vec2> v;
    return v;
}

inline void rrect_poly(const Ui::Impl* im, const Rect& r, float radius, std::vector<Vec2>& out) {
    out.clear();
    const float rad = std::min(std::max(radius, 0.0f), std::min(r.w, r.h) * 0.5f);
    auto pt = [&](float x, float y) { out.push_back(im->xf_map(x, y)); };
    if (rad <= 0.35f) {
        pt(r.x, r.y); pt(r.right(), r.y); pt(r.right(), r.bottom()); pt(r.x, r.bottom());
        return;
    }
    const int seg = 5;                       // 每角 5 段：小控件上已经看不出折线
    const float hp = 1.57079632679f;
    const float cx[4] = { r.right() - rad, r.right() - rad, r.x + rad, r.x + rad };
    const float cy[4] = { r.y + rad,       r.bottom() - rad, r.bottom() - rad, r.y + rad };
    const float a0[4] = { -hp, 0.0f, hp, 2.0f * hp };
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i <= seg; ++i) {
            const float ang = a0[k] + hp * float(i) / float(seg);
            pt(cx[k] + std::cos(ang) * rad, cy[k] + std::sin(ang) * rad);
        }
}

} // namespace detail

// =============================================================================
//  8.6  图元
// =============================================================================
inline void Ui::clear(const Color& c) {
    if (impl->canvas) impl->canvas->clear(c);
}

inline void Ui::fill_rect(const Rect& r, const Color& c, float radius) {
    if (!impl->canvas || c.a == 0 || r.empty()) return;
    // 有旋转 → 多边形（raster_rrect 只吃轴对齐矩形）
    if (impl->xf_rotated()) {
        detail::rrect_poly(impl, r, radius, detail::poly_scratch());
        detail::raster_poly(*impl->canvas, detail::poly_scratch(), c);
        return;
    }
    Rect p = impl->xf_phys(r);
    if (p.empty()) return;
    float rad = impl->xf_len(std::max(radius, 0.0f));
    // 半径大到覆盖整个矩形时退化成胶囊/圆
    rad = std::min(rad, std::min(p.w, p.h) * 0.5f);
    if (rad <= 0.35f) {
        // 无圆角：整行快填 + 上下边界按覆盖率补
        if (c.a == 255) {
            int y0 = std::max(0, int(std::floor(p.y)));
            int y1 = std::min(impl->canvas->h, int(std::ceil(p.bottom())));
            int x0 = int(std::floor(p.x)), x1 = int(std::ceil(p.right()));
            for (int y = y0; y < y1; ++y) {
                float cov_y = clampf(std::min(float(y + 1), p.bottom()) - std::max(float(y), p.y), 0.0f, 1.0f);
                if (cov_y >= 0.999f) {
                    impl->canvas->fill_row_fast(y, x0, x1, c);
                    // 左右边缘单独补
                    for (int x = x0; x < x1; ++x) {
                        if (x < p.x || x + 1 > p.right()) {
                            float cov = clampf(std::min(float(x + 1), p.right()) - std::max(float(x), p.x), 0.0f, 1.0f);
                            impl->canvas->blend(x, y, c, cov);
                        }
                    }
                } else {
                    for (int x = x0; x < x1; ++x) {
                        float cov_x = clampf(std::min(float(x + 1), p.right()) - std::max(float(x), p.x), 0.0f, 1.0f);
                        if (cov_x > 0.0f) impl->canvas->blend(x, y, c, cov_x * cov_y);
                    }
                }
            }
        } else {
            detail::raster_rrect(*impl->canvas, p, c, 0.0f);
        }
        return;
    }
    detail::raster_rrect(*impl->canvas, p, c, rad);
}

inline void Ui::stroke_rect(const Rect& r, const Color& c, float radius, float width) {
    if (!impl->canvas || c.a == 0 || r.empty()) return;
    const float w = std::max(impl->xf_len(width), 0.6f);
    if (impl->xf_rotated()) {
        // 描边 = 外轮廓 + 内轮廓，交给偶奇规则的扫描线填充（中间自然留空）
        std::vector<Vec2>& pts = detail::poly_scratch();
        detail::rrect_poly(impl, r, radius, pts);
        const float lw = w / std::max(impl->xf_scale() * impl->scale, 0.0001f);
        const Rect inner(r.x + lw, r.y + lw, std::max(r.w - lw * 2.0f, 0.0f), std::max(r.h - lw * 2.0f, 0.0f));
        if (inner.w > 0.0f && inner.h > 0.0f) {
            std::vector<Vec2> tmp;
            detail::rrect_poly(impl, inner, std::max(radius - lw, 0.0f), tmp);
            pts.insert(pts.end(), tmp.begin(), tmp.end());
        }
        detail::raster_poly(*impl->canvas, pts, c);
        return;
    }
    Rect p = impl->xf_phys(r);
    float rad = impl->xf_len(std::max(radius, 0.0f));
    rad = std::min(rad, std::min(p.w, p.h) * 0.5f);
    detail::raster_rrect_stroke(*impl->canvas, p, c, rad, w);
}

inline void Ui::fill_circle(float cx, float cy, float radius, const Color& c) {
    if (!impl->canvas || c.a == 0) return;
    const Vec2 p = impl->xf_map(cx, cy);
    detail::raster_circle(*impl->canvas, p.x, p.y, impl->xf_len(radius), c);
}

inline void Ui::fill_crescent(float cx, float cy, float radius,
                              float cut_cx, float cut_cy, float cut_radius, const Color& c) {
    if (!impl->canvas || c.a == 0) return;
    const Vec2 p = impl->xf_map(cx, cy);
    const Vec2 q = impl->xf_map(cut_cx, cut_cy);
    detail::raster_crescent(*impl->canvas, p.x, p.y, impl->xf_len(radius),
                            q.x, q.y, impl->xf_len(cut_radius), c);
}

inline void Ui::draw_line(float x0, float y0, float x1, float y1, const Color& c, float width) {
    if (!impl->canvas || c.a == 0) return;
    const Vec2 a = impl->xf_map(x0, y0);
    const Vec2 b = impl->xf_map(x1, y1);
    detail::raster_line(*impl->canvas, a.x, a.y, b.x, b.y, c, impl->xf_len(width));
}

inline void Ui::drop_shadow(const Rect& r, float radius, float blur, const Color& c, float dy) {
    if (!impl->canvas || c.a == 0) return;
    // 阴影走的是距离场，只吃轴对齐矩形。旋转时用包围盒 —— 形状不再是"跟着转"，
    // 但至少不会凭空消失（这一点在文档里写明了）。
    detail::raster_shadow(*impl->canvas, impl->xf_phys(r), impl->xf_len(radius),
                          impl->xf_len(blur), c, impl->xf_len(dy));
}

// -----------------------------------------------------------------------------
//  动画
// -----------------------------------------------------------------------------
inline float Ui::tween(uint64_t id, float target, float duration, Easing e) {
    if (!e) e = easing::out_cubic;
    auto& tw = impl->tweens[id];
    if (!tw.init) {
        tw.init = true; tw.from = target; tw.to = target;
        tw.t = duration; tw.dur = duration; tw.e = e;
        return target;
    }
    if (target != tw.to) {
        // 从**当前显示值**重新起跑：目标中途改变不会跳变
        tw.from = impl->tween_now(tw);
        tw.to   = target;
        tw.t    = 0.0f;
        tw.dur  = std::max(duration, 0.0001f);
        tw.e    = e;
    }
    if (tw.t < tw.dur) {
        tw.t += impl->dt;
        if (tw.t >= tw.dur) tw.t = tw.dur;
        repaint();                          // 只有还在跑才要帧
    }
    return impl->tween_now(tw);
}

inline Color Ui::tween_color(uint64_t id, const Color& target, float duration, Easing e) {
    // 一个进度驱动四个通道。拆成 4 个独立 tween 会让各通道进度一致但**起点**
    // 被分别重置，颜色在中途会发灰发脏 —— 单状态才能保证插值路径是直线。
    if (!e) e = easing::out_cubic;
    auto& ct = impl->color_tweens[id];
    if (!ct.init) {
        ct.init = true; ct.from = target; ct.to = target;
        ct.t = duration; ct.dur = duration; ct.e = e;
        return target;
    }
    if (target.r != ct.to.r || target.g != ct.to.g || target.b != ct.to.b || target.a != ct.to.a) {
        const float k = ct.dur > 0.0001f ? clampf(ct.t / ct.dur, 0.0f, 1.0f) : 1.0f;
        const float ek = (ct.e ? ct.e : easing::out_cubic)(k);
        ct.from = Color(uint8_t(float(ct.from.r) + (float(ct.to.r) - float(ct.from.r)) * ek),
                        uint8_t(float(ct.from.g) + (float(ct.to.g) - float(ct.from.g)) * ek),
                        uint8_t(float(ct.from.b) + (float(ct.to.b) - float(ct.from.b)) * ek),
                        uint8_t(float(ct.from.a) + (float(ct.to.a) - float(ct.from.a)) * ek));
        ct.to  = target;
        ct.t   = 0.0f;
        ct.dur = std::max(duration, 0.0001f);
        ct.e   = e;
    }
    if (ct.t < ct.dur) {
        ct.t += impl->dt;
        if (ct.t >= ct.dur) ct.t = ct.dur;
        repaint();
    }
    const float k  = ct.dur > 0.0001f ? clampf(ct.t / ct.dur, 0.0f, 1.0f) : 1.0f;
    const float ek = (ct.e ? ct.e : easing::out_cubic)(k);
    auto lerp_ch = [&](uint8_t a, uint8_t b) {
        return uint8_t(clampf(float(a) + (float(b) - float(a)) * ek, 0.0f, 255.0f));
    };
    return Color(lerp_ch(ct.from.r, ct.to.r), lerp_ch(ct.from.g, ct.to.g),
                 lerp_ch(ct.from.b, ct.to.b), lerp_ch(ct.from.a, ct.to.a));
}

inline float Ui::smooth(float current, float target, float rate) const {
    // 指数逼近，和帧率无关：dt=1/60 与 dt=1/30 落到同一个位置
    const float k = 1.0f - std::exp(-std::max(rate, 0.0f) * impl->dt);
    const float v = current + (target - current) * k;
    return std::fabs(target - v) < 0.001f ? target : v;
}

inline float Ui::fade(uint64_t id, bool visible, float duration, Easing e) {
    return tween(id, visible ? 1.0f : 0.0f, duration, e);
}

// -----------------------------------------------------------------------------
//  变换
// -----------------------------------------------------------------------------
inline void Ui::push_transform(const Transform& t) {
    impl->xf_stack.push_back(impl->xf);

    const float rad = t.rotate * 3.14159265358979f / 180.0f;
    const float cs = std::cos(rad), sn = std::sin(rad);
    // 局部矩阵 L：p' = T(translate) * R * S * T(-origin) * p
    const float s = t.scale;
    Impl::Affine L;
    L.a = cs * s;  L.b = sn * s;
    L.c = -sn * s; L.d = cs * s;
    L.e = t.translate.x + t.origin.x - (L.a * t.origin.x + L.c * t.origin.y);
    L.f = t.translate.y + t.origin.y - (L.b * t.origin.x + L.d * t.origin.y);

    // 与父矩阵复合：M = P * L
    const Impl::Affine& P = impl->xf_stack.back();
    Impl::Affine M;
    M.a = P.a * L.a + P.c * L.b;
    M.b = P.b * L.a + P.d * L.b;
    M.c = P.a * L.c + P.c * L.d;
    M.d = P.b * L.c + P.d * L.d;
    M.e = P.a * L.e + P.c * L.f + P.e;
    M.f = P.b * L.e + P.d * L.f + P.f;
    impl->xf = M;
}

inline void Ui::pop_transform() {
    if (impl->xf_stack.empty()) { impl->xf = Impl::Affine(); return; }
    impl->xf = impl->xf_stack.back();
    impl->xf_stack.pop_back();
}

inline bool Ui::transformed() const { return !impl->xf_identity(); }

inline Transform Ui::transform() const {
    Transform t;
    t.scale   = impl->xf_scale();
    t.rotate  = impl->xf_angle() * 57.2957795f;
    t.translate = Vec2(impl->xf.e, impl->xf.f);
    return t;
}

inline float Ui::text_width(const std::string& text, float font_size, int weight) const {
    if (!impl->text || text.empty()) return 0.0f;
    float fs = font_size > 0.0f ? font_size : impl->theme.font_size;
    return impl->text->measure(text, impl->px(fs), weight < 0 ? 400 : weight) / impl->scale;
}

inline float Ui::line_height(float font_size) const {
    float fs = font_size > 0.0f ? font_size : impl->theme.font_size;
    if (!impl->text) return fs * 1.42f;
    return impl->text->line_height(impl->px(fs), 400) / impl->scale;
}

namespace detail {

// 第 i 个字节处那个字符占几个字节。非法字节按 1 前进 —— 宁可画错一个字，
// 也绝不允许死循环（这段代码吃的是用户输入）。
inline size_t utf8_step(const std::string& s, size_t i) {
    const unsigned char c = (unsigned char)s[i];
    size_t n = 1;
    if      ((c & 0xE0u) == 0xC0u) n = 2;
    else if ((c & 0xF0u) == 0xE0u) n = 3;
    else if ((c & 0xF8u) == 0xF0u) n = 4;
    if (i + n > s.size()) n = 1;
    return n;
}

inline bool is_break_space(unsigned char c) { return c == ' ' || c == '\t'; }

// 单个码点的宽度（带缓存）。折行时逐字量宽是热点：没有这层缓存，每个字
// 都要走一次 measure → 哈希 → 字符串比较（首次还要一次 GDI 往返）。
inline uint64_t glyph_key(float pfs, int weight, uint32_t cp) {
    const uint64_t q = uint64_t(uint32_t(int(pfs * 4.0f + 0.5f)));
    return (q << 40) ^ (uint64_t(uint32_t(weight & 0xFFFF)) << 24) ^ uint64_t(cp);
}

inline float glyph_adv(Ui::Impl* im, uint32_t cp, float pfs, int weight) {
    const uint64_t k = glyph_key(pfs, weight, cp);
    auto it = im->adv_cache.find(k);
    if (it != im->adv_cache.end()) return it->second;

    char buf[4];
    int n = 0;
    if (cp < 0x80) { buf[n++] = char(cp); }
    else if (cp < 0x800) {
        buf[n++] = char(0xC0 | (cp >> 6));
        buf[n++] = char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        buf[n++] = char(0xE0 | (cp >> 12));
        buf[n++] = char(0x80 | ((cp >> 6) & 0x3F));
        buf[n++] = char(0x80 | (cp & 0x3F));
    } else {
        buf[n++] = char(0xF0 | (cp >> 18));
        buf[n++] = char(0x80 | ((cp >> 12) & 0x3F));
        buf[n++] = char(0x80 | ((cp >> 6) & 0x3F));
        buf[n++] = char(0x80 | (cp & 0x3F));
    }
    const float w = im->text->measure(std::string(buf, size_t(n)), pfs, weight);
    if (im->adv_cache.size() > 8192) im->adv_cache.clear();   // 不可控输入，别无限长
    im->adv_cache.emplace(k, w);
    return w;
}

// 从 i 处解一个码点（不校验合法性，非法字节按单字节处理）
inline uint32_t utf8_decode(const std::string& s, size_t i) {
    const unsigned char c = (unsigned char)s[i];
    const size_t n = utf8_step(s, i);
    if (n == 1) return c;
    uint32_t cp = uint32_t(c) & (n == 2 ? 0x1F : (n == 3 ? 0x0F : 0x07));
    for (size_t k = 1; k < n; ++k) cp = (cp << 6) | (uint32_t((unsigned char)s[i + k]) & 0x3F);
    return cp;
}

// 切成"行"：先按 '\n' 分逻辑行；wrap 时再按 max_w 折行。
// 西文会退到本行最后一个空格处，不把单词劈两半（退回来的部分顺延下一行）；
// 一个字符本身就超宽时也必须吐出去，否则死循环。
inline void layout_wrapped_lines(TextRenderer* tr, const std::string& text,
                                 float max_w, float pfs, int weight, bool wrap,
                                 std::vector<std::string>& out, Ui::Impl* im = nullptr) {
    out.clear();
    if (!tr) return;
    const bool measure_width = (max_w > 1.0f);
    size_t begin = 0;
    for (;;) {
        const size_t nl  = text.find('\n', begin);
        const size_t end = (nl == std::string::npos) ? text.size() : nl;
        const std::string logical = text.substr(begin, end - begin);

        if (!wrap || !measure_width || logical.empty()) {
            out.push_back(logical);
        } else {
            std::string line;
            float line_w = 0.0f;                       // 用"逐字宽累加"代替整行 measure：
            size_t last_space = std::string::npos;     // 一次 O(n) 而不是每次重测整行
            size_t i = 0;
            while (i < logical.size()) {
                const size_t n = utf8_step(logical, i);
                const unsigned char first = (unsigned char)logical[i];
                const float cw = im ? glyph_adv(im, utf8_decode(logical, i), pfs, weight)
                                    : tr->measure(logical.substr(i, n), pfs, weight);
                if (!line.empty() && line_w + cw > max_w) {
                    if (is_break_space(first)) {
                        out.push_back(line);
                        line.clear(); line_w = 0.0f; last_space = std::string::npos;
                        i += n;                        // 行首的落单空格直接丢弃
                        continue;
                    }
                    if (last_space != std::string::npos && last_space > 0) {
                        out.push_back(line.substr(0, last_space));
                        line.erase(0, last_space + 1);
                        line_w = line.empty() ? 0.0f : tr->measure(line, pfs, weight);
                        last_space = std::string::npos;
                    } else {
                        out.push_back(line);
                        line.clear(); line_w = 0.0f;
                    }
                    continue;                          // i 不前进：这个字留给下一行
                }
                line.append(logical, i, n);
                line_w += cw;
                if (is_break_space(first)) last_space = line.size() - 1;
                i += n;
            }
            out.push_back(line);
        }
        if (nl == std::string::npos) break;
        begin = nl + 1;
    }
}

// 折行结果缓存。draw_text 和 text_height 都走这里 —— 两者本来就必须用同一套
// 规则，缓存顺带把"每帧重排整段"的成本压到一次查找。
//
// 内存有上限（默认 6 MB，环境变量 AINO_WRAP_BUDGET 可改，单位 KiB）：超了按
// 最久未用淘汰。一段 20 万字的中文大约 1.6 MB 行文本，够放 3~4 段，覆盖
// "正在看的文档 + 上一份文档"这种真实场景。
inline const std::vector<std::string>& wrap_layout(Ui::Impl* im, const std::string& text,
                                                   float max_w, float pfs, int weight, bool wrap) {
    static const std::vector<std::string> kEmpty;
    if (!im->text) return kEmpty;
    // 空串按直接折行的结果走（1 个空行），别在这里搞特殊 —— 否则
    // "缓存命中"和"直接折行"在边界上会给出不同的行数。
    if (text.empty()) { static const std::vector<std::string> kOne(1); return kOne; }

    // 键里放地址而不是内容哈希：内容哈希要遍历整串（20 万字 ≈ 200µs/帧），
    // 地址在调用方的 string 不扩容时是稳定的。命中后用 memcmp 兜底校验，
    // 所以地址被复用（临时 string 落在同一个栈地址）也不会返回错的行。
    uint64_t k = (uint64_t(reinterpret_cast<uintptr_t>(text.data())) >> 3) * 0x9E3779B97F4A7C15ull;
    k ^= uint64_t(text.size()) * 0xC2B2AE3D27D4EB4Full;
    k ^= uint64_t(uint32_t(int(max_w * 4.0f + 0.5f))) * 1315423911u;
    k ^= (uint64_t(uint32_t(int(pfs * 4.0f + 0.5f))) << 32);
    k ^= (uint64_t(uint32_t(weight & 0xFFFF)) << 9);
    k ^= wrap ? 1u : 0u;

    ++im->wrap_clock;
    auto it = im->wrap_cache.find(k);
    if (it != im->wrap_cache.end()) {
        Ui::Impl::WrapEntry& e = it->second;
        if (e.max_w == max_w && e.pfs == pfs && e.weight == weight && e.wrap == wrap
            && e.text.size() == text.size()
            && std::memcmp(e.text.data(), text.data(), text.size()) == 0) {
            e.used = im->wrap_clock;
            return e.lines;
        }
        im->wrap_bytes -= e.bytes;            // 过期条目直接换掉
        im->wrap_cache.erase(it);
    }

    Ui::Impl::WrapEntry e;
    layout_wrapped_lines(im->text, text, max_w, pfs, weight, wrap, e.lines, im);
    e.text   = text;
    e.max_w  = max_w;
    e.pfs    = pfs;
    e.weight = weight;
    e.wrap   = wrap;
    e.used   = im->wrap_clock;
    e.bytes  = e.text.size() + 64;
    for (const std::string& l : e.lines) e.bytes += l.size() + 40;   // 行内容 + string 头部

    static const size_t kBudget = [] {
        if (const char* v = std::getenv("AINO_WRAP_BUDGET")) {
            const long n = std::strtol(v, nullptr, 10);
            if (n > 0) return size_t(n) * 1024u;
        }
        return size_t(6u << 20);
    }();

    // 先腾地方再插入：保证"刚算出来的这段"不会一进去就把自己挤掉
    while (im->wrap_bytes + e.bytes > kBudget && !im->wrap_cache.empty()) {
        uint64_t oldest_key = 0, oldest = ~0ull;
        for (const auto& kv : im->wrap_cache)
            if (kv.second.used < oldest) { oldest = kv.second.used; oldest_key = kv.first; }
        auto o = im->wrap_cache.find(oldest_key);
        im->wrap_bytes -= o->second.bytes;
        im->wrap_cache.erase(o);
        ++im->wrap_evictions;
    }

    im->wrap_bytes += e.bytes;
    auto ins = im->wrap_cache.emplace(k, std::move(e));
    return ins.first->second.lines;
}

} // namespace detail

inline void Ui::draw_text(const std::string& text, const Rect& box, const Color& c, const Style& st) {
    if (!impl->canvas || !impl->text || text.empty() || c.a == 0) return;
    const float tscale = impl->xf_scale();
    const float fs = st.font_size > 0.0f ? st.font_size : impl->theme.font_size;
    const int weight = st.font_weight > 0 ? st.font_weight : 400;
    const float pfs = impl->px(fs) * tscale;      // 字体随变换缩放（不是把位图拉大）
    const float lh = impl->text->line_height(pfs, weight);
    const Rect pb = impl->xf_phys(box);

    // 文本一律裁到自己的盒子里。溢出永远是 bug —— 对话框正文横向爬出面板
    // 就是这么来的（只有 toast / input 少数几处自己 push_clip 了，其余全靠运气）。
    Rect clip = impl->effective_clip;
    if (clip.w >= 0.0f) {
        const float x0 = std::max(clip.x, pb.x), y0 = std::max(clip.y, pb.y);
        const float x1 = std::min(clip.right(), pb.right()), y1 = std::min(clip.bottom(), pb.bottom());
        clip = Rect(x0, y0, std::max(x1 - x0, 0.0f), std::max(y1 - y0, 0.0f));
    } else {
        clip = pb;
    }
    if (clip.w <= 0.0f || clip.h <= 0.0f) return;

    // 逻辑单位换算（旋转路径里按逻辑坐标定位，再映射中心点）
    const float inv = 1.0f / std::max(impl->scale * tscale, 0.0001f);

    // ---- 旋转路径 ----
    // 每一行按自己的中心点定位，交给 GDI 用 lfEscapement 原生旋转。
    // 多行也能转：行的排列方向在局部坐标系里仍然是竖着往下，整块一起转。
    if (impl->xf_rotated()) {
        const float angle = impl->xf_angle() * 57.2957795f;
        const std::vector<std::string>& lines =
            detail::wrap_layout(impl, text, impl->px(box.w) * tscale, pfs, weight, st.wrap);
        if (lines.empty()) return;
        const int   n      = int(lines.size());
        const float lh_log = lh * inv;
        const float step   = lh_log + st.line_gap;
        const float total  = step * float(n - 1) + lh_log;
        float y0;
        if (st.valign == VAlign::Middle)   y0 = box.cy() - total * 0.5f;
        else if (st.valign == VAlign::Top) y0 = box.y;
        else                               y0 = box.bottom() - total;
        for (int i = 0; i < n; ++i) {
            const std::string& ln = lines[size_t(i)];
            if (ln.empty()) continue;
            const float tw_log = impl->text->measure(ln, pfs, weight) * inv;
            float x = box.x;
            if (st.align == Align::Center)     x = box.cx() - tw_log * 0.5f;
            else if (st.align == Align::Right) x = box.right() - tw_log;
            const Vec2 ctr = impl->xf_map(x + tw_log * 0.5f, y0 + step * float(i) + lh_log * 0.5f);
            impl->text->draw_rot(*impl->canvas, ln, ctr.x, ctr.y, pfs, weight, c, clip, angle);
        }
        return;
    }

    // 快路径：单行 + 不换行 —— 绝大多数调用走这里，行为与从前逐字节一致。
    if (!st.wrap && text.find('\n') == std::string::npos) {
        const float tw = impl->text->measure(text, pfs, weight);
        float x = pb.x;
        if (st.align == Align::Center)     x = pb.cx() - tw * 0.5f;
        else if (st.align == Align::Right) x = pb.right() - tw;
        float y_top;
        if (st.valign == VAlign::Middle)      y_top = pb.cy() - lh * 0.5f;
        else if (st.valign == VAlign::Top)    y_top = pb.y;
        else                                  y_top = pb.bottom() - lh;
        impl->text->draw(*impl->canvas, text, x, y_top, pfs, weight, c, clip);
        return;
    }

    // 多行：先按 '\n' 切逻辑行，再（可选）按可用宽度折行。结果进缓存 ——
    // 静态文本每帧只是命中一次，不再重排整段。
    const std::vector<std::string>& lines =
        detail::wrap_layout(impl, text, pb.w, pfs, weight, st.wrap);
    if (lines.empty()) return;
    const float step = lh + impl->px(st.line_gap) * tscale;
    const int   n    = int(lines.size());
    const float total = step * float(n - 1) + lh;

    float y0;
    if (st.valign == VAlign::Middle)   y0 = pb.cy() - total * 0.5f;
    else if (st.valign == VAlign::Top) y0 = pb.y;
    else                               y0 = pb.bottom() - total;

    // 只画落在裁剪区里的行。滚动区的长文档里一屏通常只有几十行可见，
    // 但 lines 可能上万行；全量交给 GDI 是纯浪费（每行一次 UTF-8→UTF-16 转换）。
    int i0 = 0, i1 = n - 1;
    if (step > 0.01f) {
        i0 = detail::clampi(int(std::floor((clip.y - y0) / step)) - 1, 0, n - 1);
        i1 = detail::clampi(int(std::ceil((clip.bottom() - y0) / step)) + 1, 0, n - 1);
    }
    for (int i = i0; i <= i1; ++i) {
        const std::string& ln = lines[size_t(i)];
        if (ln.empty()) continue;
        const float tw = impl->text->measure(ln, pfs, weight);
        float x = pb.x;
        if (st.align == Align::Center)     x = pb.cx() - tw * 0.5f;
        else if (st.align == Align::Right) x = pb.right() - tw;
        impl->text->draw(*impl->canvas, ln, x, y0 + step * float(i), pfs, weight, c, clip);
    }
}

// -----------------------------------------------------------------------------
//  多行文字的实际高度 —— 和 draw_text 用同一套折行规则，所以是"量出来的"，
//  不是"估出来的"。给组件留高度时：width 用 text_width()，height 用 text_height()。
// -----------------------------------------------------------------------------
inline float Ui::text_height(const std::string& text, float width, const Style& st) const {
    if (text.empty() || width <= 1.0f) return 0.0f;
    const float fs = st.font_size > 0.0f ? st.font_size : impl->theme.font_size;
    const int   wt = st.font_weight > 0 ? st.font_weight : 400;
    const std::vector<std::string>& lines =
        detail::wrap_layout(impl, text, impl->px(width), impl->px(fs), wt, st.wrap);
    const size_t n = lines.size();
    if (n == 0) return 0.0f;
    const float lh   = line_height(fs);
    const float step = lh + impl->px(st.line_gap);          // 与 draw_text 完全一致
    return (float(n) - 1.0f) * step + lh;
}

// alert 在给定宽度下需要多高 —— 让"正文折行"这类情况不用拍一个高度再被裁掉。
// 注意标题按单行处理（alert 就是这么画的，标题过长会裁），别把标题写太长。
inline float Ui::alert_height(const std::string& title, const std::string& description,
                              float width, const Style& st) const {
    const float fs  = st.font_size > 0.0f ? st.font_size : impl->theme.font_size;
    const float pad = st.pad_x >= 0.0f ? st.pad_x : 14.0f;
    const float d   = fs + 8.0f;                             // 图标直径（见 alert 里的自绘）
    const float tw  = std::max(width - pad * 2.0f - d - 10.0f, 1.0f);
    const float lh  = line_height(fs);
    Style ds = st;
    ds.font_size = fs;
    ds.font_weight = 400;
    ds.wrap = true;
    const float desc_h = description.empty() ? 0.0f : text_height(description, tw, ds);
    const float title_h = title.empty() ? 0.0f : lh;
    return pad * 2.0f + title_h + (description.empty() ? 0.0f : 2.0f) + desc_h;
}

// =============================================================================
//  8.7  轻量布局（立即模式下的顺序布局）
// =============================================================================
inline Rect Ui::row(float h) {
    if (impl->group.empty()) return Rect();
    Rect& g = impl->group.back();
    float amount = std::min(h, g.h);
    Rect r(g.x, g.y, g.w, amount);
    g.y += amount;
    g.h -= amount;
    return r;
}

inline Rect Ui::column(float w) {
    if (impl->group.empty()) return Rect();
    Rect& g = impl->group.back();
    float amount = std::min(w, g.w);
    Rect r(g.x, g.y, amount, g.h);
    g.x += amount;
    g.w -= amount;
    return r;
}

inline void Ui::push_padding(float x, float y) {
    if (impl->group.empty()) return;
    impl->group.push_back(impl->group.back().inset(x, y));
}

inline void Ui::pop_padding() {
    if (impl->group.size() > 1) impl->group.pop_back();
}

inline void Ui::begin_group(const Rect& box) {
    impl->group.push_back(box);
}

inline void Ui::end_group() {
    if (impl->group.size() > 1) impl->group.pop_back();
}

// -----------------------------------------------------------------------------
//  Stack 实现
//  整个布局器只做加法和一次比较：不持有控件、不注册任何东西、不需要帧间
//  状态，所以嵌套、临时丢弃、值拷贝都是零代价的。
// -----------------------------------------------------------------------------
inline void Stack::advance(float d) {
    if (vertical_) { area.y += d; area.h = std::max(0.0f, area.h - d); }
    else           { area.x += d; area.w = std::max(0.0f, area.w - d); }
}

// 结算"待垫的 gap"后的区域。rest / remain / part 都基于它，这样"剩余空间"
// 始终指"下一个元素开始的位置"，而不是游标当前所在的（还差一个间距的）位置。
inline Rect Stack::rest() const {
    Rect a = area;
    if (pending_) {
        if (vertical_) { a.y += gap; a.h = std::max(0.0f, a.h - gap); }
        else           { a.x += gap; a.w = std::max(0.0f, a.w - gap); }
    }
    return a;
}

inline Rect Stack::next(float size) {
    if (pending_) { advance(gap); pending_ = false; }   // 间距只在元素之间垫
    float s = std::max(0.0f, size);
    Rect r;
    if (vertical_) {
        s = std::min(s, area.h);
        r = Rect(area.x, area.y, area.w, s);
    } else {
        s = std::min(s, area.w);
        r = Rect(area.x, area.y, s, area.h);
    }
    advance(s);
    pending_ = true;
    return r;
}

inline Rect Stack::next_frac(float fraction) {
    if (pending_) { advance(gap); pending_ = false; }
    const float span = vertical_ ? area.h : area.w;
    return next(std::max(0.0f, span * fraction));
}

inline Rect Stack::next_text(const std::string& s, float extra, float font_size, int weight) {
    float w = 0.0f;
    if (ui && !s.empty()) {
        const float fs = font_size > 0.0f ? font_size : ui->theme().font_size;
        w = ui->text_width(s, fs, weight);       // 已是逻辑单位
    }
    return next(w + extra);
}

inline Rect Stack::fill() {
    if (pending_) { advance(gap); pending_ = false; }
    Rect r = area;
    advance(vertical_ ? area.h : area.w);
    return r;
}

inline void Stack::skip(float amount) {
    if (pending_) { advance(gap); pending_ = false; }
    advance(std::max(0.0f, amount));
}

inline Rect Stack::part(int index, int count) const {
    if (count <= 0 || index < 0 || index >= count) return Rect();
    const Rect  a      = rest();
    const float span   = vertical_ ? a.h : a.w;
    const float usable = span - gap * float(count - 1);   // 份间的 gap 先扣掉
    if (usable <= 0.0f) return Rect();
    const float each = usable / float(count);
    const float off  = float(index) * (each + gap);
    return vertical_ ? Rect(a.x, a.y + off, a.w, each)
                     : Rect(a.x + off, a.y, each, a.h);
}

inline Stack Stack::vbox(const Rect& box, float g) const {
    return Stack(ui, box, true,  g >= 0.0f ? g : gap);
}
inline Stack Stack::hbox(const Rect& box, float g) const {
    return Stack(ui, box, false, g >= 0.0f ? g : gap);
}
inline Stack Stack::vbox(float size, float g) { return vbox(next(size), g); }
inline Stack Stack::hbox(float size, float g) { return hbox(next(size), g); }

inline Stack Ui::vbox(const Rect& box, float gap, float pad) {
    return Stack(this, pad > 0.0f ? box.inset(pad) : box, true, gap);
}
inline Stack Ui::hbox(const Rect& box, float gap, float pad) {
    return Stack(this, pad > 0.0f ? box.inset(pad) : box, false, gap);
}

inline void Ui::push_clip(const Rect& r) {
    // 裁剪区跟着变换走（旋转时用包围盒 —— 轴对齐的裁剪区表达不了旋转矩形，
    // 这一点在文档里写明；要精确裁旋转内容就自己用多边形，或者干脆别套裁剪）
    impl->clips.push_back(impl->xf_phys(r));
    impl->recompute_clip();
}

inline void Ui::pop_clip() {
    if (!impl->clips.empty()) impl->clips.pop_back();
    impl->recompute_clip();
}

// =============================================================================
//  8.8  组件通用工具
// =============================================================================
namespace detail {

// 一个组件"皮肤"的完整解析结果：base/hover/press 三态 + 前景 + 描边
struct Skin {
    Color base   = Color::none();
    Color hover  = Color::none();
    Color press  = Color::none();
    Color fg     = Color(0, 0, 0);
    Color border = Color::none();
    float border_w = 0.0f;
    float radius   = 8.0f;
    float font_size= 14.0f;
    int   weight   = 400;
};

inline Skin make_skin(const Theme& th, const Style& st, int default_variant, float default_radius) {
    const int v = st.variant >= 0 ? st.variant : default_variant;
    Skin sk;
    sk.radius    = st.radius >= 0.0f ? st.radius : default_radius;
    sk.font_size = st.font_size > 0.0f ? st.font_size : th.font_size;
    sk.weight    = st.font_weight > 0 ? st.font_weight : 400;
    sk.fg        = th.foreground;

    switch (v) {
        case Style::Default:     sk.base = th.primary;     sk.fg = th.primary_foreground;     break;
        case Style::Secondary:   sk.base = th.secondary;   sk.fg = th.secondary_foreground;   break;
        case Style::Outline:     sk.base = Color::none();  sk.fg = th.foreground;
                                 sk.border = th.border;    sk.border_w = 1.0f;                 break;
        case Style::Ghost:       sk.base = Color::none();  sk.fg = th.foreground;             break;
        case Style::Destructive: sk.base = th.destructive; sk.fg = th.destructive_foreground; break;
        case Style::Link:        sk.base = Color::none();  sk.fg = th.primary;                break;
        default:                 sk.base = th.secondary;   sk.fg = th.secondary_foreground;   break;
    }
    sk.hover = sk.base.is_none() ? th.accent : hover_color(sk.base);
    sk.press = sk.base.is_none() ? press_color(th.accent) : press_color(sk.base);

    // 显式覆盖优先
    if (!st.bg.is_none()) { sk.base = st.bg; sk.hover = hover_color(sk.base); sk.press = press_color(sk.base); }
    if (!st.fg.is_none()) sk.fg = st.fg;
    if (st.muted) sk.fg = th.muted_foreground;
    if (!st.border_color.is_none()) { sk.border = st.border_color; if (sk.border_w <= 0.0f) sk.border_w = 1.0f; }
    if (st.border_width >= 0.0f) sk.border_w = st.border_width;
    return sk;
}

inline void paint_surface(Ui& ui, const Rect& r, const Skin& sk, float t_hover, float t_press,
                          bool disabled, float radius_override = -1.0f) {
    const float rad = radius_override >= 0.0f ? radius_override : sk.radius;
    Color bg = state_fill(sk.base, sk.hover, sk.press, t_hover, t_press);
    if (disabled && bg.a > 0) bg.a = uint8_t(float(bg.a) * 0.45f);
    if (bg.a > 0) ui.fill_rect(r, bg, rad);
    if (sk.border_w > 0.0f && !sk.border.is_none()) {
        Color bc = sk.border;
        if (disabled) bc.a = uint8_t(float(bc.a) * 0.45f);
        ui.stroke_rect(r, bc, rad, sk.border_w);
    }
}

inline void paint_focus_ring(Ui& ui, const Rect& r, float radius, const Color& ring) {
    ui.stroke_rect(r.expand(1.5f), ring.scaled_alpha(0.55f), radius + 1.5f, 2.0f);
}

// 把 Style 中与文字相关的部分转成 draw_text 用的 Style
inline Style text_style_of(const Style& st, const Skin& sk, int default_weight) {
    Style ts;
    ts.font_size   = sk.font_size;
    ts.font_weight = sk.weight > 0 ? sk.weight : default_weight;
    ts.align       = st.align;
    ts.valign      = st.valign;
    ts.align_set   = st.align_set;
    ts.valign_set  = st.valign_set;
    ts.italic      = st.italic;
    return ts;
}

inline bool is_focused(Ui::Impl* im, uint64_t id) {
    return im->focus == reinterpret_cast<void*>(static_cast<uintptr_t>(id));
}
inline void grab_focus(Ui::Impl* im, uint64_t id) {
    im->focus = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
    im->focus_id = id;
    im->caret_blink = 0.0f;
}
inline void drop_focus(Ui::Impl* im) { im->focus = nullptr; im->focus_id = 0; }

// -----------------------------------------------------------------------------
//  文本编辑小工具（input 组件用）
// -----------------------------------------------------------------------------
// 由横坐标反查光标应落在第几个码点
inline int caret_from_x(const Ui& ui, const std::string& s, float local_x, float font_size, int weight) {
    std::vector<uint32_t> cps = utf8_codepoints(s);
    float acc = 0.0f;
    int best = 0;
    float best_d = 1e9f;
    for (size_t i = 0; i <= cps.size(); ++i) {
        std::string prefix = utf8_from_codepoints(cps, 0, i);
        float w = ui.text_width(prefix, font_size, weight);
        float d = std::fabs(w - local_x);
        if (d < best_d) { best_d = d; best = int(i); }
        acc = w;
    }
    (void)acc;
    return best;
}

// -----------------------------------------------------------------------------
//  编辑器的共用件：按词跳转 + 撤销栈
//  单行 input 和多行 textarea 用同一套，避免两处行为不一致（以前就是这样：
//  input 能拖选，textarea 不能；两边都没有撤销）。
// -----------------------------------------------------------------------------
//  0 = 空白/标点（按词跳时先跳过），1 = 西文词，2 = CJK（逐字跳）
inline int cp_class(uint32_t c) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return 0;
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') return 1;
    if (c >= 0x2E80) return 2;
    return 0;
}

inline int word_left(const std::vector<uint32_t>& cps, int i) {
    int n = int(cps.size());
    if (i > n) i = n;
    if (i <= 0) return 0;
    while (i > 0 && cp_class(cps[size_t(i - 1)]) == 0) --i;
    if (i == 0) return 0;
    const int cls = cp_class(cps[size_t(i - 1)]);
    if (cls == 2) return i - 1;                       // 中文一个字就是一步
    while (i > 0 && cp_class(cps[size_t(i - 1)]) == cls) --i;
    return i;
}

inline int word_right(const std::vector<uint32_t>& cps, int i) {
    const int n = int(cps.size());
    if (i < 0) i = 0;
    if (i >= n) return n;
    const int cls = cp_class(cps[size_t(i)]);
    if (cls == 0) {
        while (i < n && cp_class(cps[size_t(i)]) == 0) ++i;
        return i;
    }
    if (cls == 2) return i + 1;
    while (i < n && cp_class(cps[size_t(i)]) == cls) ++i;
    return i;
}

// 双击选词：返回 [a, b)；点在空白处时选中那一段空白
inline void word_span(const std::vector<uint32_t>& cps, int at, int* a, int* b) {
    const int n = int(cps.size());
    if (n == 0) { *a = 0; *b = 0; return; }
    if (at >= n) at = n - 1;
    const int cls = cp_class(cps[size_t(at)]);
    int lo = at, hi = at + 1;
    if (cls == 2) { *a = at; *b = at + 1; return; }
    while (lo > 0 && cp_class(cps[size_t(lo - 1)]) == cls) --lo;
    while (hi < n && cp_class(cps[size_t(hi)]) == cls) ++hi;
    *a = lo;
    *b = hi;
}

enum : uint64_t { EDIT_TYPE = 1, EDIT_DELETE = 2, EDIT_OTHER = 3 };

// 压一份"改动前"的快照。连续同类输入（0.7s 内）合并成一步 —— 否则打 20 个字
// 要按 20 次 Ctrl+Z 才能退掉一句话，这是编辑器最让人烦的细节之一。
inline void edit_snapshot(Ui::Impl* im, uint64_t id, const std::string& before,
                          int caret0, int anchor0, uint64_t kind) {
    auto& st = im->edit_stack[id];
    const float now = im->clock;
    if (kind != EDIT_OTHER && st.kind == kind && !st.undo.empty() && (now - st.last) < 0.7f) {
        st.last = now;
        st.redo.clear();
        return;
    }
    st.undo.push_back(Ui::Impl::EditSnap{before, caret0, anchor0});
    if (st.undo.size() > 200) st.undo.erase(st.undo.begin());
    st.redo.clear();
    st.last = now;
    st.kind = kind;
}

inline bool edit_undo(Ui::Impl* im, uint64_t id, std::string* text, int* caret, int* anchor) {
    auto it = im->edit_stack.find(id);
    if (it == im->edit_stack.end() || it->second.undo.empty()) return false;
    auto& st = it->second;
    st.redo.push_back(Ui::Impl::EditSnap{*text, *caret, *anchor});
    const Ui::Impl::EditSnap s = st.undo.back();
    st.undo.pop_back();
    *text = s.text;
    *caret = s.caret;
    *anchor = s.anchor;
    st.kind = 0;
    st.last = -1e9f;                       // 撤销之后不再和后续输入合并
    if (im->request_repaint) im->request_repaint();
    return true;
}

inline bool edit_redo(Ui::Impl* im, uint64_t id, std::string* text, int* caret, int* anchor) {
    auto it = im->edit_stack.find(id);
    if (it == im->edit_stack.end() || it->second.redo.empty()) return false;
    auto& st = it->second;
    st.undo.push_back(Ui::Impl::EditSnap{*text, *caret, *anchor});
    const Ui::Impl::EditSnap s = st.redo.back();
    st.redo.pop_back();
    *text = s.text;
    *caret = s.caret;
    *anchor = s.anchor;
    st.kind = 0;
    st.last = -1e9f;
    if (im->request_repaint) im->request_repaint();
    return true;
}

} // namespace detail

// =============================================================================
//  8.9  文本类组件
// =============================================================================
inline void Ui::label(const std::string& text, const Rect& box, const Style& st) {
    Style s = st;
    if (s.font_size <= 0.0f) s.font_size = impl->theme.font_size;
    if (s.font_weight <= 0) s.font_weight = 400;
    const Color c = !s.fg.is_none() ? s.fg
                  : (s.muted ? impl->theme.muted_foreground : impl->theme.foreground);
    draw_text(text, box, c, s);
}

inline void Ui::heading(const std::string& text, const Rect& box, const Style& st) {
    Style s = st;
    if (s.font_size <= 0.0f) s.font_size = 24.0f;
    if (s.font_weight <= 0) s.font_weight = 700;
    const Color c = !s.fg.is_none() ? s.fg
                  : (s.muted ? impl->theme.muted_foreground : impl->theme.foreground);
    draw_text(text, box, c, s);
}

inline void Ui::subheading(const std::string& text, const Rect& box, const Style& st) {
    Style s = st;
    if (s.font_size <= 0.0f) s.font_size = 18.0f;
    if (s.font_weight <= 0) s.font_weight = 600;
    const Color c = !s.fg.is_none() ? s.fg
                  : (s.muted ? impl->theme.muted_foreground : impl->theme.foreground);
    draw_text(text, box, c, s);
}

inline void Ui::muted_text(const std::string& text, const Rect& box, const Style& st) {
    Style s = st;
    if (s.font_size <= 0.0f) s.font_size = impl->theme.font_size;
    if (s.font_weight <= 0) s.font_weight = 400;
    const Color c = !s.fg.is_none() ? s.fg : impl->theme.muted_foreground;
    draw_text(text, box, c, s);
}

inline void Ui::code_text(const std::string& text, const Rect& box, const Style& st) {
    const Theme& th = impl->theme;
    const float rad = st.radius >= 0.0f ? st.radius : th.radius_sm;
    const float fs  = st.font_size > 0.0f ? st.font_size : th.font_size - 1.0f;
    const Color bg  = !st.bg.is_none() ? st.bg : th.muted;
    const float pad = 6.0f;
    fill_rect(box, bg, rad);
    if (st.border_width > 0.0f && !st.border_color.is_none())
        stroke_rect(box, st.border_color, rad, st.border_width);
    Style ts;
    ts.font_size = fs;
    ts.font_weight = 400;
    ts.align = st.align_set ? st.align : Align::Left;
    ts.valign = VAlign::Middle;
    const Color c = !st.fg.is_none() ? st.fg : th.foreground;
    draw_text(text, box.inset(pad, 0.0f), c, ts);
}

// =============================================================================
//  8.10  容器类组件
// =============================================================================
inline void Ui::panel(const Rect& box, const Style& st) {
    const Theme& th = impl->theme;
    const Color bg = !st.bg.is_none() ? st.bg : th.card;
    const Color bd = !st.border_color.is_none() ? st.border_color : th.border;
    const float bw = st.border_width >= 0.0f ? st.border_width : 1.0f;
    const float rad = st.radius >= 0.0f ? st.radius : th.radius_lg;
    if (st.muted) fill_rect(box, th.muted, rad);
    else          fill_rect(box, bg, rad);
    if (bw > 0.0f && bd.a > 0) stroke_rect(box, bd, rad, bw);
}

inline void Ui::card(const Rect& box, const Style& st) { panel(box, st); }

inline void Ui::separator(const Rect& box, const Style& st) {
    const Color c = !st.bg.is_none() ? st.bg : impl->theme.border;
    if (box.w >= box.h)
        fill_rect(Rect(box.x, box.cy() - 0.5f, box.w, 1.0f), c, 0.0f);
    else
        fill_rect(Rect(box.cx() - 0.5f, box.y, 1.0f, box.h), c, 0.0f);
}

inline void Ui::badge(const std::string& text, const Rect& box, const Style& st) {
    const Theme& th = impl->theme;
    const float rad = st.radius >= 0.0f ? st.radius : 999.0f;
    const float fs  = st.font_size > 0.0f ? st.font_size : th.font_size - 3.0f;
    detail::Skin sk = detail::make_skin(th, st, Style::Secondary, rad);
    if (sk.weight == 400) sk.weight = 500;
    const Color bg = !st.bg.is_none() ? st.bg : th.secondary;
    const Color fg = !st.fg.is_none() ? st.fg : th.secondary_foreground;
    fill_rect(box, bg, rad);
    if (st.border_width > 0.0f && !st.border_color.is_none())
        stroke_rect(box, st.border_color, rad, st.border_width);
    Style ts;
    ts.font_size = fs;
    ts.font_weight = sk.weight;
    ts.align = Align::Center;
    ts.valign = VAlign::Middle;
    draw_text(text, box, fg, ts);
}

inline void Ui::progress(float value, const Rect& box, const Style& st) {
    const Theme& th = impl->theme;
    const float rad = st.radius >= 0.0f ? st.radius : 999.0f;
    const Color track = !st.border_color.is_none() ? st.border_color : th.muted;
    const Color fill  = !st.bg.is_none() ? st.bg : th.primary;
    fill_rect(box, track, rad);
    const float t = detail::clampf(value, 0.0f, 1.0f);
    if (t > 0.0f) fill_rect(Rect(box.x, box.y, box.w * t, box.h), fill, rad);
}

inline void Ui::skeleton(const Rect& box, const Style& st) {
    const Theme& th = impl->theme;
    const float rad = st.radius >= 0.0f ? st.radius : th.radius;
    Color c = !st.bg.is_none() ? st.bg : th.muted;
    // 呼吸要靠时间驱动，所以默认是**静态**的灰块：skeleton 常常只是"这里将来
    // 会有内容"，安静不动的占位块完全够用，而它不该让整窗从 0% 变成 20% CPU。
    //
    // Style().animate() 点名要求它动 —— 但也只在**鼠标悬停**时动。理由同上：
    // 一个常驻动画就是整窗每帧重画，代价和窗口面积成正比，不能默认付。
    if (st.animated) {
        const bool hovered = box.contains(impl->input.mouse.x, impl->input.mouse.y);
        float& ph = impl->phase[detail::make_id("skeleton_phase", box)];
        if (hovered) {
            ph += impl->dt * 3.2f;
            repaint();
        }
        c.a = uint8_t(255.0f * (0.55f + 0.25f * std::sin(ph)));
    }
    fill_rect(box, c, rad);
}

// =============================================================================
//  8.11  交互类组件
// =============================================================================
inline bool Ui::button(const std::string& text, const Rect& box, const Style& st) {
    uint64_t id = detail::make_id("button", box, detail::hash_string(text.c_str()));
    Style s = st;
    if (!s.align_set) s.align = Align::Center;
    if (!s.valign_set) s.valign = VAlign::Middle;

    const bool enabled = !s.disabled;
    detail::Interact in = detail::interact(impl, id, box, enabled);
    if (in.hovered) set_cursor(Cursor::Hand);

    const float t_hover = detail::animate_value(impl, id * 31u + 1u, in.hovered ? 1.0f : 0.0f, 18.0f);
    const float t_press = detail::animate_value(impl, id * 31u + 2u, (in.held && in.hovered) ? 1.0f : 0.0f, 34.0f);

    detail::Skin sk = detail::make_skin(impl->theme, s, Style::Default, impl->theme.radius_sm);
    if (s.font_weight <= 0) sk.weight = 500;   // shadcn 按钮默认 medium

    Rect r = box;
    if (s.press_scale > 0.0f && s.press_scale < 1.0f && t_press > 0.01f) {
        float k = 1.0f - (1.0f - s.press_scale) * t_press;
        r = Rect::centered(box.cx(), box.cy(), box.w * k, box.h * k);
    }

    detail::paint_surface(*this, r, sk, t_hover, t_press, s.disabled);

    Style ts = detail::text_style_of(s, sk, 500);
    Color fg = sk.fg;
    if (s.disabled) fg.a = uint8_t(float(fg.a) * 0.45f);
    draw_text(text, r, fg, ts);

    if (detail::is_focused(impl, id)) detail::paint_focus_ring(*this, r, sk.radius, impl->theme.ring);

    if (in.pressed) detail::grab_focus(impl, id);
    return in.clicked && enabled;
}

inline bool Ui::icon_button(const std::string& glyph, const Rect& box, const Style& st) {
    uint64_t id = detail::make_id("iconbtn", box, detail::hash_string(glyph.c_str()));
    Style s = st;
    if (s.variant < 0) s.variant = Style::Ghost;
    if (!s.align_set) s.align = Align::Center;
    if (!s.valign_set) s.valign = VAlign::Middle;

    const bool enabled = !s.disabled;
    detail::Interact in = detail::interact(impl, id, box, enabled);
    if (in.hovered) set_cursor(Cursor::Hand);
    const float t_hover = detail::animate_value(impl, id * 31u + 1u, in.hovered ? 1.0f : 0.0f, 18.0f);
    const float t_press = detail::animate_value(impl, id * 31u + 2u, (in.held && in.hovered) ? 1.0f : 0.0f, 34.0f);

    float rad = s.radius >= 0.0f ? s.radius : impl->theme.radius_sm;
    detail::Skin sk = detail::make_skin(impl->theme, s, Style::Ghost, rad);
    detail::paint_surface(*this, box, sk, t_hover, t_press, s.disabled);

    float fs = s.font_size > 0.0f ? s.font_size : std::min(box.w, box.h) * 0.55f;
    Style ts;
    ts.font_size = fs;
    ts.font_weight = 400;
    ts.align = Align::Center;
    ts.valign = VAlign::Middle;
    Color fg = sk.fg;
    if (s.disabled) fg.a = uint8_t(float(fg.a) * 0.45f);
    draw_text(glyph, box, fg, ts);
    if (in.pressed) detail::grab_focus(impl, id);
    return in.clicked && enabled;
}

// -----------------------------------------------------------------------------
//  nav_item —— 侧边栏 / 导航列表里的一行
//  和 button 的区别全在"它是导航，不是动作"：文字左对齐（button 居中）、
//  选中态是持续的（button 只有 hover/press），并且右侧可以留白给计数或快捷键。
// -----------------------------------------------------------------------------
inline bool Ui::nav_item(const std::string& label, bool active, const Rect& box, const Style& st) {
    const Theme& th = impl->theme;
    uint64_t id = detail::make_id("navitem", box, detail::hash_string(label.c_str()));
    const bool enabled = !st.disabled;

    detail::Interact in = detail::interact(impl, id, box, enabled);
    if (in.hovered) set_cursor(Cursor::Hand);
    const float t_hover  = detail::animate_value(impl, id * 31u + 1u, (in.hovered && !active) ? 1.0f : 0.0f, 20.0f);
    const float t_active = detail::animate_value(impl, id * 31u + 2u, active ? 1.0f : 0.0f, 24.0f);

    const float rad = st.radius >= 0.0f ? st.radius : th.radius_sm;
    const float fs  = st.font_size > 0.0f ? st.font_size : th.font_size;
    const float pad = st.pad_x >= 0.0f ? st.pad_x : 14.0f;

    // 底色：hover 是淡一层的 accent，选中是满 accent（两者可以叠加）
    const float bg_t = clampf(t_hover * 0.6f + t_active, 0.0f, 1.0f);
    if (bg_t > 0.01f) {
        Color bg = !st.bg.is_none() ? st.bg : th.accent;
        bg.a = uint8_t(clampf(float(bg.a) * bg_t, 0.0f, 255.0f));
        fill_rect(box, bg, rad);
    }

    // 选中态左侧一条 3px 竖条。只靠底色，深色主题下几乎看不出差别。
    if (t_active > 0.02f) {
        const float h = std::max(box.h - 12.0f, 4.0f);
        fill_rect(Rect(box.x + 3.0f, box.cy() - h * 0.5f, 3.0f, h),
                  (!st.fg.is_none() ? st.fg : th.primary).with_alpha(uint8_t(235.0f * t_active)),
                  1.5f);
    }

    // 字重固定 500：选中时换字重会让文字宽度跳一下，看着像整个侧边栏在抖。
    // 选中感交给底色 + 竖条 + 前景色。
    Color fg = !st.fg.is_none()
             ? st.fg
             : th.muted_foreground.mix(th.foreground, clampf(t_active + t_hover * 0.35f, 0.0f, 1.0f));
    if (st.disabled) fg.a = uint8_t(float(fg.a) * 0.45f);
    Style ts;
    ts.font_size = fs;
    ts.font_weight = st.font_weight > 0 ? st.font_weight : 500;
    ts.align = Align::Left;
    ts.valign = VAlign::Middle;
    draw_text(label, Rect(box.x + pad, box.y, std::max(box.w - pad - 8.0f, 1.0f), box.h), fg, ts);

    if (in.pressed) detail::grab_focus(impl, id);
    return in.clicked && enabled;
}

// -----------------------------------------------------------------------------
//  input —— 带光标、选区、编辑、剪贴板的单行文本框
// -----------------------------------------------------------------------------
inline bool Ui::input(std::string* value, const Rect& box, const Style& st) {
    if (!value) return false;
    const Theme& th = impl->theme;
    uint64_t id = detail::make_id("input", box);
    const bool enabled = !st.disabled;

    detail::Interact in = detail::interact(impl, id, box, enabled);
    if (in.hovered || detail::is_focused(impl, id)) set_cursor(Cursor::IBeam);

    bool focused = detail::is_focused(impl, id);
    bool changed = false;

    // 点击别处 → 失焦
    if (focused && impl->input.pressed[0] && !in.hovered) {
        detail::drop_focus(impl);
        focused = false;
    }
    // 点进来 → 取焦 + 定位光标
    if (in.pressed) {
        detail::grab_focus(impl, id);
        focused = true;
    }

    const float rad  = st.radius >= 0.0f ? st.radius : th.radius_sm;
    const float pad  = st.pad_x >= 0.0f ? st.pad_x : 12.0f;
    const float fs   = st.font_size > 0.0f ? st.font_size : th.font_size;
    const float tw   = text_width(*value, fs, 400);
    const float avail = std::max(box.w - pad * 2.0f, 1.0f);

    if (focused) {
        // 光标定位（单击 / 拖拽 / 双击选词 / 三击全选 / Shift 扩选）
        if (impl->input.pressed[0] && in.hovered) {
            const float lx = impl->input.mouse.x - (box.x + pad);
            impl->caret = detail::caret_from_x(*this, *value, lx, fs, 400);
            const std::vector<uint32_t> cps = utf8_codepoints(*value);
            if (impl->input.click_count >= 3) {
                impl->sel_anchor = 0;
                impl->caret = int(cps.size());
            } else if (impl->input.click_count == 2) {
                int a = 0, b = 0;
                detail::word_span(cps, impl->caret, &a, &b);
                impl->sel_anchor = a;
                impl->caret = b;
            } else if (!impl->input.shift()) {
                impl->sel_anchor = -1;
            }
            impl->flags[id * 31u + 7u] = float(impl->caret);   // 拖选锚点
        }
        if (impl->input.down[0] && in.hovered && impl->active == id) {
            const float lx = impl->input.mouse.x - (box.x + pad);
            const int before = impl->caret;
            impl->caret = detail::caret_from_x(*this, *value, lx, fs, 400);
            if (impl->caret != before && impl->sel_anchor < 0)
                impl->sel_anchor = int(impl->flags[id * 31u + 7u]);
        }

        const Input& kin = impl->input;
        int n_cp = utf8_count(*value);
        if (impl->caret > n_cp) impl->caret = n_cp;
        if (impl->caret < 0) impl->caret = 0;

        // ---- 撤销 / 重做 ----
        // Ctrl+Z 撤销，Ctrl+Shift+Z / Ctrl+Y 重做。和输入合并成一步的规则在
        // edit_snapshot 里（连续打字 0.7s 内算一次）。
        bool undone = false;
        if (kin.ctrl_of('Z') || kin.ctrl_of('Y')) {
            const bool redo = kin.key_pressed['Y'] || (kin.key_pressed['Z'] && kin.shift_of('Z'));
            if (redo ? detail::edit_redo(impl, id, value, &impl->caret, &impl->sel_anchor)
                     : detail::edit_undo(impl, id, value, &impl->caret, &impl->sel_anchor)) {
                n_cp = utf8_count(*value);
                impl->caret = detail::clampi(impl->caret, 0, n_cp);
                impl->caret_blink = 0.0f;
                changed = true;
                undone = true;
            }
        }

        const std::string before_text = *value;
        const int caret0 = impl->caret, anchor0 = impl->sel_anchor;
        uint64_t edit_kind = detail::EDIT_OTHER;

        int sel_from = impl->sel_anchor >= 0 ? std::min(impl->caret, impl->sel_anchor) : impl->caret;
        int sel_to   = impl->sel_anchor >= 0 ? std::max(impl->caret, impl->sel_anchor) : impl->caret;
        bool has_sel = sel_to > sel_from;

        auto delete_selection = [&]() {
            if (!has_sel) return false;
            std::vector<uint32_t> cps = utf8_codepoints(*value);
            std::vector<uint32_t> keep;
            keep.reserve(cps.size());
            for (int i = 0; i < int(cps.size()); ++i)
                if (i < sel_from || i >= sel_to) keep.push_back(cps[i]);
            *value = utf8_from_codepoints(keep, 0, keep.size());
            impl->caret = sel_from;
            impl->sel_anchor = -1;
            changed = true;
            return true;
        };

        if (!undone && kin.key_pressed[KEY_BACKSPACE]) {
            edit_kind = detail::EDIT_DELETE;
            if (kin.ctrl_of(KEY_BACKSPACE) || kin.alt_of(KEY_BACKSPACE)) {
                // Ctrl+Backspace：删到上一个词边界
                const std::vector<uint32_t> cps = utf8_codepoints(*value);
                const int to = impl->caret;
                const int from = detail::word_left(cps, impl->caret);
                if (has_sel || from < to) {
                    impl->sel_anchor = from;
                    impl->caret = has_sel ? sel_to : to;
                    has_sel = true;
                    sel_from = from;
                    sel_to = impl->caret;
                    delete_selection();
                }
            } else if (!delete_selection() && impl->caret > 0) {
                std::vector<uint32_t> cps = utf8_codepoints(*value);
                cps.erase(cps.begin() + (impl->caret - 1));
                *value = utf8_from_codepoints(cps, 0, cps.size());
                impl->caret -= 1;
                changed = true;
            }
        }
        if (!undone && kin.key_pressed[KEY_DELETE]) {
            edit_kind = detail::EDIT_DELETE;
            if (!delete_selection()) {
                std::vector<uint32_t> cps = utf8_codepoints(*value);
                if (impl->caret < int(cps.size())) {
                    cps.erase(cps.begin() + impl->caret);
                    *value = utf8_from_codepoints(cps, 0, cps.size());
                    changed = true;
                }
            }
        }
        // ←/→：Ctrl 按词跳，Shift 扩选
        if (!undone && kin.key_pressed[KEY_LEFT]) {
            if (!kin.shift()) {
                impl->caret = has_sel ? sel_from
                                      : (kin.ctrl_of(KEY_LEFT) ? detail::word_left(utf8_codepoints(*value), impl->caret)
                                                               : std::max(0, impl->caret - 1));
                impl->sel_anchor = -1;
            } else {
                if (impl->sel_anchor < 0) impl->sel_anchor = has_sel ? sel_to : impl->caret;
                impl->caret = kin.ctrl_of(KEY_LEFT) ? detail::word_left(utf8_codepoints(*value), impl->caret)
                                                    : std::max(0, impl->caret - 1);
            }
            impl->caret_blink = 0.0f;
        }
        if (!undone && kin.key_pressed[KEY_RIGHT]) {
            if (!kin.shift()) {
                impl->caret = has_sel ? sel_to
                                      : (kin.ctrl_of(KEY_RIGHT) ? detail::word_right(utf8_codepoints(*value), impl->caret)
                                                                : std::min(n_cp, impl->caret + 1));
                impl->sel_anchor = -1;
            } else {
                if (impl->sel_anchor < 0) impl->sel_anchor = has_sel ? sel_from : impl->caret;
                impl->caret = kin.ctrl_of(KEY_RIGHT) ? detail::word_right(utf8_codepoints(*value), impl->caret)
                                                     : std::min(n_cp, impl->caret + 1);
            }
            impl->caret_blink = 0.0f;
        }
        if (!undone && kin.key_pressed[KEY_HOME]) {
            if (kin.shift()) { if (impl->sel_anchor < 0) impl->sel_anchor = impl->caret; }
            else impl->sel_anchor = -1;
            impl->caret = 0;
        }
        if (!undone && kin.key_pressed[KEY_END]) {
            if (kin.shift()) { if (impl->sel_anchor < 0) impl->sel_anchor = impl->caret; }
            else impl->sel_anchor = -1;
            impl->caret = n_cp;
        }

        // 剪贴板
        if (!undone && kin.ctrl_of('C')) {
            std::vector<uint32_t> cps = utf8_codepoints(*value);
            impl->set_clipboard(utf8_from_codepoints(cps, size_t(sel_from), size_t(sel_to - sel_from)));
        }
        if (!undone && kin.ctrl_of('X')) {
            edit_kind = detail::EDIT_OTHER;
            std::vector<uint32_t> cps = utf8_codepoints(*value);
            impl->set_clipboard(utf8_from_codepoints(cps, size_t(sel_from), size_t(sel_to - sel_from)));
            delete_selection();
        }
        if (!undone && kin.ctrl_of('V')) {
            edit_kind = detail::EDIT_OTHER;
            delete_selection();
            std::string paste = impl->get_clipboard ? impl->get_clipboard() : std::string();
            // 单行输入：过滤换行
            std::string filtered;
            for (char ch : paste) if (ch != '\n' && ch != '\r') filtered.push_back(ch);
            if (!filtered.empty()) {
                std::vector<uint32_t> cps = utf8_codepoints(*value);
                std::vector<uint32_t> ins = utf8_codepoints(filtered);
                cps.insert(cps.begin() + impl->caret, ins.begin(), ins.end());
                *value = utf8_from_codepoints(cps, 0, cps.size());
                impl->caret += int(ins.size());
                changed = true;
            }
        }
        if (!undone && kin.ctrl_of('A')) {
            impl->sel_anchor = 0; impl->caret = n_cp;
        }
        if (!undone && kin.key_pressed[KEY_ESC]) { detail::drop_focus(impl); focused = false; }

        // 文本输入（含中文 IME 提交后的字符）
        // 按住 Ctrl 时不落字：真实 Ctrl+Z 走 WM_CHAR(0x1A) 已经被后端过滤，但
        // 有些输入法/键盘布局会在组合键下送来可打印字符，那就变成"撤销一次
        // 顺手打了个 Z"。
        if (!undone && !kin.ctrl() && impl->text_veto <= 0.0f && !kin.text_input.empty()) {
            edit_kind = detail::EDIT_TYPE;
            delete_selection();
            std::vector<uint32_t> cps = utf8_codepoints(*value);
            std::vector<uint32_t> ins = utf8_codepoints(kin.text_input);
            cps.insert(cps.begin() + impl->caret, ins.begin(), ins.end());
            *value = utf8_from_codepoints(cps, 0, cps.size());
            impl->caret += int(ins.size());
            impl->caret_blink = 0.0f;
            changed = true;
        }

        // 有实际文本改动才进撤销栈（光标移动、选区变化不算）
        if (changed && !undone && *value != before_text)
            detail::edit_snapshot(impl, id, before_text, caret0, anchor0, edit_kind);
    }

    // ---------------- 绘制 ----------------
    detail::Skin sk = detail::make_skin(th, st, Style::Outline, rad);
    const float t_hover = detail::animate_value(impl, id * 31u + 1u, (in.hovered && !focused) ? 1.0f : 0.0f, 18.0f);
    Color bg = !st.bg.is_none() ? st.bg : th.background;
    if (st.disabled && bg.a > 0) bg.a = uint8_t(float(bg.a) * 0.45f);
    if (bg.a > 0) fill_rect(box, bg, rad);
    Color bd = !st.border_color.is_none() ? st.border_color : th.input;
    if (focused) bd = th.ring;
    else if (t_hover > 0.01f) bd = bd.mix(th.foreground.with_alpha(120), t_hover * 0.4f);
    float bw = st.border_width >= 0.0f ? st.border_width : 1.0f;
    if (bw > 0.0f) stroke_rect(box, bd, rad, focused ? bw + 0.4f : bw);

    const bool show_placeholder = value->empty() && !st.placeholder.empty();
    const std::string& shown = show_placeholder ? st.placeholder : *value;
    const float shown_w = show_placeholder ? text_width(st.placeholder, fs, 400) : tw;

    // 水平滚动：保证光标可见
    float scroll = 0.0f;
    if (shown_w > avail) {
        float caret_x = text_width(utf8_from_codepoints(utf8_codepoints(*value), 0, size_t(impl->caret)), fs, 400);
        if (caret_x - scroll > avail) scroll = caret_x - avail;
        if (caret_x - scroll < 0.0f) scroll = caret_x;
        scroll = detail::clampf(scroll, 0.0f, std::max(0.0f, shown_w - avail));
    }

    Rect text_box(box.x + pad - scroll, box.y, std::max(box.w - pad * 2.0f, 1.0f), box.h);
    Style ts;
    ts.font_size = fs;
    ts.font_weight = 400;
    ts.align = Align::Left;
    ts.valign = VAlign::Middle;
    push_clip(box);   // 文本与选区都不得溢出输入框

    if (focused && impl->sel_anchor >= 0) {
        int sf = std::min(impl->caret, impl->sel_anchor), stt = std::max(impl->caret, impl->sel_anchor);
        std::vector<uint32_t> cps = utf8_codepoints(*value);
        float x0 = text_width(utf8_from_codepoints(cps, 0, size_t(sf)), fs, 400);
        float x1 = text_width(utf8_from_codepoints(cps, 0, size_t(stt)), fs, 400);
        fill_rect(Rect(box.x + pad + x0 - scroll, box.cy() - fs * 0.72f, x1 - x0, fs * 1.44f),
                  th.primary.with_alpha(52), 2.0f);
    }

    Color fg = !st.fg.is_none() ? st.fg : (show_placeholder ? th.muted_foreground : th.foreground);
    if (st.disabled) fg.a = uint8_t(float(fg.a) * 0.45f);
    draw_text(shown, text_box, fg, ts);

    // 光标位置（不管闪不闪都要算：IME 的组合窗/候选窗跟着它走）
    float caret_x = box.x + pad;
    {
        std::vector<uint32_t> cps = utf8_codepoints(*value);
        float cx = text_width(utf8_from_codepoints(cps, 0, size_t(impl->caret)), fs, 400);
        caret_x = detail::clampf(box.x + pad + cx - scroll, box.x + pad - 1.0f, box.right() - pad);
    }
    if (focused && std::fmod(impl->caret_blink, 1.06f) < 0.62f) {
        float ch = fs * 1.15f;
        fill_rect(Rect(caret_x, box.cy() - ch * 0.5f, 1.4f, ch), th.foreground, 0.7f);
        repaint();   // 光标要闪，持续要帧
    }
    // 输入法：把拼音串钉在输入框下沿、候选框再往下一点。
    // 不做这一步，系统会把它们放在客户区左上角，和输入框完全对不上。
    if (focused && impl->ime_pos) {
        const Rect pb = impl->phys(Rect(caret_x, box.y, 1.0f, box.h));
        impl->ime_pos(pb.x, pb.y, pb.bottom());
    }
    pop_clip();

    if (detail::is_focused(impl, id) && focused)
        detail::paint_focus_ring(*this, box, rad, th.ring);

    return changed;
}

// -----------------------------------------------------------------------------
//  checkbox
// -----------------------------------------------------------------------------
inline bool Ui::checkbox(bool* value, const std::string& label_text, const Rect& box, const Style& st) {
    if (!value) return false;
    const Theme& th = impl->theme;
    uint64_t id = detail::make_id("checkbox", box, detail::hash_string(label_text.c_str()));
    const bool enabled = !st.disabled;

    detail::Interact in = detail::interact(impl, id, box, enabled);
    if (in.hovered) set_cursor(Cursor::Hand);
    bool changed = false;
    if (in.clicked) { *value = !*value; changed = true; }

    const float t = detail::animate_value(impl, id * 37u + 1u, *value ? 1.0f : 0.0f, 20.0f);
    const float t_hover = detail::animate_value(impl, id * 37u + 2u, in.hovered ? 1.0f : 0.0f, 18.0f);

    const float box_size = st.font_size > 0.0f ? st.font_size + 4.0f : 16.0f;
    Rect box_r(box.x, box.cy() - box_size * 0.5f, box_size, box_size);
    const float rad = 4.0f;

    Color off_bd = th.input;
    if (t_hover > 0.01f) off_bd = off_bd.mix(th.foreground.with_alpha(110), t_hover * 0.55f);
    Color fill_c = th.primary.mix(th.primary, t);
    Color bd_c = off_bd.mix(th.primary, t);
    if (st.disabled) { fill_c.a = uint8_t(float(fill_c.a) * 0.45f); bd_c.a = uint8_t(float(bd_c.a) * 0.45f); }

    if (t > 0.02f) {
        Color bg = fill_c;
        bg.a = uint8_t(float(bg.a) * t);
        fill_rect(box_r, bg, rad);
    }
    stroke_rect(box_r, bd_c, rad, 1.0f);

    if (t > 0.30f) {
        // 对勾用两条线自绘 —— 不依赖字体里是否有 ✓（很多中文字体没有这个字形）
        Color fg = th.primary_foreground;
        fg.a = uint8_t(float(fg.a) * detail::clampf((t - 0.30f) / 0.45f, 0.0f, 1.0f));
        const float cx = box_r.cx(), cy = box_r.cy();
        const float s = box_size * 0.22f;
        const float lw = std::max(1.4f, box_size * 0.11f);
        draw_line(cx - s * 1.20f, cy + s * 0.05f, cx - s * 0.15f, cy + s * 0.90f, fg, lw);
        draw_line(cx - s * 0.15f, cy + s * 0.90f, cx + s * 1.30f, cy - s * 0.85f, fg, lw);
    }

    if (!label_text.empty()) {
        Rect lr(box.x + box_size + 8.0f, box.y, std::max(box.w - box_size - 8.0f, 1.0f), box.h);
        Style ts;
        ts.font_size = st.font_size > 0.0f ? st.font_size : th.font_size;
        ts.font_weight = 500;
        ts.align = Align::Left;
        ts.valign = VAlign::Middle;
        Color c = !st.fg.is_none() ? st.fg : th.foreground;
        if (st.disabled) c.a = uint8_t(float(c.a) * 0.45f);
        draw_text(label_text, lr, c, ts);
    }

    if (in.pressed) detail::grab_focus(impl, id);
    if (detail::is_focused(impl, id)) detail::paint_focus_ring(*this, box_r, rad, th.ring);
    return changed;
}

// -----------------------------------------------------------------------------
//  toggle —— shadcn 的 Switch
// -----------------------------------------------------------------------------
inline bool Ui::toggle(bool* value, const Rect& box, const Style& st) {
    if (!value) return false;
    const Theme& th = impl->theme;
    uint64_t id = detail::make_id("toggle", box);
    const bool enabled = !st.disabled;

    detail::Interact in = detail::interact(impl, id, box, enabled);
    if (in.hovered) set_cursor(Cursor::Hand);
    bool changed = false;
    if (in.clicked) { *value = !*value; changed = true; }

    const float t = detail::animate_value(impl, id * 41u + 1u, *value ? 1.0f : 0.0f, 22.0f);
    const float t_hover = detail::animate_value(impl, id * 41u + 2u, in.hovered ? 1.0f : 0.0f, 18.0f);

    const float track_h = st.font_size > 0.0f ? st.font_size + 6.0f : 20.0f;
    const float track_w = track_h * 1.8f;
    Rect track = Rect::centered(box.cx(), box.cy(), track_w, track_h);
    const float rad = track_h * 0.5f;

    Color off = th.input;
    if (t_hover > 0.01f) off = off.mix(th.foreground.with_alpha(110), t_hover * 0.5f);
    Color track_c = off.mix(th.primary, t);
    if (st.disabled) track_c.a = uint8_t(float(track_c.a) * 0.45f);
    fill_rect(track, track_c, rad);

    const float knob_r = track_h * 0.5f - 2.5f;
    const float kx = detail::lerpf(track.x + track_h * 0.5f, track.right() - track_h * 0.5f, t);
    Color knob = Color(255, 255, 255);
    if (th.primary.luminance() > 0.6f && t > 0.5f) knob = Color(0x18, 0x18, 0x1B);
    if (st.disabled) knob.a = 200;
    fill_circle(kx, track.cy(), knob_r, knob);

    if (in.pressed) detail::grab_focus(impl, id);
    if (detail::is_focused(impl, id)) detail::paint_focus_ring(*this, track, rad, th.ring);
    return changed;
}

// -----------------------------------------------------------------------------
//  slider
// -----------------------------------------------------------------------------
inline bool Ui::slider(float* value, float min_value, float max_value, const Rect& box, const Style& st) {
    if (!value) return false;
    const Theme& th = impl->theme;
    uint64_t id = detail::make_id("slider", box);
    const bool enabled = !st.disabled;

    Rect hit(box.x, box.y, box.w, box.h);
    detail::Interact in = detail::interact(impl, id, hit, enabled);
    if (in.hovered || impl->active == id) set_cursor(Cursor::ResizeH);

    bool changed = false;
    float t = (max_value > min_value) ? detail::clampf((*value - min_value) / (max_value - min_value), 0.0f, 1.0f) : 0.0f;

    if (impl->active == id && impl->input.down[0]) {
        float nt = detail::clampf((impl->input.mouse.x - box.x) / std::max(box.w, 1.0f), 0.0f, 1.0f);
        float nv = min_value + (max_value - min_value) * nt;
        if (std::fabs(nv - *value) > 1e-6f) { *value = nv; changed = true; }
        t = nt;
    }
    const float t_hover = detail::animate_value(impl, id * 43u + 1u, (in.hovered || impl->active == id) ? 1.0f : 0.0f, 18.0f);

    const float track_h = 4.0f;
    Rect track(box.x + 7.0f, box.cy() - track_h * 0.5f, std::max(box.w - 14.0f, 1.0f), track_h);
    const float rad = track_h * 0.5f;

    Color track_c = !st.border_color.is_none() ? st.border_color : th.muted;
    fill_rect(track, track_c, rad);
    Color fill_c = !st.bg.is_none() ? st.bg : th.primary;
    if (st.disabled) fill_c.a = uint8_t(float(fill_c.a) * 0.45f);
    if (t > 0.0f) fill_rect(Rect(track.x, track.y, track.w * t, track.h), fill_c, rad);

    const float knob_r = detail::lerpf(7.0f, 8.0f, t_hover);
    const float kx = track.x + track.w * t;
    Color knob = th.background;
    if (knob.luminance() < 0.5f) knob = th.foreground;
    knob = th.background;
    fill_circle(kx, track.cy(), knob_r, knob);
    stroke_rect(Rect::centered(kx, track.cy(), knob_r * 2.0f, knob_r * 2.0f),
                fill_c.is_none() ? th.primary : fill_c, knob_r, 2.0f);

    if (in.pressed) detail::grab_focus(impl, id);
    return changed;
}

// -----------------------------------------------------------------------------
//  tabs —— shadcn 的 TabsList（muted 容器 + 选中项浮起）
// -----------------------------------------------------------------------------
inline bool Ui::tabs(const std::vector<std::string>& labels, int* index, const Rect& box, const Style& st) {
    if (!index || labels.empty()) return false;
    const Theme& th = impl->theme;
    uint64_t base_id = detail::make_id("tabs", box);
    const float rad = st.radius >= 0.0f ? st.radius : th.radius;
    const float inner_rad = std::max(0.0f, rad - 3.0f);
    const float fs = st.font_size > 0.0f ? st.font_size : th.font_size;

    bool changed = false;
    Rect container(box.x, box.y, box.w, box.h);
    Color container_bg = !st.bg.is_none() ? st.bg : th.muted;
    if (st.disabled) container_bg.a = uint8_t(float(container_bg.a) * 0.45f);
    fill_rect(container, container_bg, rad);

    Rect inner = container.inset(3.0f, 3.0f);
    const float item_w = inner.w / float(labels.size());

    for (size_t i = 0; i < labels.size(); ++i) {
        Rect item(inner.x + item_w * float(i), inner.y, item_w, inner.h);
        uint64_t id = detail::make_id("tab", item, base_id + i);
        detail::Interact in = detail::interact(impl, id, item, !st.disabled);
        if (in.hovered) set_cursor(Cursor::Hand);
        if (in.clicked && *index != int(i)) { *index = int(i); changed = true; }

        const float t_sel = detail::animate_value(impl, id * 47u + 1u, (*index == int(i)) ? 1.0f : 0.0f, 22.0f);
        const float t_hov = detail::animate_value(impl, id * 47u + 2u, in.hovered ? 1.0f : 0.0f, 18.0f);

        if (t_sel > 0.02f) {
            Color sel_bg = !st.border_color.is_none() ? st.border_color : th.background;
            // 在深色主题里选中项要略亮才看得见
            if (sel_bg.luminance() < 0.2f) sel_bg = sel_bg.lighten(0.07f);
            sel_bg.a = uint8_t(float(sel_bg.a) * t_sel);
            fill_rect(item, sel_bg, inner_rad);
        } else if (t_hov > 0.02f) {
            fill_rect(item, th.foreground.with_alpha(uint8_t(14.0f * t_hov)), inner_rad);
        }

        Style ts;
        ts.font_size = fs;
        ts.font_weight = *index == int(i) ? 600 : 500;
        ts.align = Align::Center;
        ts.valign = VAlign::Middle;
        Color c = (*index == int(i)) ? th.foreground : th.muted_foreground;
        if (st.disabled) c.a = uint8_t(float(c.a) * 0.45f);
        draw_text(labels[i], item, c, ts);
    }
    return changed;
}

// -----------------------------------------------------------------------------
//  segmented —— 与 tabs 同构，语义上用于"选择值"
// -----------------------------------------------------------------------------
inline bool Ui::segmented(const std::vector<std::string>& items, int* index, const Rect& box, const Style& st) {
    return tabs(items, index, box, st);
}

// -----------------------------------------------------------------------------
//  select —— 下拉选择。弹层用 deferred 画到帧末，保证盖在其它组件之上
// -----------------------------------------------------------------------------
inline bool Ui::select(const std::vector<std::string>& items, int* index, const Rect& box, const Style& st) {
    if (!index || items.empty()) return false;
    const Theme& th = impl->theme;
    uint64_t id = detail::make_id("select", box);
    const float rad = st.radius >= 0.0f ? st.radius : th.radius_sm;
    const float fs = st.font_size > 0.0f ? st.font_size : th.font_size;
    const bool enabled = !st.disabled;

    bool open = impl->flags[id] > 0.5f;
    detail::Interact in = detail::interact(impl, id, box, enabled);
    if (in.hovered) set_cursor(Cursor::Hand);

    bool changed = false;
    if (in.clicked) { open = !open; impl->flags[id] = open ? 1.0f : 0.0f; }

    const float t_hover = detail::animate_value(impl, id * 53u + 1u, (in.hovered || open) ? 1.0f : 0.0f, 18.0f);

    // --- 主体 ---
    Color bg = !st.bg.is_none() ? st.bg : th.background;
    if (bg.a > 0) fill_rect(box, bg, rad);
    Color bd = !st.border_color.is_none() ? st.border_color : th.input;
    if (open) bd = th.ring;
    else if (t_hover > 0.01f) bd = bd.mix(th.foreground.with_alpha(120), t_hover * 0.4f);
    stroke_rect(box, bd, rad, st.border_width >= 0.0f ? st.border_width : 1.0f);

    const float pad = 12.0f;
    const std::string cur = (*index >= 0 && *index < int(items.size())) ? items[size_t(*index)] : std::string();
    Rect label_box(box.x + pad, box.y, std::max(box.w - pad * 2.0f - 18.0f, 1.0f), box.h);
    Style ts;
    ts.font_size = fs;
    ts.font_weight = 500;
    ts.align = Align::Left;
    ts.valign = VAlign::Middle;
    Color fg = !st.fg.is_none() ? st.fg : (cur.empty() ? th.muted_foreground : th.foreground);
    draw_text(cur.empty() && !st.placeholder.empty() ? st.placeholder : cur, label_box, fg, ts);

    // 下拉箭头自绘（不依赖字体字形）
    {
        const float ccx = box.right() - pad - 5.0f;
        const float ccy = box.cy() + 0.5f;
        const float s = 3.6f;
        const Color cc = th.muted_foreground;
        draw_line(ccx - s, ccy - s * 0.45f, ccx, ccy + s * 0.55f, cc, 1.6f);
        draw_line(ccx, ccy + s * 0.55f, ccx + s, ccy - s * 0.45f, cc, 1.6f);
    }

    if (detail::is_focused(impl, id)) detail::paint_focus_ring(*this, box, rad, th.ring);

    // --- 弹层 ---
    if (open) {
        const float item_h = std::max(box.h, 32.0f);
        const float list_h = item_h * float(items.size()) + 8.0f;
        float ly = box.bottom() + 4.0f;
        if (ly + list_h > height() - 4.0f) ly = std::max(4.0f, box.y - 4.0f - list_h);
        Rect list(box.x, ly, box.w, list_h);

        std::vector<std::string> items_copy = items;
        int* idx_ptr = index;
        const Style st_copy = st;
        const Rect box_copy = box;

        deferred([this, id, list, item_h, items_copy, idx_ptr, rad, fs, box_copy, st_copy, th]() mutable {
            // 点击外部关闭
            const Input& kin = impl->input;
            bool inside_list = list.contains(kin.mouse.x, kin.mouse.y);
            bool inside_body = box_copy.contains(kin.mouse.x, kin.mouse.y);
            bool click_outside = (kin.pressed[0] || kin.pressed[1]) && !inside_list && !inside_body;

            // 阴影 + 面板
            drop_shadow(list, rad, 10.0f, Color(0, 0, 0, 34), 3.0f);
            fill_rect(list, th.popover, rad);
            stroke_rect(list, th.border, rad, 1.0f);
            push_clip(list);

            const float pad = 4.0f;
            int clicked_index = -1;
            for (size_t i = 0; i < items_copy.size(); ++i) {
                Rect item(list.x + pad, list.y + pad + item_h * float(i), list.w - pad * 2.0f, item_h);
                detail::Interact ii = detail::interact(impl, detail::make_id("select_item", item), item, true);
                if (ii.hovered) set_cursor(Cursor::Hand);

                const float t_h = detail::animate_value(impl, detail::hash_string("si") + i * 97u + uint64_t(list.y),
                                                        ii.hovered ? 1.0f : 0.0f, 20.0f);
                if (t_h > 0.02f) fill_rect(item, th.accent.mix(th.foreground.with_alpha(10), t_h * 0.3f), rad - 2.0f);

                Style its;
                its.font_size = fs;
                its.font_weight = (*idx_ptr == int(i)) ? 600 : 500;
                its.align = Align::Left;
                its.valign = VAlign::Middle;
                draw_text(items_copy[i], item.inset(10.0f, 0.0f), th.popover_foreground, its);

                if (*idx_ptr == int(i)) {
                    const float ccx = item.right() - 16.0f, ccy = item.cy();
                    const float s = 3.4f;
                    draw_line(ccx - s * 1.2f, ccy + 0.5f, ccx - s * 0.2f, ccy + s * 0.9f, th.foreground, 1.7f);
                    draw_line(ccx - s * 0.2f, ccy + s * 0.9f, ccx + s * 1.2f, ccy - s * 0.9f, th.foreground, 1.7f);
                }
                if (ii.clicked) clicked_index = int(i);
            }
            pop_clip();

            if (clicked_index >= 0) {
                *idx_ptr = clicked_index;
                impl->flags[id] = 0.0f;
                if (impl->request_repaint) impl->request_repaint();
            } else if (click_outside) {
                impl->flags[id] = 0.0f;
                if (impl->request_repaint) impl->request_repaint();
            }
        });
        repaint();
    }

    return changed;
}

// =============================================================================
//  8.13  表单补充
// =============================================================================

// -----------------------------------------------------------------------------
//  radio_group —— 单选组
//  纵向等分 box.h，每行"圆环 + 标签"。选中态是 shadcn 的做法：外圈描边变粗
//  并转成 primary，中心长出一个实心点（靠一个动画量 t 同时驱动两者）。
// -----------------------------------------------------------------------------
inline bool Ui::radio_group(const std::vector<std::string>& items, int* index, const Rect& box, const Style& st) {
    if (!index || items.empty() || box.h <= 0.0f) return false;
    const Theme& th = impl->theme;
    uint64_t base = detail::make_id("radiogroup", box);
    const float fs = st.font_size > 0.0f ? st.font_size : th.font_size;
    const float row_h = box.h / float(items.size());
    const float dot = std::max(14.0f, fs + 2.0f);

    bool changed = false;
    for (size_t i = 0; i < items.size(); ++i) {
        Rect row(box.x, box.y + row_h * float(i), box.w, row_h);
        uint64_t id = detail::make_id("radio", row, base + i);
        detail::Interact in = detail::interact(impl, id, row, !st.disabled);
        if (in.hovered) set_cursor(Cursor::Hand);
        if (in.pressed) detail::grab_focus(impl, id);
        if (in.clicked && *index != int(i)) { *index = int(i); changed = true; }

        const float t     = detail::animate_value(impl, id * 59u + 1u, (*index == int(i)) ? 1.0f : 0.0f, 22.0f);
        const float t_hov = detail::animate_value(impl, id * 59u + 2u, in.hovered ? 1.0f : 0.0f, 18.0f);

        const Rect dot_r = Rect::centered(row.x + dot * 0.5f, row.cy(), dot, dot);
        Color bd = th.input;
        if (t < 0.5f && t_hov > 0.01f) bd = bd.mix(th.foreground.with_alpha(110), t_hov * 0.55f);
        bd = bd.mix(th.primary, t);
        if (st.disabled) bd.a = uint8_t(float(bd.a) * 0.45f);
        stroke_rect(dot_r, bd, dot * 0.5f, detail::lerpf(1.0f, 1.8f, t));

        if (t > 0.02f) {
            // 中心点用 t*(2-t) 做一次 ease-out，弹出感比线性更像"长出来"
            const float ir = (dot * 0.5f - std::max(3.2f, dot * 0.24f)) * (t * (2.0f - t));
            if (ir > 0.3f) {
                Color fg = th.primary;
                if (st.disabled) fg.a = uint8_t(float(fg.a) * 0.45f);
                fill_circle(dot_r.cx(), dot_r.cy(), ir, fg);
            }
        }

        Style ts;
        ts.font_size = fs;
        ts.font_weight = (*index == int(i)) ? 500 : 400;
        ts.align = Align::Left;
        ts.valign = VAlign::Middle;
        Color c = !st.fg.is_none() ? st.fg : th.foreground;
        if (st.disabled) c.a = uint8_t(float(c.a) * 0.45f);
        draw_text(items[i], Rect(row.x + dot + 9.0f, row.y, std::max(row.w - dot - 9.0f, 1.0f), row.h), c, ts);

        if (detail::is_focused(impl, id)) detail::paint_focus_ring(*this, dot_r, dot * 0.5f, th.ring);
    }
    return changed;
}

// -----------------------------------------------------------------------------
//  stepper —— 数值步进器 [−] value [+]
// -----------------------------------------------------------------------------
inline bool Ui::stepper(float* value, float step, float min_value, float max_value, const Rect& box, const Style& st) {
    if (!value || box.w <= 12.0f) return false;
    const Theme& th = impl->theme;
    uint64_t base = detail::make_id("stepper", box);
    const float rad = st.radius >= 0.0f ? st.radius : th.radius_sm;
    const float fs  = st.font_size > 0.0f ? st.font_size : th.font_size;
    const float btn_w = std::min(box.h, 34.0f);

    const Rect minus_r(box.x, box.y, btn_w, box.h);
    const Rect plus_r(box.right() - btn_w, box.y, btn_w, box.h);
    const Rect value_r(box.x + btn_w, box.y, std::max(box.w - btn_w * 2.0f, 1.0f), box.h);

    // 底 + 框（整块画一次，按钮只画自己的 hover 底）
    Color bg = !st.bg.is_none() ? st.bg : th.background;
    if (st.disabled && bg.a > 0) bg.a = uint8_t(float(bg.a) * 0.45f);
    if (bg.a > 0) fill_rect(box, bg, rad);
    Color bd = !st.border_color.is_none() ? st.border_color : th.input;
    if (st.disabled) bd.a = uint8_t(float(bd.a) * 0.45f);
    stroke_rect(box, bd, rad, st.border_width >= 0.0f ? st.border_width : 1.0f);

    auto draw_btn = [&](const Rect& r, uint64_t id, bool plus) -> bool {
        detail::Interact in = detail::interact(impl, id, r, !st.disabled);
        if (in.hovered) set_cursor(Cursor::Hand);
        if (in.pressed) detail::grab_focus(impl, id);
        const float t_h = detail::animate_value(impl, id * 61u + 1u, in.hovered ? 1.0f : 0.0f, 18.0f);
        const float t_p = detail::animate_value(impl, id * 61u + 2u, (in.held && in.hovered) ? 1.0f : 0.0f, 30.0f);
        const float a = std::max(t_h * 0.85f, t_p * 0.6f);
        if (a > 0.02f) {
            Color hl = th.accent.mix(th.foreground.with_alpha(12), t_p * 0.5f);
            hl.a = uint8_t(float(hl.a) * a);
            fill_rect(r.inset(3.0f), hl, std::max(0.0f, rad - 2.0f));
        }
        Color c = !st.fg.is_none() ? st.fg : th.foreground;
        if (st.disabled) c.a = uint8_t(float(c.a) * 0.45f);
        const float lw = std::max(1.5f, fs * 0.13f);
        const float s  = fs * 0.34f;
        draw_line(r.cx() - s, r.cy(), r.cx() + s, r.cy(), c, lw);
        if (plus) draw_line(r.cx(), r.cy() - s, r.cx(), r.cy() + s, c, lw);
        if (detail::is_focused(impl, id)) detail::paint_focus_ring(*this, r.inset(3.0f), rad, th.ring);
        return in.clicked;
    };

    bool changed = false;
    if (draw_btn(minus_r, base + 1u, false)) {
        const float nv = detail::clampf(*value - step, min_value, max_value);
        if (nv != *value) { *value = nv; changed = true; }
    }
    if (draw_btn(plus_r, base + 2u, true)) {
        const float nv = detail::clampf(*value + step, min_value, max_value);
        if (nv != *value) { *value = nv; changed = true; }
    }

    // 分隔线 + 数值
    fill_rect(Rect(minus_r.right(), box.y + 4.0f, 1.0f, box.h - 8.0f), th.border, 0.0f);
    fill_rect(Rect(plus_r.x - 1.0f, box.y + 4.0f, 1.0f, box.h - 8.0f), th.border, 0.0f);

    char buf[64];
    if (std::fabs(*value - std::floor(*value + 0.5f)) < 0.0005f)
        std::snprintf(buf, sizeof(buf), "%.0f", double(*value));
    else
        std::snprintf(buf, sizeof(buf), "%.2f", double(*value));

    Style ts;
    ts.font_size = fs;
    ts.font_weight = 500;
    ts.align = Align::Center;
    ts.valign = VAlign::Middle;
    Color fc = !st.fg.is_none() ? st.fg : th.foreground;
    if (st.disabled) fc.a = uint8_t(float(fc.a) * 0.45f);
    push_clip(value_r);
    draw_text(buf, value_r, fc, ts);
    pop_clip();
    return changed;
}

// -----------------------------------------------------------------------------
//  textarea —— 多行文本框
//  与 input 共享同一套编辑状态（impl->caret / sel_anchor），差别只在"坐标 <-> 码点"
//  的换算要过一层行号。'\n' 硬换行，不做软换行；超出视口时内部纵向滚动。
// -----------------------------------------------------------------------------
inline bool Ui::textarea(std::string* value, const Rect& box, const Style& st) {
    if (!value) return false;
    const Theme& th = impl->theme;
    uint64_t id = detail::make_id("textarea", box);
    const bool enabled = !st.disabled;
    const float rad  = st.radius >= 0.0f ? st.radius : th.radius_sm;
    const float padx = st.pad_x >= 0.0f ? st.pad_x : 10.0f;
    const float pady = st.pad_y >= 0.0f ? st.pad_y : 8.0f;
    const float fs   = st.font_size > 0.0f ? st.font_size : th.font_size;
    const float lh   = std::max(line_height(fs), fs * 1.15f);

    detail::Interact in = detail::interact(impl, id, box, enabled);
    bool focused = detail::is_focused(impl, id);
    if (in.hovered || focused) set_cursor(Cursor::IBeam);

    bool changed = false;
    if (focused && impl->input.pressed[0] && !in.hovered) { detail::drop_focus(impl); focused = false; }
    if (in.pressed) { detail::grab_focus(impl, id); focused = true; }

    auto split_lines = [](const std::string& s) {
        std::vector<std::string> out;
        std::string cur;
        for (char ch : s) {
            if (ch == '\n') { out.push_back(cur); cur.clear(); }
            else cur.push_back(ch);
        }
        out.push_back(cur);            // 末尾的 '\n' 也算一个空行
        return out;
    };

    std::vector<std::string> lines = split_lines(*value);

    auto to_linecol = [&](int cp) -> std::pair<int, int> {
        int remain = cp < 0 ? 0 : cp;
        for (size_t i = 0; i < lines.size(); ++i) {
            const int n = utf8_count(lines[i]);
            if (remain <= n) return std::make_pair(int(i), remain);
            remain -= n + 1;           // +1 吃掉换行符
        }
        const int last = int(lines.size()) - 1;
        return std::make_pair(last, utf8_count(lines[size_t(last)]));
    };
    auto to_caret = [&](int ln, int col) {
        int cp = 0;
        for (int i = 0; i < ln && i < int(lines.size()); ++i) cp += utf8_count(lines[i]) + 1;
        return cp + (col < 0 ? 0 : col);
    };
    auto line_x = [&](int ln, int col) {
        if (ln < 0 || ln >= int(lines.size())) return 0.0f;
        const int n = utf8_count(lines[size_t(ln)]);
        if (col > n) col = n;
        const std::vector<uint32_t> cps = utf8_codepoints(lines[size_t(ln)]);
        return text_width(utf8_from_codepoints(cps, 0, size_t(col)), fs, 400);
    };

    float& tscroll = impl->flags[id * 89u + 1u];
    const float view_h   = std::max(box.h - pady * 2.0f, 1.0f);
    const float need_h   = lh * float(lines.size());
    const float max_off  = std::max(0.0f, need_h - view_h);
    int   n_cp     = utf8_count(*value);

    if (max_off > 0.0f && in.hovered && std::fabs(impl->input.wheel) > 0.001f) {
        tscroll = detail::clampf(tscroll - impl->input.wheel * 40.0f, 0.0f, max_off);
        repaint();
    }

    if (focused) {
        if (impl->caret > n_cp) impl->caret = n_cp;
        if (impl->caret < 0) impl->caret = 0;

        // 鼠标 → 光标：单击定位 / 拖选 / 双击选词 / 三击全选 / Shift 扩选
        auto caret_at_mouse = [&]() -> int {
            const float ly = impl->input.mouse.y - (box.y + pady) + tscroll;
            int ln = int(std::floor(ly / lh));
            if (ln < 0) ln = 0;
            if (ln > int(lines.size()) - 1) ln = int(lines.size()) - 1;
            const int col = detail::caret_from_x(*this, lines[size_t(ln)],
                                                 impl->input.mouse.x - (box.x + padx), fs, 400);
            return to_caret(ln, col);
        };
        if (impl->input.pressed[0] && in.hovered) {
            impl->caret = caret_at_mouse();
            if (impl->input.click_count >= 3) {
                impl->sel_anchor = 0;
                impl->caret = n_cp;
            } else if (impl->input.click_count == 2) {
                const std::vector<uint32_t> cps = utf8_codepoints(*value);
                int a = 0, b = 0;
                detail::word_span(cps, impl->caret, &a, &b);
                impl->sel_anchor = a;
                impl->caret = b;
            } else if (!impl->input.shift()) {
                impl->sel_anchor = -1;
            }
            impl->flags[id * 89u + 3u] = float(impl->caret);      // 拖选锚点
        }
        if (impl->input.down[0] && impl->active == id && in.hovered) {
            const int prev = impl->caret;
            impl->caret = caret_at_mouse();
            if (impl->caret != prev && impl->sel_anchor < 0)
                impl->sel_anchor = int(impl->flags[id * 89u + 3u]);
        }

        const Input& kin = impl->input;

        // ---- 撤销 / 重做 ----
        bool undone = false;
        if (kin.ctrl_of('Z') || kin.ctrl_of('Y')) {
            const bool redo = kin.key_pressed['Y'] || (kin.key_pressed['Z'] && kin.shift_of('Z'));
            if (redo ? detail::edit_redo(impl, id, value, &impl->caret, &impl->sel_anchor)
                     : detail::edit_undo(impl, id, value, &impl->caret, &impl->sel_anchor)) {
                n_cp = utf8_count(*value);
                impl->caret = detail::clampi(impl->caret, 0, n_cp);
                impl->caret_blink = 0.0f;
                changed = true;
                undone = true;
            }
        }

        const std::string before_text = *value;
        const int caret0 = impl->caret, anchor0 = impl->sel_anchor;
        uint64_t edit_kind = detail::EDIT_OTHER;

        int sel_from = impl->sel_anchor >= 0 ? std::min(impl->caret, impl->sel_anchor) : impl->caret;
        int sel_to   = impl->sel_anchor >= 0 ? std::max(impl->caret, impl->sel_anchor) : impl->caret;
        bool has_sel = sel_to > sel_from;

        auto delete_sel = [&]() -> bool {
            if (!has_sel) return false;
            const std::vector<uint32_t> cps = utf8_codepoints(*value);
            std::vector<uint32_t> keep;
            keep.reserve(cps.size());
            for (int i = 0; i < int(cps.size()); ++i)
                if (i < sel_from || i >= sel_to) keep.push_back(cps[i]);
            *value = utf8_from_codepoints(keep, 0, keep.size());
            impl->caret = sel_from;
            impl->sel_anchor = -1;
            changed = true;
            return true;
        };
        auto insert_text = [&](const std::string& ins) {
            delete_sel();
            std::vector<uint32_t> cps = utf8_codepoints(*value);
            const std::vector<uint32_t> add = utf8_codepoints(ins);
            cps.insert(cps.begin() + impl->caret, add.begin(), add.end());
            *value = utf8_from_codepoints(cps, 0, cps.size());
            impl->caret += int(add.size());
            impl->caret_blink = 0.0f;
            changed = true;
        };
        // 选中 [from, to) 后删掉（按词删用的）
        auto delete_span = [&](int from, int to) {
            if (to <= from) return;
            impl->sel_anchor = from;
            impl->caret = to;
            has_sel = true;
            sel_from = from;
            sel_to = to;
            delete_sel();
        };

        if (!undone && kin.key_pressed[KEY_BACKSPACE]) {
            edit_kind = detail::EDIT_DELETE;
            if ((kin.ctrl_of(KEY_BACKSPACE) || kin.alt_of(KEY_BACKSPACE)) && !has_sel) {
                delete_span(detail::word_left(utf8_codepoints(*value), impl->caret), impl->caret);
            } else if (!delete_sel() && impl->caret > 0) {
                std::vector<uint32_t> cps = utf8_codepoints(*value);
                cps.erase(cps.begin() + (impl->caret - 1));
                *value = utf8_from_codepoints(cps, 0, cps.size());
                impl->caret -= 1;
                changed = true;
            }
        }
        if (!undone && kin.key_pressed[KEY_DELETE]) {
            edit_kind = detail::EDIT_DELETE;
            if ((kin.ctrl_of(KEY_DELETE) || kin.alt_of(KEY_DELETE)) && !has_sel) {
                delete_span(impl->caret, detail::word_right(utf8_codepoints(*value), impl->caret));
            } else if (!delete_sel()) {
                std::vector<uint32_t> cps = utf8_codepoints(*value);
                if (impl->caret < int(cps.size())) {
                    cps.erase(cps.begin() + impl->caret);
                    *value = utf8_from_codepoints(cps, 0, cps.size());
                    changed = true;
                }
            }
        }
        if (!undone && kin.key_pressed[KEY_ENTER]) {
            edit_kind = detail::EDIT_TYPE;
            insert_text(kin.ctrl() || kin.shift() ? "\n\n" : "\n");
        }

        // ←/→：Ctrl 按词跳；Home/End：Ctrl 到文档首尾；Shift 一律扩选
        if (!undone && kin.key_pressed[KEY_LEFT]) {
            if (!kin.shift()) {
                impl->caret = has_sel ? sel_from
                                      : (kin.ctrl() ? detail::word_left(utf8_codepoints(*value), impl->caret)
                                                    : std::max(0, impl->caret - 1));
                impl->sel_anchor = -1;
            } else {
                if (impl->sel_anchor < 0) impl->sel_anchor = has_sel ? sel_to : impl->caret;
                impl->caret = kin.ctrl() ? detail::word_left(utf8_codepoints(*value), impl->caret)
                                         : std::max(0, impl->caret - 1);
            }
            impl->caret_blink = 0.0f;
        }
        if (!undone && kin.key_pressed[KEY_RIGHT]) {
            if (!kin.shift()) {
                impl->caret = has_sel ? sel_to
                                      : (kin.ctrl() ? detail::word_right(utf8_codepoints(*value), impl->caret)
                                                    : std::min(n_cp, impl->caret + 1));
                impl->sel_anchor = -1;
            } else {
                if (impl->sel_anchor < 0) impl->sel_anchor = has_sel ? sel_from : impl->caret;
                impl->caret = kin.ctrl() ? detail::word_right(utf8_codepoints(*value), impl->caret)
                                         : std::min(n_cp, impl->caret + 1);
            }
            impl->caret_blink = 0.0f;
        }
        if (!undone && (kin.key_pressed[KEY_UP] || kin.key_pressed[KEY_DOWN])) {
            const std::pair<int, int> lc = to_linecol(impl->caret);
            const int target = lc.first + (kin.key_pressed[KEY_UP] ? -1 : 1);
            if (target >= 0 && target < int(lines.size())) {
                // 上下移动保持"视觉列"，所以先量出当前行的像素横坐标再用它反查列
                const float x = line_x(lc.first, lc.second);
                const int col = detail::caret_from_x(*this, lines[size_t(target)], x, fs, 400);
                impl->caret = to_caret(target, col);
                impl->caret_blink = 0.0f;
            }
            if (kin.shift()) { if (impl->sel_anchor < 0) impl->sel_anchor = has_sel ? sel_from : impl->caret; }
            else impl->sel_anchor = -1;
        }
        if (!undone && kin.key_pressed[KEY_HOME]) {
            if (kin.shift()) { if (impl->sel_anchor < 0) impl->sel_anchor = impl->caret; }
            else impl->sel_anchor = -1;
            if (kin.ctrl_of(KEY_HOME)) impl->caret = 0;
            else { const std::pair<int, int> lc = to_linecol(impl->caret); impl->caret = to_caret(lc.first, 0); }
            impl->caret_blink = 0.0f;
        }
        if (!undone && kin.key_pressed[KEY_END]) {
            if (kin.shift()) { if (impl->sel_anchor < 0) impl->sel_anchor = impl->caret; }
            else impl->sel_anchor = -1;
            if (kin.ctrl_of(KEY_END)) impl->caret = n_cp;
            else {
                const std::pair<int, int> lc = to_linecol(impl->caret);
                impl->caret = to_caret(lc.first, utf8_count(lines[size_t(lc.first)]));
            }
            impl->caret_blink = 0.0f;
        }
        if (!undone && kin.ctrl() && kin.key_pressed['C']) {
            const std::vector<uint32_t> cps = utf8_codepoints(*value);
            impl->set_clipboard(utf8_from_codepoints(cps, size_t(sel_from), size_t(sel_to - sel_from)));
        }
        if (!undone && kin.ctrl() && kin.key_pressed['X']) {
            edit_kind = detail::EDIT_OTHER;
            const std::vector<uint32_t> cps = utf8_codepoints(*value);
            impl->set_clipboard(utf8_from_codepoints(cps, size_t(sel_from), size_t(sel_to - sel_from)));
            delete_sel();
        }
        if (!undone && kin.ctrl() && kin.key_pressed['V']) {
            edit_kind = detail::EDIT_OTHER;
            if (impl->get_clipboard) insert_text(impl->get_clipboard());
        }
        if (!undone && kin.ctrl() && kin.key_pressed['A']) { impl->sel_anchor = 0; impl->caret = n_cp; }
        if (!undone && kin.key_pressed[KEY_ESC]) { detail::drop_focus(impl); focused = false; }
        // 字符输入（Enter 走上面的 KEY_ENTER，所以这里排除掉 ctrl 组合键）
        if (!undone && !kin.ctrl() && impl->text_veto <= 0.0f && !kin.text_input.empty()) {
            edit_kind = detail::EDIT_TYPE;
            std::string ins;
            for (char ch : kin.text_input) if (ch != '\r') ins.push_back(ch);
            if (!ins.empty()) insert_text(ins);
        }

        if (changed && !undone && *value != before_text)
            detail::edit_snapshot(impl, id, before_text, caret0, anchor0, edit_kind);

        // 编辑完重新分行，否则下面的绘制还在用旧的 lines
        if (changed) lines = split_lines(*value);
    }

    // 光标行保持在视口内（先按光标位置调整，再统一夹到合法区间）
    {
        const std::pair<int, int> lc = to_linecol(impl->caret);
        const float cy0 = lh * float(lc.first);
        const float cy1 = cy0 + lh;
        if (cy0 - tscroll < 0.0f) tscroll = cy0;
        if (cy1 - tscroll > view_h) tscroll = cy1 - view_h;
    }
    tscroll = detail::clampf(tscroll, 0.0f, max_off);

    // ---------------- 绘制 ----------------
    const float t_hover = detail::animate_value(impl, id * 89u + 2u, (in.hovered && !focused) ? 1.0f : 0.0f, 18.0f);
    Color bg = !st.bg.is_none() ? st.bg : th.background;
    if (st.disabled && bg.a > 0) bg.a = uint8_t(float(bg.a) * 0.45f);
    if (bg.a > 0) fill_rect(box, bg, rad);
    Color bd = !st.border_color.is_none() ? st.border_color : th.input;
    if (focused) bd = th.ring;
    else if (t_hover > 0.01f) bd = bd.mix(th.foreground.with_alpha(120), t_hover * 0.4f);
    const float bw = st.border_width >= 0.0f ? st.border_width : 1.0f;
    if (bw > 0.0f) stroke_rect(box, bd, rad, focused ? bw + 0.4f : bw);

    const bool show_ph = value->empty() && !st.placeholder.empty();
    Style ts;
    ts.font_size = fs;
    ts.font_weight = 400;
    ts.align = Align::Left;
    ts.valign = VAlign::Top;

    push_clip(box);

    // 选区：按行画，跨行时每行一段，视觉上连续
    if (focused && impl->sel_anchor >= 0) {
        const int sf  = std::min(impl->caret, impl->sel_anchor);
        const int stt = std::max(impl->caret, impl->sel_anchor);
        int base = 0;
        for (size_t i = 0; i < lines.size(); ++i) {
            const int n = utf8_count(lines[i]);
            const int a = std::max(sf, base) - base;
            const int b = std::min(stt, base + n) - base;
            const float y = box.y + pady + lh * float(i) - tscroll;
            if (b > a) {
                const float x0 = line_x(int(i), a);
                const float x1 = line_x(int(i), b);
                fill_rect(Rect(box.x + padx + x0, y, x1 - x0, lh), th.primary.with_alpha(52), 2.0f);
            }
            // 行尾的换行符也被选中时补一小段，不然"选中换行"看不出反馈
            if (stt > base + n && sf <= base + n)
                fill_rect(Rect(box.x + padx + line_x(int(i), n), y, 6.0f, lh), th.primary.with_alpha(52), 2.0f);
            base += n + 1;
        }
    }

    const Color fg = show_ph ? th.muted_foreground : (!st.fg.is_none() ? st.fg : th.foreground);
    for (size_t i = 0; i < lines.size(); ++i) {
        const float y = box.y + pady + lh * float(i) - tscroll;
        if (y > box.bottom()) break;
        if (y + lh < box.y) continue;
        const std::string text_line = (show_ph && i == 0) ? st.placeholder : lines[i];
        draw_text(text_line, Rect(box.x + padx, y, std::max(box.w - padx * 2.0f, 1.0f), lh), fg, ts);
    }

    // 光标（IME 的组合窗/候选窗也跟着它走，所以位置先算出来）
    if (focused) {
        const std::pair<int, int> lc = to_linecol(impl->caret);
        const float cx = box.x + padx + line_x(lc.first, lc.second);
        float cy = box.y + pady + lh * float(lc.first) - tscroll;
        if (std::fmod(impl->caret_blink, 1.06f) < 0.62f) {
            if (cy + lh > box.y && cy < box.bottom())
                fill_rect(Rect(cx, cy + lh * 0.06f, 1.4f, lh * 0.88f), th.foreground, 0.7f);
            repaint();                 // 光标要闪，持续要帧
        }
        if (impl->ime_pos) {
            // 光标滚出可视区时把 IME 钉在最近的那一行，别让它飞到框外
            const float iy = detail::clampf(cy, box.y, std::max(box.y, box.bottom() - lh));
            const Rect pb = impl->phys(Rect(cx, iy, 1.0f, lh));
            impl->ime_pos(pb.x, pb.y, pb.bottom());
        }
    }

    pop_clip();

    if (focused) detail::paint_focus_ring(*this, box, rad, th.ring);
    return changed;
}

// -----------------------------------------------------------------------------
//  rating —— 星级评分
//  星形是凹多边形，普通圆角矩形光栅化器画不了，所以这里用 raster_poly
//  （奇偶规则扫描线填充）。点第 i 颗星 → *value = i+1；再点当前值 → 清零。
// -----------------------------------------------------------------------------
inline bool Ui::rating(int* value, int max_stars, const Rect& box, const Style& st) {
    if (!value || max_stars <= 0 || max_stars > 32) return false;
    const Theme& th = impl->theme;
    uint64_t base = detail::make_id("rating", box);
    const float gap = 4.0f;
    const float size = std::min((box.w - gap * float(max_stars - 1)) / float(max_stars), box.h);
    if (size <= 3.0f) return false;
    const float top = box.y + (box.h - size) * 0.5f;
    const float r_out = size * 0.5f;
    const float r_in  = r_out * 0.44f;

    Color empty_c = th.muted;
    if (empty_c.luminance() > 0.82f) empty_c = th.border;     // 浅色主题下 muted 太淡，用 border
    const Color full_c = !st.bg.is_none() ? st.bg : th.primary;

    bool changed = false;
    for (int i = 0; i < max_stars; ++i) {
        const Rect sr(box.x + float(i) * (size + gap), top, size, size);
        uint64_t id = detail::make_id("star", sr, base + uint64_t(i));
        detail::Interact in = detail::interact(impl, id, sr, !st.disabled);
        if (in.hovered) set_cursor(Cursor::Hand);
        if (in.pressed) detail::grab_focus(impl, id);
        if (in.clicked) {
            const int nv = (*value == i + 1) ? 0 : (i + 1);
            if (nv != *value) { *value = nv; changed = true; }
        }
        const float t   = detail::animate_value(impl, id * 67u + 1u, (i < *value) ? 1.0f : 0.0f, 24.0f);
        const float t_h = detail::animate_value(impl, id * 67u + 2u, in.hovered ? 1.0f : 0.0f, 18.0f);

        if (!impl->canvas) continue;
        Color c = empty_c.mix(full_c, t);
        if (st.disabled) c.a = uint8_t(float(c.a) * 0.45f);
        const std::vector<Vec2> pts = detail::star_points(impl->px(sr.cx()), impl->px(sr.cy()),
                                                         5, impl->px(r_out), impl->px(r_in));
        detail::raster_poly(*impl->canvas, pts, c);

        // 悬停时勾一圈细边，让"可点"这件事看得出来
        if (t_h > 0.01f && t < 0.9f) {
            const Color hc = th.foreground.with_alpha(uint8_t(80.0f * t_h));
            for (size_t k = 0; k < pts.size(); ++k) {
                const Vec2& p0 = pts[k];
                const Vec2& p1 = pts[(k + 1) % pts.size()];
                detail::raster_line(*impl->canvas, p0.x, p0.y, p1.x, p1.y, hc, impl->px(1.0f));
            }
        }
    }
    return changed;
}

// =============================================================================
//  8.14  展示补充
// =============================================================================

// 头像：圆形底 + 首个码点。传 "张三" 画"张"，传 "AB" 画"A"。
inline void Ui::avatar(const std::string& text, const Rect& box, const Style& st) {
    const Theme& th = impl->theme;
    const float d = std::min(box.w, box.h);
    if (d <= 2.0f) return;
    const Rect c = Rect::centered(box.cx(), box.cy(), d, d);

    Color bg = !st.bg.is_none() ? st.bg : th.muted;
    if (st.disabled) bg.a = uint8_t(float(bg.a) * 0.45f);
    fill_rect(c, bg, d * 0.5f);
    const float bw = st.border_width >= 0.0f ? st.border_width : 0.0f;   // 默认不描边（照片位通常有底图）
    if (bw > 0.0f && !st.border_color.is_none()) stroke_rect(c, st.border_color, d * 0.5f, bw);

    const std::vector<uint32_t> cps = utf8_codepoints(text);
    std::string initial = cps.empty() ? std::string("?") : utf8_from_codepoints(cps, 0, 1);

    Style ts;
    ts.font_size = st.font_size > 0.0f ? st.font_size : d * 0.42f;
    ts.font_weight = 600;
    ts.align = Align::Center;
    ts.valign = VAlign::Middle;
    Color fg = !st.fg.is_none() ? st.fg
             : (bg.luminance() > 0.5f ? th.muted_foreground : th.primary_foreground);
    if (st.disabled) fg.a = uint8_t(float(fg.a) * 0.45f);
    draw_text(initial, c, fg, ts);
}

// 加载指示器：底环 + 一段绕圈旋转的弧。
//
// 两者都是**一次成形的环/弧**（raster_arc），不是拼线段 —— 拼线段会在接缝处
// 叠色，看起来一圈一节一节的"碎片"。也更快：整圈一次扫描 vs 84 次。
//
// 弧身**没有端点**：整圈画满，用沿角度连续变化的 alpha 表示方向（拖尾）。
// 带端点的弧（圆头 / 平头 / 渐隐）都会在转到起点时"断"一下 —— 尾端重新长出来，
// 用户看到的就是接缝。COS 型 alpha 是周期的，首尾数值相等，绕多少圈都无缝。
inline void Ui::spinner(const Rect& box, const Style& st) {
    const Theme& th = impl->theme;
    const float d = std::min(box.w, box.h);
    if (d <= 6.0f) return;

    Color track = !st.border_color.is_none() ? st.border_color : th.muted;
    Color arc   = !st.bg.is_none() ? st.bg : th.primary;
    if (st.disabled) { track.a = uint8_t(float(track.a) * 0.45f); arc.a = uint8_t(float(arc.a) * 0.45f); }

    const bool hovered = box.contains(impl->input.mouse.x, impl->input.mouse.y);
    const bool spin = st.animated || hovered;

    const float cx = impl->px(box.cx());
    const float cy = impl->px(box.cy());
    const float rad = impl->px(std::max(d * 0.5f - 1.5f, 1.0f));
    const float lw  = impl->px(std::max(1.5f, d * 0.12f));

    // 底环：整圈
    detail::raster_arc(*impl->canvas, cx, cy, rad, lw, track, 0.0f, 0.0f, 1.0f);

    float& ang = impl->phase[detail::make_id("spinner_phase", box)];
    if (spin) {
        ang += impl->dt * 4.6f;                    // 角速度：约 0.73 圈/秒
        if (ang > 6.2831853f) ang = std::fmod(ang, 6.2831853f);
        repaint();                                 // 只有"该转"的时候才要帧
    }

    // 拖尾环：头端满色，对面最暗但不是 0（留 14% 底，环始终连成完整一圈）。
    detail::raster_ring_tail(*impl->canvas, cx, cy, rad, lw, arc, ang, 0.14f);
}

// 提示条：图标 + 标题 + 描述。variant = Destructive 时图标/标题转破坏色。
inline void Ui::alert(const std::string& title, const std::string& description, const Rect& box, const Style& st) {
    const Theme& th = impl->theme;
    const float rad = st.radius >= 0.0f ? st.radius : th.radius;
    const float fs  = st.font_size > 0.0f ? st.font_size : th.font_size;
    const float pad = st.pad_x >= 0.0f ? st.pad_x : 14.0f;

    const bool danger = (st.variant == Style::Destructive);
    const bool quiet  = (st.variant == Style::Secondary);

    Color bg = !st.bg.is_none() ? st.bg : (quiet ? th.muted : th.card);
    Color bd = !st.border_color.is_none() ? st.border_color : (danger ? th.destructive.scaled_alpha(0.45f) : th.border);
    const Color head_c = !st.fg.is_none() ? st.fg : (danger ? th.destructive : th.foreground);
    Color icon_c = danger ? th.destructive : th.foreground;
    if (st.disabled) {
        if (bg.a > 0) bg.a = uint8_t(float(bg.a) * 0.45f);
        bd.a = uint8_t(float(bd.a) * 0.45f);
    }

    fill_rect(box, bg, rad);
    if (bd.a > 0) stroke_rect(box, bd, rad, st.border_width >= 0.0f ? st.border_width : 1.0f);

    // 图标：实心圆里的感叹号（自绘 —— 中文字体不一定有 ⓘ / ⚠ 这些字形）
    const float d = std::min(fs + 8.0f, std::max(12.0f, box.h - pad * 2.0f + 6.0f));
    const Rect ic = Rect::centered(box.x + pad + d * 0.5f, box.y + pad + d * 0.5f, d, d);
    fill_circle(ic.cx(), ic.cy(), d * 0.5f, icon_c);
    const Color on_icon = (icon_c.luminance() > 0.55f) ? Color(0x18, 0x18, 0x1B) : Color(255, 255, 255);
    draw_line(ic.cx(), ic.cy() - d * 0.20f, ic.cx(), ic.cy() + d * 0.06f, on_icon, std::max(1.4f, d * 0.14f));
    fill_circle(ic.cx(), ic.cy() + d * 0.22f, std::max(0.9f, d * 0.085f), on_icon);

    const float tx = ic.right() + 10.0f;
    const float tw = std::max(box.right() - pad - tx, 1.0f);
    const float lh = line_height(fs);

    if (description.empty()) {
        Style ts;
        ts.font_size = fs;
        ts.font_weight = 600;
        ts.align = Align::Left;
        ts.valign = VAlign::Middle;
        draw_text(title, Rect(tx, box.y, tw, box.h), head_c, ts);
    } else {
        Style hs;
        hs.font_size = fs;
        hs.font_weight = 600;
        hs.align = Align::Left;
        hs.valign = VAlign::Top;
        draw_text(title, Rect(tx, box.y + pad, tw, lh), head_c, hs);
        Style ds;
        ds.font_size = fs;
        ds.font_weight = 400;
        ds.align = Align::Left;
        ds.valign = VAlign::Top;
        ds.wrap = true;
        draw_text(description, Rect(tx, box.y + pad + lh + 2.0f, tw,
                                    std::max(box.h - pad * 2.0f - lh - 2.0f, 1.0f)),
                  th.muted_foreground, ds);
    }
}

// 面包屑：a › b › c，最后一段高亮
inline void Ui::breadcrumb(const std::vector<std::string>& items, const Rect& box, const Style& st) {
    if (items.empty()) return;
    const Theme& th = impl->theme;
    const float fs = st.font_size > 0.0f ? st.font_size : th.font_size;
    push_clip(box);
    float x = box.x;
    for (size_t i = 0; i < items.size(); ++i) {
        const bool last = (i + 1 == items.size());
        const float w = text_width(items[i], fs, last ? 500 : 400);
        Style ts;
        ts.font_size = fs;
        ts.font_weight = last ? 500 : 400;
        ts.align = Align::Left;
        ts.valign = VAlign::Middle;
        Color c = last ? (!st.fg.is_none() ? st.fg : th.foreground) : th.muted_foreground;
        if (st.disabled) c.a = uint8_t(float(c.a) * 0.45f);
        draw_text(items[i], Rect(x, box.y, w + 1.0f, box.h), c, ts);
        x += w;
        if (!last) {
            const float s = fs * 0.30f;
            const float mx = x + 7.0f, my = box.cy();
            const Color ac = th.muted_foreground.scaled_alpha(0.85f);
            draw_line(mx - s * 0.4f, my - s, mx + s * 0.5f, my, ac, 1.3f);
            draw_line(mx + s * 0.5f, my, mx - s * 0.4f, my + s, ac, 1.3f);
            x += 14.0f;
        }
    }
    pop_clip();
}

// 按键提示键帽
inline void Ui::kbd(const std::string& text, const Rect& box, const Style& st) {
    const Theme& th = impl->theme;
    const float rad = st.radius >= 0.0f ? st.radius : 4.0f;
    const float fs  = st.font_size > 0.0f ? st.font_size : th.font_size - 2.0f;
    Color bg = !st.bg.is_none() ? st.bg : th.muted;
    Color bd = !st.border_color.is_none() ? st.border_color : th.border;
    Color fg = !st.fg.is_none() ? st.fg : th.muted_foreground;
    if (st.disabled) { bg.a = uint8_t(float(bg.a) * 0.45f); bd.a = uint8_t(float(bd.a) * 0.45f); fg.a = uint8_t(float(fg.a) * 0.45f); }
    fill_rect(box, bg, rad);
    // 底边加厚一点，做出"键帽下沿"的立体感
    fill_rect(Rect(box.x + rad, box.bottom() - 2.0f, std::max(box.w - rad * 2.0f, 1.0f), 2.0f), bd, 1.0f);
    stroke_rect(box, bd, rad, 1.0f);
    Style ts;
    ts.font_size = fs;
    ts.font_weight = 500;
    ts.align = Align::Center;
    ts.valign = VAlign::Middle;
    draw_text(text, Rect(box.x, box.y - 1.0f, box.w, box.h), fg, ts);
}

// 折叠面板：只有标题行可点，内容由调用方在 *open 为真时自己画。
// 返回 true 表示本帧切换了展开状态。
inline bool Ui::accordion(const std::string& title, bool* open, const Rect& box, const Style& st) {
    if (!open || box.w <= 0.0f) return false;
    const Theme& th = impl->theme;
    uint64_t id = detail::make_id("accordion", box, detail::hash_string(title.c_str()));
    const float fs = st.font_size > 0.0f ? st.font_size : th.font_size;
    const float head_h = std::min(box.h, std::max(fs + 22.0f, 38.0f));
    const Rect head(box.x, box.y, box.w, head_h);
    const float rad = st.radius >= 0.0f ? st.radius : th.radius_sm;

    detail::Interact in = detail::interact(impl, id, head, !st.disabled);
    if (in.hovered) set_cursor(Cursor::Hand);
    if (in.pressed) detail::grab_focus(impl, id);
    bool changed = false;
    if (in.clicked) { *open = !*open; changed = true; }

    const float t_h = detail::animate_value(impl, id * 71u + 1u, in.hovered ? 1.0f : 0.0f, 18.0f);
    if (t_h > 0.02f) fill_rect(head, th.foreground.with_alpha(uint8_t(10.0f * t_h)), rad);

    Style ts;
    ts.font_size = fs;
    ts.font_weight = 500;
    ts.align = Align::Left;
    ts.valign = VAlign::Middle;
    Color c = !st.fg.is_none() ? st.fg : th.foreground;
    if (st.disabled) c.a = uint8_t(float(c.a) * 0.45f);
    draw_text(title, Rect(head.x + 12.0f, head.y, std::max(head.w - 44.0f, 1.0f), head.h), c, ts);

    // 箭头只做上下两态（不做旋转矩阵，省代码也够表达）
    const float s = fs * 0.28f;
    const float mx = head.right() - 20.0f, my = head.cy();
    const Color ac = th.muted_foreground;
    if (*open) {
        draw_line(mx - s, my + s * 0.5f, mx, my - s * 0.55f, ac, 1.6f);
        draw_line(mx, my - s * 0.55f, mx + s, my + s * 0.5f, ac, 1.6f);
    } else {
        draw_line(mx - s, my - s * 0.5f, mx, my + s * 0.55f, ac, 1.6f);
        draw_line(mx, my + s * 0.55f, mx + s, my - s * 0.5f, ac, 1.6f);
    }

    if (detail::is_focused(impl, id)) detail::paint_focus_ring(*this, head, rad, th.ring);
    return changed;
}

// 表格：表头 + 等宽列 + 斑马纹。列宽一律等分（要非等宽请传 Style::pad_x 自行缩进）。
inline void Ui::table(const std::vector<std::string>& headers,
                      const std::vector<std::vector<std::string>>& rows,
                      const Rect& box, const Style& st) {
    const Theme& th = impl->theme;
    size_t cols = headers.size();
    if (cols == 0) for (const auto& r : rows) cols = std::max(cols, r.size());
    if (cols == 0 || box.w <= 4.0f || box.h <= 4.0f) return;

    const float rad = st.radius >= 0.0f ? st.radius : th.radius;
    const float fs  = st.font_size > 0.0f ? st.font_size : th.font_size;
    const float head_h = std::max(fs + 18.0f, 36.0f);
    const float row_h  = std::max(fs + 16.0f, 34.0f);
    const float col_w  = box.w / float(cols);
    const float cell_pad = st.pad_x >= 0.0f ? st.pad_x : 12.0f;

    push_clip(box);

    const Rect hr(box.x, box.y, box.w, head_h);
    fill_rect(hr, !st.bg.is_none() ? st.bg : th.muted, rad);
    for (size_t c = 0; c < cols; ++c) {
        const Rect cr(hr.x + col_w * float(c), hr.y, col_w, hr.h);
        if (c > 0) fill_rect(Rect(cr.x, cr.y + 7.0f, 1.0f, std::max(cr.h - 14.0f, 1.0f)), th.border, 0.0f);
        if (c >= headers.size()) continue;
        Style ts;
        ts.font_size = std::max(fs - 1.0f, 8.0f);
        ts.font_weight = 600;
        ts.align = Align::Left;
        ts.valign = VAlign::Middle;
        Color cc = th.muted_foreground;
        if (st.disabled) cc.a = uint8_t(float(cc.a) * 0.45f);
        draw_text(headers[c], cr.inset(cell_pad, 0.0f), cc, ts);
    }
    fill_rect(Rect(box.x, hr.bottom() - 1.0f, box.w, 1.0f), th.border, 0.0f);

    for (size_t r = 0; r < rows.size(); ++r) {
        const Rect rr(box.x, hr.bottom() + row_h * float(r), box.w, row_h);
        if (rr.y >= box.bottom()) break;
        if (r % 2 == 1) fill_rect(rr, th.muted.scaled_alpha(0.5f), 0.0f);
        for (size_t c = 0; c < cols && c < rows[r].size(); ++c) {
            Style ts;
            ts.font_size = fs;
            ts.font_weight = 400;
            ts.align = Align::Left;
            ts.valign = VAlign::Middle;
            Color cc = !st.fg.is_none() ? st.fg : th.foreground;
            if (st.disabled) cc.a = uint8_t(float(cc.a) * 0.45f);
            draw_text(rows[r][c], Rect(rr.x + col_w * float(c), rr.y, col_w, rr.h).inset(cell_pad, 0.0f), cc, ts);
        }
        if (r + 1 < rows.size())
            fill_rect(Rect(rr.x, rr.bottom() - 1.0f, rr.w, 1.0f), th.border, 0.0f);
    }

    pop_clip();
    stroke_rect(box, th.border, rad, 1.0f);
}

// 迷你折线图。默认在折线下方垫一层低透明度填充（经典 sparkline 观感），
// 只想要裸折线就传 Style::ghost()。
inline void Ui::sparkline(const std::vector<float>& values, const Rect& box, const Style& st) {
    if (values.size() < 2 || box.w < 4.0f || box.h < 4.0f) return;
    float lo = values[0], hi = values[0];
    for (float v : values) { lo = std::min(lo, v); hi = std::max(hi, v); }
    if (hi - lo < 1e-6f) { hi = lo + 1.0f; lo -= 1.0f; }

    const Theme& th = impl->theme;
    const float pad = 2.0f;
    const float iw  = std::max(box.w - pad * 2.0f, 1.0f);
    const float ih  = std::max(box.h - pad * 2.0f, 1.0f);
    const float step = iw / float(values.size() - 1);
    const Color line_c = !st.bg.is_none() ? st.bg : th.primary;
    const float lw = st.border_width > 0.0f ? st.border_width : 1.8f;

    std::vector<Vec2> pts;
    pts.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        const float x = box.x + pad + step * float(i);
        const float y = box.y + pad + ih * (1.0f - (values[i] - lo) / (hi - lo));
        pts.push_back(Vec2(impl->px(x), impl->px(y)));
    }

    if (impl->canvas && st.variant != Style::Ghost) {
        std::vector<Vec2> area = pts;
        area.push_back(Vec2(impl->px(box.right() - pad), impl->px(box.bottom() - pad)));
        area.push_back(Vec2(impl->px(box.x + pad), impl->px(box.bottom() - pad)));
        Color fc = line_c;
        fc.a = 30;
        if (st.disabled) fc.a = 14;
        detail::raster_poly(*impl->canvas, area, fc);
    }
    for (size_t i = 1; i < pts.size(); ++i)
        draw_line(pts[i - 1].x / impl->scale, pts[i - 1].y / impl->scale,
                  pts[i].x / impl->scale, pts[i].y / impl->scale, line_c, lw);
}

// 柱状图（迷你）。负值用 muted 画，一眼能区分出"掉下去的那几根"。
inline void Ui::bars(const std::vector<float>& values, const Rect& box, const Style& st) {
    if (values.empty() || box.w < 4.0f || box.h < 4.0f) return;
    const Theme& th = impl->theme;
    float hi = 0.0f;
    for (float v : values) hi = std::max(hi, std::fabs(v));
    if (hi < 1e-6f) return;

    const float gap = values.size() > 1 ? std::max(2.0f, box.w * 0.02f) : 0.0f;
    const float bw  = std::max(1.0f, (box.w - gap * float(values.size() - 1)) / float(values.size()));
    const float rad = st.radius >= 0.0f ? st.radius : std::min(3.0f, bw * 0.5f);
    const Color pos_c = !st.bg.is_none() ? st.bg : th.primary;

    for (size_t i = 0; i < values.size(); ++i) {
        const float t = detail::clampf(std::fabs(values[i]) / hi, 0.0f, 1.0f);
        const float h = std::max(1.0f, box.h * t);
        Color cc = values[i] < 0.0f ? th.muted : pos_c;
        if (st.disabled) cc.a = uint8_t(float(cc.a) * 0.45f);
        fill_rect(Rect(box.x + (bw + gap) * float(i), box.bottom() - h, bw, h), cc, rad);
    }
}

// =============================================================================
//  8.15  浮层类
// =============================================================================

// 悬停提示：只在鼠标进入 anchor 时登记一次帧末绘制。
// 注意这里刻意不调 repaint() —— 悬停期间鼠标每动一下窗口本来就会重画，
// 常驻要帧会把空闲 CPU 从 0% 抬起来。
inline void Ui::tooltip(const Rect& anchor, const std::string& text, const Style& st) {
    if (text.empty() || st.disabled) return;
    const Input& kin = impl->input;
    if (!anchor.contains(kin.mouse.x, kin.mouse.y)) return;

    const Theme& th = impl->theme;
    const float fs = st.font_size > 0.0f ? st.font_size : th.font_size - 1.0f;
    const float padx = 8.0f, pady = 5.0f;
    const float w = text_width(text, fs, 500) + padx * 2.0f;
    const float h = std::max(line_height(fs), fs * 1.2f) + pady * 2.0f;
    float x = anchor.cx() - w * 0.5f;
    float y = anchor.y - h - 7.0f;
    if (y < 4.0f) y = anchor.bottom() + 7.0f;          // 上方放不下就翻到下方
    x = detail::clampf(x, 4.0f, std::max(4.0f, width() - w - 4.0f));

    const Style st_copy = st;
    deferred([this, x, y, w, h, text, fs, th, st_copy]() mutable {
        const Rect box(x, y, w, h);
        drop_shadow(box, 6.0f, 10.0f, Color(0, 0, 0, 34), 3.0f);
        fill_rect(box, !st_copy.bg.is_none() ? st_copy.bg : th.popover, 6.0f);
        stroke_rect(box, th.border, 6.0f, 1.0f);
        Style ts;
        ts.font_size = fs;
        ts.font_weight = 500;
        ts.align = Align::Center;
        ts.valign = VAlign::Middle;
        draw_text(text, box, !st_copy.fg.is_none() ? st_copy.fg : th.popover_foreground, ts);
    });
}

// 下拉菜单。
// 关键点：**交互在帧内做，绘制推迟到帧末**。这样既保证了弹层盖住其它组件，
// 又能让返回值在点击当帧就生效（不用等到下一帧）。
inline bool Ui::menu(const std::string& label, const std::vector<std::string>& items,
                     int* clicked_index, const Rect& box, const Style& st) {
    const Theme& th = impl->theme;
    uint64_t id = detail::make_id("menu", box);
    const float rad = st.radius >= 0.0f ? st.radius : th.radius_sm;
    const float fs  = st.font_size > 0.0f ? st.font_size : th.font_size;
    const bool enabled = !st.disabled;

    bool open = impl->flags[id] > 0.5f;
    detail::Interact in = detail::interact(impl, id, box, enabled);
    if (in.hovered) set_cursor(Cursor::Hand);
    const float t_hover = detail::animate_value(impl, id * 73u + 1u, (in.hovered || open) ? 1.0f : 0.0f, 18.0f);
    const float t_press = detail::animate_value(impl, id * 73u + 2u, (in.held && in.hovered) ? 1.0f : 0.0f, 34.0f);

    detail::Skin sk = detail::make_skin(th, st, Style::Outline, rad);
    if (st.font_weight <= 0) sk.weight = 500;
    detail::paint_surface(*this, box, sk, t_hover, t_press, st.disabled);

    const float pad = st.pad_x >= 0.0f ? st.pad_x : 12.0f;
    Style ts;
    ts.font_size = fs;
    ts.font_weight = 500;
    ts.align = Align::Left;
    ts.valign = VAlign::Middle;
    Color fg = sk.fg;
    if (st.disabled) fg.a = uint8_t(float(fg.a) * 0.45f);
    draw_text(label, Rect(box.x + pad, box.y, std::max(box.w - pad * 2.0f - 18.0f, 1.0f), box.h), fg, ts);
    {
        const float ccx = box.right() - pad - 5.0f, ccy = box.cy() + 0.5f, s = 3.6f;
        draw_line(ccx - s, ccy - s * 0.45f, ccx, ccy + s * 0.55f, fg, 1.6f);
        draw_line(ccx, ccy + s * 0.55f, ccx + s, ccy - s * 0.45f, fg, 1.6f);
    }
    if (detail::is_focused(impl, id)) detail::paint_focus_ring(*this, box, rad, th.ring);
    if (in.pressed) detail::grab_focus(impl, id);
    if (in.clicked) {
        open = !open;
        impl->flags[id] = open ? 1.0f : 0.0f;
        repaint();
    }

    int chosen = -1;
    bool closing = false;
    if (open && !items.empty()) {
        const float item_h = std::max(box.h, 32.0f);
        float list_w = box.w;
        for (const auto& it : items) list_w = std::max(list_w, text_width(it, fs, 500) + 34.0f);
        const float list_h = item_h * float(items.size()) + 8.0f;
        float ly = box.bottom() + 4.0f;
        if (ly + list_h > height() - 4.0f) ly = std::max(4.0f, box.y - 4.0f - list_h);
        const Rect list(box.x, ly, list_w, list_h);

        std::vector<Rect>    rects;
        std::vector<uint64_t> ids;
        std::vector<uint8_t> hov;
        rects.reserve(items.size());
        ids.reserve(items.size());
        hov.reserve(items.size());
        for (size_t i = 0; i < items.size(); ++i) {
            const Rect r(list.x + 4.0f, list.y + 4.0f + item_h * float(i), list.w - 8.0f, item_h);
            rects.push_back(r);
            ids.push_back(detail::make_id("menu_item", r, id + i));
            hov.push_back(0);
        }
        for (size_t i = 0; i < items.size(); ++i) {
            detail::Interact ii = detail::interact(impl, ids[i], rects[i], true);
            if (ii.hovered) { set_cursor(Cursor::Hand); hov[i] = 1; }
            if (ii.clicked) chosen = int(i);
        }
        const bool inside_list = list.contains(impl->input.mouse.x, impl->input.mouse.y);
        closing = (impl->input.pressed[0] || impl->input.pressed[1]) && !inside_list
               && !box.contains(impl->input.mouse.x, impl->input.mouse.y);

        std::vector<std::string> items_copy = items;
        deferred([this, list, rad, item_h, rects, ids, hov, items_copy, th, fs]() mutable {
            drop_shadow(list, rad, 10.0f, Color(0, 0, 0, 34), 3.0f);
            fill_rect(list, th.popover, rad);
            stroke_rect(list, th.border, rad, 1.0f);
            push_clip(list);
            for (size_t i = 0; i < rects.size(); ++i) {
                const float t_h = detail::animate_value(impl, ids[i] * 83u + 1u, hov[i] ? 1.0f : 0.0f, 20.0f);
                if (t_h > 0.02f)
                    fill_rect(rects[i], th.accent.mix(th.foreground.with_alpha(10), t_h * 0.3f),
                              std::max(0.0f, rad - 2.0f));
                Style its;
                its.font_size = fs;
                its.font_weight = 500;
                its.align = Align::Left;
                its.valign = VAlign::Middle;
                draw_text(items_copy[i], rects[i].inset(10.0f, 0.0f), th.popover_foreground, its);
            }
            pop_clip();
            (void)item_h;
        });
    }

    if (chosen >= 0) {
        if (clicked_index) *clicked_index = chosen;
        impl->flags[id] = 0.0f;
        repaint();
        return true;
    }
    if (closing) {
        impl->flags[id] = 0.0f;
        repaint();
    }
    return false;
}

// 模态框。约定：在帧回调的**末尾**调用；返回 true 表示本帧处于打开态，
// 想画自己的按钮就紧接着 dialog() 之后画，这样天然落在遮罩之上。
inline bool Ui::dialog(const std::string& title, const std::string& body, bool* open,
                       const Rect& box, const Style& st) {
    if (!open || !*open) return false;
    const Theme& th = impl->theme;
    uint64_t cid = detail::make_id("dialog_close", box);
    const float rad = st.radius >= 0.0f ? st.radius : th.radius_lg;
    const float fs  = st.font_size > 0.0f ? st.font_size : th.font_size;
    const float pad = st.pad_x >= 0.0f ? st.pad_x : 24.0f;
    const Input& kin = impl->input;

    // 交互：点外部 / Esc / 右上角 × 都算关闭
    bool dismiss = false;
    if (!box.contains(kin.mouse.x, kin.mouse.y) && (kin.pressed[0] || kin.pressed[1])) dismiss = true;
    if (kin.key_pressed[KEY_ESC]) dismiss = true;

    const Rect close_r(box.right() - pad - 6.0f, box.y + pad - 8.0f, 22.0f, 22.0f);
    detail::Interact cin = detail::interact(impl, cid, close_r, true);
    if (cin.hovered) set_cursor(Cursor::Hand);
    if (cin.clicked) dismiss = true;
    const float ct = detail::animate_value(impl, cid * 79u + 1u, cin.hovered ? 1.0f : 0.0f, 18.0f);

    // 绘制：遮罩 → 面板
    fill_rect(Rect(0, 0, width(), height()), Color(0, 0, 0, 110), 0.0f);
    drop_shadow(box, rad, 22.0f, Color(0, 0, 0, 46), 8.0f);
    fill_rect(box, !st.bg.is_none() ? st.bg : th.background, rad);
    stroke_rect(box, th.border, rad, 1.0f);

    // 文字必须裁在面板里 —— 正文一长就会横着爬出去（溢出到遮罩上的字最难发现，
    // 因为遮罩本身是半透明黑，看起来只是"有点脏"）。
    push_clip(box);
    const float title_lh = line_height(fs + 3.0f);
    Style hs;
    hs.font_size = fs + 3.0f;
    hs.font_weight = 600;
    hs.align = Align::Left;
    hs.valign = VAlign::Top;
    hs.wrap = true;
    draw_text(title, Rect(box.x + pad, box.y + pad, std::max(box.w - pad * 2.0f - 26.0f, 1.0f), title_lh),
              !st.fg.is_none() ? st.fg : th.foreground, hs);
    if (!body.empty()) {
        Style bs;
        bs.font_size = fs;
        bs.font_weight = 400;
        bs.align = Align::Left;
        bs.valign = VAlign::Top;
        bs.wrap = true;
        bs.line_gap = 3.0f;                 // 正文比标题松一点，长段落才好读
        draw_text(body, Rect(box.x + pad, box.y + pad + title_lh + 10.0f, box.w - pad * 2.0f,
                             std::max(box.h - pad * 2.0f - title_lh - 10.0f, 1.0f)),
                  th.muted_foreground, bs);
    }
    pop_clip();

    if (ct > 0.02f) fill_rect(close_r, th.foreground.with_alpha(uint8_t(12.0f * ct)), 6.0f);
    {
        const float s = 4.6f;
        const Color cc = th.muted_foreground;
        draw_line(close_r.cx() - s, close_r.cy() - s, close_r.cx() + s, close_r.cy() + s, cc, 1.6f);
        draw_line(close_r.cx() + s, close_r.cy() - s, close_r.cx() - s, close_r.cy() + s, cc, 1.6f);
    }

    if (dismiss) { *open = false; repaint(); }
    return true;      // 本帧仍然按"打开"画完了，下一帧才真的消失（不会闪一下）
}

// 轻提示条
inline void Ui::toast(const std::string& text, const Rect& box, const Style& st) {
    const Theme& th = impl->theme;
    const float rad = st.radius >= 0.0f ? st.radius : th.radius;
    const float fs  = st.font_size > 0.0f ? st.font_size : th.font_size;
    const bool danger = (st.variant == Style::Destructive);

    Color bg = !st.bg.is_none() ? st.bg : th.popover;
    Color fg = !st.fg.is_none() ? st.fg : th.popover_foreground;
    Color bd = !st.border_color.is_none() ? st.border_color : th.border;
    if (st.disabled) { bg.a = uint8_t(float(bg.a) * 0.6f); fg.a = uint8_t(float(fg.a) * 0.6f); }

    drop_shadow(box, rad, 14.0f, Color(0, 0, 0, 38), 5.0f);
    fill_rect(box, bg, rad);
    stroke_rect(box, bd, rad, 1.0f);
    float text_x = box.x + 14.0f;
    if (danger) {
        fill_rect(Rect(box.x + 8.0f, box.y + 8.0f, 3.0f, std::max(box.h - 16.0f, 1.0f)), th.destructive, 1.5f);
        text_x = box.x + 18.0f;
    }
    Style ts;
    ts.font_size = fs;
    ts.font_weight = 500;
    ts.align = Align::Left;
    ts.valign = VAlign::Middle;
    push_clip(box);
    draw_text(text, Rect(text_x, box.y, std::max(box.right() - 14.0f - text_x, 1.0f), box.h), fg, ts);
    pop_clip();
}

// =============================================================================
//  8.16  滚动
//  实现方式是把"视口"翻译成布局语义：压一个负向偏移的 group + 一个裁剪区，
//  于是内部的 row()/column() 自动吃到那个偏移，不用逐个组件改坐标。
//
//  通用滚动与虚拟滚动走的是**同一个函数**：虚拟模式只是把内容高度算成
//  行数 × 行高，然后按可见区间挑着调用回调。所以偏移只存一份（按 box 记忆），
//  滚动条、裁剪、滚轮都只有一套 —— 不会出现"两个滚动区各自记位置"的怪事。
// =============================================================================
inline ScrollScope Ui::scroll(const Rect& box, float content_h) {
    ScrollScope sc;
    sc.ui         = this;
    sc.id         = detail::make_id("scroll", box);
    sc.box        = box;
    sc.content_h  = content_h;
    sc.max_offset = std::max(0.0f, content_h - box.h);

    float& off = impl->flags[sc.id];
    const Input& kin = impl->input;
    if (sc.max_offset > 0.0f && box.contains(kin.mouse.x, kin.mouse.y) && std::fabs(kin.wheel) > 0.001f) {
        off = detail::clampf(off - kin.wheel * 44.0f, 0.0f, sc.max_offset);
        repaint();
    }
    off = detail::clampf(off, 0.0f, sc.max_offset);
    sc.offset = off;

    Ui::Impl::ScrollCtx ctx;
    ctx.id = sc.id;
    ctx.box = box;
    ctx.offset = off;
    ctx.max_offset = sc.max_offset;

    impl->scroll_stack.push_back(ctx);
    impl->clips.push_back(impl->phys(box));
    impl->recompute_clip();
    impl->group.push_back(Rect(box.x, box.y - off, box.w, std::max(content_h, box.h)));
    sc.active = true;
    return sc;
}

// 收尾：画滚动条（放在收尾而不是开头，才能盖在内容之上）、弹裁剪与偏移。
// 幂等 —— RAII 析构和手工 end_scroll() 同时存在也不会弹两次。
inline void ScrollScope::close() {
    if (!active || !ui) return;
    active = false;
    if (ui->impl && !ui->impl->scroll_stack.empty() &&
        ui->impl->scroll_stack.back().id == id)
        ui->end_scroll();
}

inline ScrollScope::~ScrollScope() { close(); }

inline void ScrollScope::content(float h) {
    content_h  = h;
    max_offset = std::max(0.0f, h - box.h);
    if (!ui || !ui->impl) return;
    float& off = ui->impl->flags[id];
    off = detail::clampf(off, 0.0f, max_offset);
    offset = off;
    if (!ui->impl->scroll_stack.empty() && ui->impl->scroll_stack.back().id == id) {
        ui->impl->scroll_stack.back().max_offset = max_offset;
        ui->impl->scroll_stack.back().offset = off;
        if (!ui->impl->group.empty())
            ui->impl->group.back() = Rect(box.x, box.y - off, box.w, std::max(h, box.h));
    }
}

inline void ScrollScope::to(float v) {
    if (!ui || !ui->impl) return;
    float& off = ui->impl->flags[id];
    const float nv = detail::clampf(v, 0.0f, max_offset);
    if (nv != off) { off = nv; ui->repaint(); }
    offset = nv;
    if (!ui->impl->scroll_stack.empty() && ui->impl->scroll_stack.back().id == id) {
        ui->impl->scroll_stack.back().offset = nv;
        if (!ui->impl->group.empty())
            ui->impl->group.back() = Rect(box.x, box.y - nv, box.w, std::max(content_h, box.h));
    }
}


// 老写法：开区但不交还句柄 —— 收尾交给 end_scroll()，和 0.1.0 的语义一致。
// （句柄的 active 置 false 是刻意的：否则这个临时对象一析构就把区关了。）
inline void Ui::begin_scroll(const Rect& box, float content_h) {
    ScrollScope sc = scroll(box, content_h);
    sc.active = false;
}

inline void Ui::end_scroll() {
    if (impl->scroll_stack.empty()) return;
    const Ui::Impl::ScrollCtx ctx = impl->scroll_stack.back();

    // 滚动条画在内容之上（所以放在 end_scroll 里，而不是 begin_scroll）
    if (ctx.max_offset > 0.0f) {
        const float thumb_h = std::max(24.0f, ctx.box.h * (ctx.box.h / (ctx.box.h + ctx.max_offset)));
        const float ty = ctx.box.y + (ctx.box.h - thumb_h) * (ctx.offset / ctx.max_offset);
        fill_rect(Rect(ctx.box.right() - 5.0f, ty, 3.0f, thumb_h),
                  impl->theme.muted_foreground.scaled_alpha(0.45f), 1.5f);
    }

    impl->last_scroll = ctx.offset;
    impl->scroll_stack.pop_back();
    if (!impl->clips.empty()) impl->clips.pop_back();
    if (impl->group.size() > 1) impl->group.pop_back();
    impl->recompute_clip();
}

// 滚动区开启期间返回内层的偏移；已经 end_scroll() 之后返回最近一次的值，
// 这样"滚完再读一下滚到哪了"也是成立的（一开始只在栈里可取，是个坑）。
inline float Ui::scroll_y() const {
    return impl->scroll_stack.empty() ? impl->last_scroll : impl->scroll_stack.back().offset;
}

inline void Ui::scroll_to(float offset) {
    if (impl->scroll_stack.empty()) return;
    const Ui::Impl::ScrollCtx& ctx = impl->scroll_stack.back();
    const float nv = detail::clampf(offset, 0.0f, ctx.max_offset);
    if (nv != ctx.offset) { impl->flags[ctx.id] = nv; repaint(); }
}

// -----------------------------------------------------------------------------
//  虚拟滚动
//  只算可见区间，其余项**不构建、不绘制**。这是"一万行列表"和"一万行布局 +
//  一万次绘制"的区别：后者的成本随数据量线性增长，前者与数据量无关。
// -----------------------------------------------------------------------------
inline int Ui::scroll_visible(const Rect& box, int count, float item_h, int* first, int* last) {
    if (first) *first = 0;
    if (last)  *last  = -1;
    if (count < 0) count = 0;

    const uint64_t id = detail::make_id("scroll", box);
    const float content_h = float(count) * item_h;
    const float max_off   = std::max(0.0f, content_h - box.h);

    float& off = impl->flags[id];
    const Input& kin = impl->input;
    if (max_off > 0.0f && box.contains(kin.mouse.x, kin.mouse.y) && std::fabs(kin.wheel) > 0.001f) {
        off = detail::clampf(off - kin.wheel * 44.0f, 0.0f, max_off);
        repaint();
    }
    off = detail::clampf(off, 0.0f, max_off);
    if (count == 0 || item_h <= 0.01f) return off;

    int i0 = int(std::floor(off / item_h));
    int i1 = int(std::ceil((off + box.h) / item_h)) - 1;
    i0 = detail::clampi(i0, 0, count - 1);
    i1 = detail::clampi(i1, 0, count - 1);
    if (first) *first = i0;
    if (last)  *last  = i1;
    return off;
}

inline void Ui::scroll_bar(const Rect& box, float content_h) {
    const float max_off = std::max(0.0f, content_h - box.h);
    if (max_off <= 0.0f) return;
    const float off = scroll_offset(box);
    const float thumb_h = std::max(24.0f, box.h * (box.h / (box.h + max_off)));
    const float ty = box.y + (box.h - thumb_h) * (off / max_off);
    fill_rect(Rect(box.right() - 5.0f, ty, 3.0f, thumb_h),
              impl->theme.muted_foreground.scaled_alpha(0.45f), 1.5f);
}

inline float Ui::scroll_offset(const Rect& box) const {
    auto it = impl->flags.find(detail::make_id("scroll", box));
    return it == impl->flags.end() ? 0.0f : it->second;
}

inline void Ui::scroll_to_index(const Rect& box, int index, float item_h, int count) {
    const float max_off = std::max(0.0f, float(std::max(count, 0)) * item_h - box.h);
    const float target  = detail::clampf(float(std::max(index, 0)) * item_h, 0.0f, max_off);
    float& off = impl->flags[detail::make_id("scroll", box)];
    if (off != target) { off = target; repaint(); }
}

// =============================================================================
//  8.17  Window 内部状态
// =============================================================================
struct Window::Impl {
    Ui ui;
    Theme theme = Theme::light();
    bool running = true;
    bool need_frame = true;
    // 上一帧的"再要一帧"是组件自己提的（spinner / 光标闪烁 / hover 插值），
    // 而不是用户输入触发的。主循环靠它决定要不要按 60fps 节流：
    // 区分不出来的话，只要有一个连续动画在跑，need_frame 就恒为真 →
    // 主循环退化成满速重绘（实测空闲 CPU 98%）。
    bool self_repaint = false;
    // 连续动画的重绘预算（毫秒/帧）。默认 16ms ≈ 60fps；调大可显著降低
    // spinner / 光标闪烁这类常驻动画的 CPU（代价是动画帧率下降）。
    uint32_t anim_budget_ms = 16;
    // AINO_FRAME_LOG=1 时把每帧渲染耗时打到 stderr，用来定位"谁在吃 CPU"。
    bool frame_log = false;
    uint32_t frame_log_n = 0;
    uint64_t frame_log_sum = 0;     // µs
    uint32_t frame_log_worst = 0;   // µs
    uint64_t t_feed = 0, t_cb = 0, t_end = 0, t_present = 0;   // 分段累计 µs
    float scale = 1.0f;
    int client_w = 0, client_h = 0;     // 物理像素
    Canvas canvas;
    TextRenderer* text = nullptr;
    Cursor cursor = Cursor::Arrow;
    void* native = nullptr;
    std::function<void()> frame_cb_ptr;   // run() 期间指向用户回调，render() 用
    // run() 期间指向 Window::render()。窗口过程里要"同步补一帧"时用它 ——
    // 拖边框缩放时主循环被 user32 的模态循环卡住，根本轮不到 run() 出帧。
    std::function<void()> render_fn;

    ~Impl() {
        if (text) { text->release(); delete text; text = nullptr; }
    }
};

#ifdef AINO_PLATFORM_WINDOWS
namespace detail {
struct Win32State;   // 定义在下面（win_proc 之前）—— 构造函数只用它的指针，声明就够
inline void win_set_ime_pos(Win32State* s, float caret_x, float top, float bottom);
} // namespace detail
#endif

inline Window::Window(int w, int h, const std::string& title) : impl(new Impl()) {
    impl->theme = Theme::light();
    impl->ui.set_theme(impl->theme);
    // 让 Ui 的 repaint() 能叫醒主循环（同时标记"这是组件自请求的帧"）
    impl->ui.impl->request_repaint = [this]() {
        impl->need_frame = true;
        impl->self_repaint = true;
    };
#ifdef AINO_PLATFORM_WINDOWS
    // 输入框有焦点时把 IME 的组合窗 / 候选窗钉到光标处。
    // 不绑的话系统会把它们丢在客户区左上角 —— 中文输入时候选框飘得老远。
    impl->ui.impl->ime_pos = [this](float x, float top, float bottom) {
        detail::win_set_ime_pos(reinterpret_cast<detail::Win32State*>(impl->native), x, top, bottom);
    };
#endif
    // 诊断开关：AINO_FRAME_LOG=1 时每 120 帧打印一次渲染耗时。
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable: 4996)   // getenv 在 MSVC 下被标记为不安全，这里只读环境变量
#endif
    {   const char* fl = std::getenv("AINO_FRAME_LOG");
        if (fl && fl[0] && fl[0] != '0') impl->frame_log = true; }
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#ifdef AINO_PLATFORM_WINDOWS
    detail::w32_create(this, w, h, title);
#elif defined(AINO_PLATFORM_X11)
    detail::x11_create(this, w, h, title);
#elif defined(AINO_PLATFORM_MACOS)
    detail::cocoa_create(this, w, h, title);
#endif
}

inline Window::~Window() {
#ifdef AINO_PLATFORM_WINDOWS
    detail::w32_destroy(this);
#elif defined(AINO_PLATFORM_X11)
    detail::x11_destroy(this);
#elif defined(AINO_PLATFORM_MACOS)
    detail::cocoa_destroy(this);
#endif
    delete impl;
    impl = nullptr;
}

inline bool  Window::ok() const { return impl && impl->native != nullptr; }
inline Ui&   Window::ui() { return impl->ui; }
inline void  Window::set_theme(const Theme& t) {
    // 与"渲染实际在用的那份主题"比较（不是 Window 自己的副本，否则用户直接改
    // ui.set_theme() 之后这里会误判成"没变"）。
    // 少了这一步，在帧回调里每帧调用 set_theme 会让 need_frame 永远为真，
    // 主循环退化成忙等（实测 ~960fps、空闲吃满一个核）。
    if (impl->ui.impl->theme.same_as(t)) { impl->theme = t; return; }
    impl->theme = t;
    impl->ui.set_theme(t);
    impl->need_frame = true;
}
inline int   Window::width() const { return int(float(impl->client_w) / impl->scale + 0.5f); }
inline int   Window::height() const { return int(float(impl->client_h) / impl->scale + 0.5f); }
inline float Window::dpi_scale() const { return impl->scale; }
inline bool  Window::should_close() const { return !impl->running; }
inline void  Window::request_repaint() { impl->need_frame = true; }

inline void  Window::set_anim_fps(float fps) {
    if (fps < 1.0f) fps = 1.0f;
    if (fps > 240.0f) fps = 240.0f;
    impl->anim_budget_ms = uint32_t(1000.0f / fps + 0.5f);
    if (impl->anim_budget_ms < 1) impl->anim_budget_ms = 1;
}

#ifdef AINO_PLATFORM_WINDOWS
// =============================================================================
//  8.13  Windows 后端
// =============================================================================
namespace detail {

// 显式用宽字符版资源 ID —— IDC_* 会随 UNICODE 宏在 A/W 之间切换，
// 而我们要的是一个不依赖调用方编译开关的库
#ifndef AINO_IDC_ARROW
#  define AINO_IDC_ARROW   MAKEINTRESOURCEW(32512)
#  define AINO_IDC_IBEAM   MAKEINTRESOURCEW(32513)
#  define AINO_IDC_HAND    MAKEINTRESOURCEW(32649)
#  define AINO_IDC_SIZEWE  MAKEINTRESOURCEW(32644)
#  define AINO_IDC_SIZENS  MAKEINTRESOURCEW(32645)
#endif

struct Win32State {
    Window::Impl* owner = nullptr;
    HWND hwnd = nullptr;
    HDC mem_dc = nullptr;
    HBITMAP hbmp = nullptr;
    void* bits = nullptr;
    int pw = 0, ph = 0;
    bool tracking = false;
    bool has_backbuffer = false;
    // 后备缓冲里装的是**当前尺寸下渲染过的**画面吗？
    // 重建缓冲（resize）后立刻变 false，渲染一帧后变 true。
    // 铁律：bb_valid 为 false 时**绝不允许 BitBlt 上屏** —— 那块新建的 DIB 全是 0，
    // 贴上去就是一片黑（这就是拖边框时的黑闪）。
    bool bb_valid = false;

    // 待消费的输入
    float mx = 0.0f, my = 0.0f;
    bool  inside = false;
    bool  btn_down[3]    = {false, false, false};
    bool  btn_pressed[3] = {false, false, false};
    bool  btn_released[3]= {false, false, false};
    float wheel = 0.0f;
    int   clicks = 0;
    std::string text_in;
    bool  key_down[KEY_COUNT]    = {};
    bool  key_pressed[KEY_COUNT] = {};
    bool  key_ctrl[KEY_COUNT]    = {};
    bool  key_shift[KEY_COUNT]   = {};
    bool  key_alt[KEY_COUNT]     = {};
    uint32_t high_surrogate = 0;
};

inline UINT win_dpi_of(HWND hwnd) {
    typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
    static PFN_GetDpiForWindow fn = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        HMODULE u = GetModuleHandleW(L"user32.dll");
        if (u) fn = reinterpret_cast<PFN_GetDpiForWindow>(GetProcAddress(u, "GetDpiForWindow"));
    }
    if (fn && hwnd) { UINT d = fn(hwnd); if (d) return d; }
    HDC dc = GetDC(nullptr);
    UINT d = UINT(GetDeviceCaps(dc, LOGPIXELSX));
    ReleaseDC(nullptr, dc);
    return d ? d : 96;
}

inline void win_enable_dpi_awareness() {
    typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(void*);
    static bool done = false;
    if (done) return;
    done = true;
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (u) {
        auto fn = reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(
            GetProcAddress(u, "SetProcessDpiAwarenessContext"));
        if (fn && fn(reinterpret_cast<void*>(static_cast<intptr_t>(-4)))) return;   // PER_MONITOR_AWARE_V2
    }
    SetProcessDPIAware();
}

inline int win_map_vk(WPARAM vk) {
    if (vk >= 'A' && vk <= 'Z') return int(vk);
    if (vk >= '0' && vk <= '9') return int(vk);
    switch (vk) {
        case VK_BACK:   return KEY_BACKSPACE;
        case VK_TAB:    return KEY_TAB;
        case VK_RETURN: return KEY_ENTER;
        case VK_ESCAPE: return KEY_ESC;
        case VK_SPACE:  return KEY_SPACE;
        case VK_DELETE: return KEY_DELETE;
        case VK_LEFT:   return KEY_LEFT;
        case VK_RIGHT:  return KEY_RIGHT;
        case VK_UP:     return KEY_UP;
        case VK_DOWN:   return KEY_DOWN;
        case VK_HOME:   return KEY_HOME;
        case VK_END:    return KEY_END;
        case VK_PRIOR:  return KEY_PAGEUP;
        case VK_NEXT:   return KEY_PAGEDOWN;
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return KEY_SHIFT;
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return KEY_CTRL;
        case VK_MENU: case VK_LMENU: case VK_RMENU: return KEY_ALT;
        case VK_LWIN: case VK_RWIN: return KEY_SUPER;
        case VK_F1: return KEY_F1; case VK_F2: return KEY_F2; case VK_F3: return KEY_F3;
        case VK_F4: return KEY_F4; case VK_F5: return KEY_F5; case VK_F6: return KEY_F6;
        case VK_F7: return KEY_F7; case VK_F8: return KEY_F8; case VK_F9: return KEY_F9;
        case VK_F10: return KEY_F10; case VK_F11: return KEY_F11; case VK_F12: return KEY_F12;
        default: return 0;
    }
}

inline void win_append_utf16(std::string& out, uint32_t unit) {
    // 处理代理对，否则 emoji / 扩展汉字会被切成两个非法码点
    static uint32_t pending_high = 0;
    (void)pending_high;
    if (unit >= 0xD800 && unit <= 0xDBFF) { pending_high = unit; return; }
    uint32_t cp = unit;
    if (unit >= 0xDC00 && unit <= 0xDFFF && pending_high) {
        cp = 0x10000 + ((pending_high - 0xD800) << 10) + (unit - 0xDC00);
        pending_high = 0;
    }
    char buf[8];
    int n = utf8_encode(cp, buf);
    out.append(buf, size_t(n));
}

inline void win_create_backbuffer(Win32State* s, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (s->pw == w && s->ph == h && s->has_backbuffer) return;

    HDC screen = GetDC(nullptr);
    if (!s->mem_dc) s->mem_dc = CreateCompatibleDC(screen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;          // top-down，跟我们的行序一致
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hb = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!hb || !bits) { s->has_backbuffer = false; return; }

    // 换上新的，旧的被换出后才允许删除
    HBITMAP old = reinterpret_cast<HBITMAP>(SelectObject(s->mem_dc, hb));
    if (old && old == s->hbmp) DeleteObject(old);
    s->hbmp = hb;
    SetBkMode(s->mem_dc, TRANSPARENT);

    s->bits = bits;
    s->pw = w; s->ph = h;
    s->has_backbuffer = true;
    s->bb_valid = false;      // 新缓冲是零填充的，没渲染过之前不许上屏

    Window::Impl* o = s->owner;
    o->canvas.px     = reinterpret_cast<uint8_t*>(bits);
    o->canvas.w      = w;
    o->canvas.h      = h;
    o->canvas.stride = w * 4;
    o->canvas.platform_dc = s->mem_dc;
    o->canvas.clear_clip();
    o->client_w = w;
    o->client_h = h;

    // run() 之前（窗口刚建出来、还没绑定 render_fn）没有任何人会马上渲染它，
    // 这时先把缓冲铺成主题底色，免得首帧出现前露一块黑。
    if (!o->render_fn) {
        const Color& bg = o->theme.background;
        uint8_t* p = reinterpret_cast<uint8_t*>(bits);
        const size_t n = size_t(w) * size_t(h);
        for (size_t i = 0; i < n; ++i, p += 4) {
            p[detail::CH_R] = bg.r; p[detail::CH_G] = bg.g;
            p[detail::CH_B] = bg.b; p[detail::CH_A] = 255;
        }
        s->bb_valid = true;   // 至少是"对的颜色"，可以贴
    }
}

inline void win_present(Win32State* s) {
    if (!s->has_backbuffer || !s->hwnd) return;
    HDC dc = GetDC(s->hwnd);
    BitBlt(dc, 0, 0, s->pw, s->ph, s->mem_dc, 0, 0, SRCCOPY);
    ReleaseDC(s->hwnd, dc);
}

// 在消息处理路径上"同步补一帧"。
//
// 为什么需要它：拖边框/拖角缩放时，鼠标按下走的是 DefWindowProc 的非客户区逻辑，
// user32 会在这里进入一个**模态循环**（WM_ENTERSIZEMOVE … WM_EXITSIZEMOVE），
// 我们自己的 run() 主循环这期间一行都跑不到 —— 于是没有任何代码去渲染。
// 而 WM_SIZE 刚把后备缓冲换成一块全零的新 DIB，紧接着的 WM_PAINT 就把它贴上去，
// 屏幕上就是一片黑，一直黑到松手为止（用户报的"缩放窗口时候会变黑"就是这个）。
// 所以这里直接在消息里画：拖一次画一次，跟手且不黑。
inline void win_render_sync(Win32State* s) {
    if (!s || !s->has_backbuffer) return;
    if (!s->owner || !s->owner->render_fn) return;   // run() 之前没绑，跳过
    s->owner->render_fn();                           // 内部会 win_present
}

// -----------------------------------------------------------------------------
//  输入法（IME）定位
// -----------------------------------------------------------------------------
// 窗口如果什么都不说，系统就把拼音串和候选框丢在客户区左上角（或者跟着鼠标）
// —— 于是中文输入的候选窗和那个正在编辑的输入框完全对不上。必须显式告诉 IME
// 该画在哪：ImmSetCompositionWindow 定组合串的原点，ImmSetCandidateWindow 定
// 候选框的左上角。
//
// 这里**不静态链接 imm32**，而是 LoadLibrary + GetProcAddress 取那四个入口，
// 为的是守住"只依赖 user32 + gdi32"这条（README 里写死的卖点）。imm32.dll 在
// 每个 Windows 上都有，取不到就当没有输入法支持，不影响其它功能。
//
// 结构体自己定义而不用 <imm.h>：COMPOSITIONFORM / CANDIDATEFORM 的布局是 ABI
// 固定的，抄一份比多引一个头文件更可控。
struct AinoCompositionFormW {
    DWORD dwStyle;
    POINT ptCurrentPos;
    RECT  rcArea;
};
struct AinoCandidateFormW {
    DWORD dwIndex;
    DWORD dwStyle;
    POINT ptCurrentPos;
    RECT  rcArea;
};
constexpr DWORD AINO_CFS_POINT        = 0x0002;   // 组合窗左上角 = ptCurrentPos
constexpr DWORD AINO_CFS_CANDIDATEPOS = 0x0040;   // 候选窗左上角 = ptCurrentPos

struct ImmApi {
    HIMC (WINAPI* get_ctx)   (HWND) = nullptr;
    BOOL (WINAPI* rel_ctx)   (HWND, HIMC) = nullptr;
    BOOL (WINAPI* set_comp)  (HIMC, const AinoCompositionFormW*) = nullptr;
    BOOL (WINAPI* set_cand)  (HIMC, const AinoCandidateFormW*) = nullptr;
    bool ok = false;
    ImmApi() {
        HMODULE h = LoadLibraryW(L"imm32.dll");
        if (!h) return;
        get_ctx  = reinterpret_cast<HIMC (WINAPI*)(HWND)>(GetProcAddress(h, "ImmGetContext"));
        rel_ctx  = reinterpret_cast<BOOL (WINAPI*)(HWND, HIMC)>(GetProcAddress(h, "ImmReleaseContext"));
        set_comp = reinterpret_cast<BOOL (WINAPI*)(HIMC, const AinoCompositionFormW*)>(
                       GetProcAddress(h, "ImmSetCompositionWindow"));
        set_cand = reinterpret_cast<BOOL (WINAPI*)(HIMC, const AinoCandidateFormW*)>(
                       GetProcAddress(h, "ImmSetCandidateWindow"));
        ok = get_ctx && rel_ctx && set_comp && set_cand;
    }
};

// caret_x / top / bottom 都是**物理客户区**像素。
// 组合串贴着输入框下沿开始（不遮挡正在编辑的内容），候选框再往下一点。
inline void win_set_ime_pos(Win32State* s, float caret_x, float top, float bottom) {
    (void)top;
    if (!s || !s->hwnd) return;
    static ImmApi api;
    if (!api.ok) return;
    HIMC himc = api.get_ctx(s->hwnd);
    if (!himc) return;

    AinoCompositionFormW cf{};
    cf.dwStyle = AINO_CFS_POINT;
    cf.ptCurrentPos.x = LONG(caret_x);
    cf.ptCurrentPos.y = LONG(bottom);
    api.set_comp(himc, &cf);

    AinoCandidateFormW cd{};
    cd.dwIndex = 0;
    cd.dwStyle = AINO_CFS_CANDIDATEPOS;
    cd.ptCurrentPos.x = LONG(caret_x);
    cd.ptCurrentPos.y = LONG(bottom + 4.0f);
    api.set_cand(himc, &cd);

    api.rel_ctx(s->hwnd, himc);
}

inline void win_apply_cursor(Win32State* s) {
    if (!s->owner) return;
    LPCWSTR name = AINO_IDC_ARROW;
    switch (s->owner->cursor) {
        case Cursor::Hand:    name = AINO_IDC_HAND;   break;
        case Cursor::IBeam:   name = AINO_IDC_IBEAM;  break;
        case Cursor::ResizeH: name = AINO_IDC_SIZEWE; break;
        case Cursor::ResizeV: name = AINO_IDC_SIZENS; break;
        default:              name = AINO_IDC_ARROW;  break;
    }
    SetCursor(LoadCursorW(nullptr, name));
}

inline LRESULT CALLBACK win_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Win32State* s = nullptr;
    if (m == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        s = reinterpret_cast<Win32State*>(cs->lpCreateParams);
        s->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    } else {
        s = reinterpret_cast<Win32State*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    if (!s) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_ERASEBKGND:
        return 1;   // 我们自己整块重画，禁止系统擦背景（防闪烁）

    case WM_PRINTCLIENT: {
        // 打印/截图（PrintWindow）走的路径 —— 直接把后备缓冲贴过去，
        // 顺带也支持了系统的窗口缩略图预览
        HDC dc = reinterpret_cast<HDC>(w);
        if (s->has_backbuffer && dc) BitBlt(dc, 0, 0, s->pw, s->ph, s->mem_dc, 0, 0, SRCCOPY);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        // 两条规矩：
        //   · 缓冲没在当前尺寸下渲染过 → 先补一帧（贴一块没画过的位图 = 贴黑）；
        //   · 画过 → **只贴，不重画**。系统叫我们重画（遮住又露出来、拖动、别的
        //     窗口盖住）只是要"把这块区域补上"，缓冲里就是最终画面，重渲染纯属浪费 ——
        //     以前这里无条件 need_frame = true，于是窗口被反复遮挡/暴露时
        //     会一路空转出帧（实测空闲 CPU 抖出 25%）。
        if (!s->bb_valid) win_render_sync(s);
        if (s->has_backbuffer && s->bb_valid) {
            HDC dc = ps.hdc;
            BitBlt(dc, 0, 0, s->pw, s->ph, s->mem_dc, 0, 0, SRCCOPY);
        }
        EndPaint(h, &ps);
        return 0;
    }

    case WM_SIZE: {
        int nw = LOWORD(l), nh = HIWORD(l);
        win_create_backbuffer(s, nw, nh);
        // 尺寸真的变了（缓冲被重建）→ 立刻同步画一帧再把标志交给主循环。
        // 不这么做的话，这里到主循环出帧之间（拖动时是**永远**）屏幕上就是黑的。
        if (!s->bb_valid) {
            win_render_sync(s);
            s->owner->need_frame = false;   // 刚画完，不必再排一帧
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        s->mx = float(GET_X_LPARAM(l));
        s->my = float(GET_Y_LPARAM(l));
        s->inside = true;
        if (!s->tracking) {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = h;
            TrackMouseEvent(&tme);
            s->tracking = true;
        }
        s->owner->need_frame = true;
        return 0;
    }

    case WM_MOUSELEAVE:
        s->tracking = false;
        s->inside = false;
        s->mx = -1e6f; s->my = -1e6f;
        s->owner->need_frame = true;
        return 0;

    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: {
        int idx = (m == WM_LBUTTONDOWN) ? 0 : (m == WM_RBUTTONDOWN ? 1 : 2);
        s->mx = float(GET_X_LPARAM(l)); s->my = float(GET_Y_LPARAM(l));
        s->inside = true;
        s->btn_down[idx] = true; s->btn_pressed[idx] = true;
        SetCapture(h);
        s->owner->need_frame = true;
        return 0;
    }

    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: {
        int idx = (m == WM_LBUTTONUP) ? 0 : (m == WM_RBUTTONUP ? 1 : 2);
        s->mx = float(GET_X_LPARAM(l)); s->my = float(GET_Y_LPARAM(l));
        s->btn_down[idx] = false; s->btn_released[idx] = true;
        if (!s->btn_down[0] && !s->btn_down[1] && !s->btn_down[2]) ReleaseCapture();
        s->owner->need_frame = true;
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        s->mx = float(GET_X_LPARAM(l)); s->my = float(GET_Y_LPARAM(l));
        s->btn_down[0] = true; s->btn_pressed[0] = true;
        s->clicks = 2;
        SetCapture(h);
        s->owner->need_frame = true;
        return 0;
    }

    case WM_MOUSEWHEEL: {
        s->wheel += float(GET_WHEEL_DELTA_WPARAM(w)) / 120.0f;
        s->owner->need_frame = true;
        return 0;
    }

    case WM_CHAR: {
        uint32_t unit = uint32_t(w);
        if (unit >= 32 || unit == 9) win_append_utf16(s->text_in, unit);
        s->owner->need_frame = true;
        return 0;
    }

    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        int k = win_map_vk(w);
        if (k > 0 && k < KEY_COUNT) {
            if (!s->key_down[k]) {
                s->key_pressed[k] = true;
                // 修饰位在**按下这一刻**定格：同一批消息里 Ctrl 的 KEYUP 可能
                // 已经先被处理掉，帧里再看 key_down 就晚了。
                s->key_ctrl[k]  = s->key_down[KEY_CTRL];
                s->key_shift[k] = s->key_down[KEY_SHIFT];
                s->key_alt[k]   = s->key_down[KEY_ALT];
            }
            s->key_down[k] = true;
        }
        s->owner->need_frame = true;
        return 0;
    }

    case WM_KEYUP: case WM_SYSKEYUP: {
        int k = win_map_vk(w);
        if (k > 0 && k < KEY_COUNT) s->key_down[k] = false;
        s->owner->need_frame = true;
        return 0;
    }

    case WM_KILLFOCUS:
        for (int i = 0; i < 3; ++i) s->btn_down[i] = false;
        for (int i = 0; i < KEY_COUNT; ++i) s->key_down[i] = false;
        s->owner->need_frame = true;
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) { win_apply_cursor(s); return TRUE; }
        break;

    case WM_DPICHANGED: {
        s->owner->scale = float(HIWORD(w)) / 96.0f;
        RECT* r = reinterpret_cast<RECT*>(l);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        s->owner->need_frame = true;
        return 0;
    }

    case WM_CLOSE:
        s->owner->running = false;
        PostQuitMessage(0);
        return 0;

    case WM_DESTROY:
        s->owner->running = false;
        return 0;

    default: break;
    }
    return DefWindowProcW(h, m, w, l);
}

inline void w32_create(Window* self, int w, int h, const std::string& title) {
    Window::Impl* o = self->impl;
    win_enable_dpi_awareness();

    Win32State* s = new Win32State();
    s->owner = o;
    o->native = s;

    static bool class_registered = false;
    if (!class_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_OWNDC;
        wc.lpfnWndProc   = win_proc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, AINO_IDC_ARROW);
        wc.lpszClassName = L"AinoWindowClass";
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        class_registered = true;
    }

    UINT sys_dpi = win_dpi_of(nullptr);
    o->scale = float(sys_dpi) / 96.0f;

    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rc = {0, 0, LONG(float(w) * o->scale), LONG(float(h) * o->scale)};
    AdjustWindowRectEx(&rc, style, FALSE, 0);

    std::wstring wtitle = to_wide(title.empty() ? std::string("aino") : title);
    HWND hwnd = CreateWindowExW(0, L"AinoWindowClass", wtitle.c_str(), style,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, GetModuleHandleW(nullptr), s);
    if (!hwnd) { delete s; o->native = nullptr; return; }

    o->scale = float(win_dpi_of(hwnd)) / 96.0f;

    GdiTextRenderer* tr = new GdiTextRenderer();
    tr->init(o->scale, o->theme.font_family, o->theme.font_family_mono);
    o->text = tr;
    o->ui.impl->text = tr;

    RECT cr;
    GetClientRect(hwnd, &cr);
    win_create_backbuffer(s, cr.right - cr.left, cr.bottom - cr.top);
    o->ui.impl->canvas = &o->canvas;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    o->need_frame = true;
}

inline void w32_destroy(Window* self) {
    Window::Impl* o = self->impl;
    Win32State* s = reinterpret_cast<Win32State*>(o->native);
    if (!s) return;
    if (s->mem_dc) {
        // 先把位图换出再删 DC，避免句柄泄漏
        HBITMAP tmp = CreateCompatibleBitmap(s->mem_dc, 1, 1);
        if (tmp) { SelectObject(s->mem_dc, tmp); DeleteObject(tmp); }
        DeleteDC(s->mem_dc);
        s->mem_dc = nullptr;
    }
    if (s->hbmp) { DeleteObject(s->hbmp); s->hbmp = nullptr; }
    if (s->hwnd && IsWindow(s->hwnd)) DestroyWindow(s->hwnd);
    delete s;
    o->native = nullptr;
}

// 把这一轮累积的 Win32 消息转成 aino::Input
inline void win_feed_input(Win32State* s) {
    Window::Impl* o = s->owner;
    Ui::Impl* u = o->ui.impl;
    Input& in = u->input;

    const Vec2 prev = in.mouse;
    in.mouse = Vec2(s->mx / o->scale, s->my / o->scale);
    in.mouse_prev = prev;
    in.mouse_delta = Vec2(in.mouse.x - prev.x, in.mouse.y - prev.y);

    for (int i = 0; i < 3; ++i) {
        in.pressed[i]  = s->btn_pressed[i];
        in.released[i] = s->btn_released[i];
        in.down[i]     = s->btn_down[i];
    }
    in.wheel = s->wheel;
    in.double_click = (s->clicks >= 2);
    in.click_count = s->clicks;
    in.text_input = s->text_in;
    for (int i = 0; i < KEY_COUNT; ++i) {
        in.key_down[i]    = s->key_down[i];
        in.key_pressed[i] = s->key_pressed[i];
        in.key_ctrl[i]    = s->key_ctrl[i];
        in.key_shift[i]   = s->key_shift[i];
        in.key_alt[i]     = s->key_alt[i];
    }
    in.dragging = s->btn_down[0];

    s->btn_pressed[0] = s->btn_pressed[1] = s->btn_pressed[2] = false;
    s->btn_released[0] = s->btn_released[1] = s->btn_released[2] = false;
    s->wheel = 0.0f;
    s->clicks = 0;
    s->text_in.clear();
    for (int i = 0; i < KEY_COUNT; ++i) s->key_pressed[i] = false;
}

inline uint64_t win_ticks_ms() {
    return uint64_t(GetTickCount64());
}

inline uint64_t win_ticks_us() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f;
    }();
    LARGE_INTEGER c{}; QueryPerformanceCounter(&c);
    return uint64_t(double(c.QuadPart) * 1e6 / double(freq.QuadPart));
}

// 每 120 帧汇总一次渲染耗时（只在 AINO_FRAME_LOG=1 时被调用）。
inline void win_frame_log(Window::Impl* o, uint64_t render_us) {
    ++o->frame_log_n;
    o->frame_log_sum += render_us;
    if (render_us > o->frame_log_worst) o->frame_log_worst = uint32_t(render_us);
    if (o->frame_log_n >= 120) {
        const double n = double(o->frame_log_n);
        std::fprintf(stderr,
            "[aino] 120 帧  渲染均 %.2f ms  最差 %.2f ms  (预算 %.0f ms)\n"
            "        取输入 %.2f | 绘制 %.2f | 收尾 %.2f | 上屏 %.2f  (ms)\n",
            double(o->frame_log_sum) / n / 1000.0, double(o->frame_log_worst) / 1000.0,
            double(o->anim_budget_ms),
            double(o->t_feed) / n / 1000.0, double(o->t_cb) / n / 1000.0,
            double(o->t_end) / n / 1000.0, double(o->t_present) / n / 1000.0);
        o->frame_log_n = 0; o->frame_log_sum = 0; o->frame_log_worst = 0;
        o->t_feed = o->t_cb = o->t_end = o->t_present = 0;
    }
}

} // namespace detail

inline bool Window::poll() {
    Window::Impl* o = impl;
    detail::Win32State* s = reinterpret_cast<detail::Win32State*>(o->native);
    if (!s || !o->running) return false;

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { o->running = false; break; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return o->running;
}

inline void Window::render() {
    Window::Impl* o = impl;
    detail::Win32State* s = reinterpret_cast<detail::Win32State*>(o->native);
    if (!s || !s->has_backbuffer) return;

    static uint64_t last = 0;
    uint64_t now = detail::win_ticks_ms();
    float dt = last ? float(now - last) / 1000.0f : (1.0f / 60.0f);
    if (dt > 0.25f) dt = 0.25f;
    last = now;

    const bool prof = o->frame_log;
    uint64_t p0 = prof ? detail::win_ticks_us() : 0;

    detail::win_feed_input(s);
    uint64_t pa = prof ? detail::win_ticks_us() : 0;

    // 必须在构建帧"之前"清标志：帧内任何 repaint()（动画、光标闪烁）都会
    // 重新把它置 true，从而让下一帧继续。放到帧末清会把请求吃掉，动画就卡在第一帧。
    o->need_frame = false;

    Ui& ui = o->ui;
    ui.begin_frame(float(s->pw) / o->scale, float(s->ph) / o->scale, dt, o->scale);
    if (o->frame_cb_ptr) o->frame_cb_ptr();
    ui.set_cursor(ui.cursor());
    uint64_t p1 = prof ? detail::win_ticks_us() : 0;
    ui.end_frame();
    uint64_t p2 = prof ? detail::win_ticks_us() : 0;

    o->cursor = ui.cursor();
    detail::win_present(s);
    s->bb_valid = true;          // 这块缓冲现在装的是"当前尺寸下画过的画面"
    detail::win_apply_cursor(s);
    if (prof) {
        uint64_t p3 = detail::win_ticks_us();
        o->t_feed += pa - p0;      // 取输入
        o->t_cb += p1 - pa;        // begin_frame + 用户帧回调（即真正的绘制）
        o->t_end += p2 - p1;       // end_frame（延迟浮层等）
        o->t_present += p3 - p2;   // 上屏
    }
}

inline void Window::run(const std::function<void(Ui&)>& frame) {
    Window::Impl* o = impl;
    detail::Win32State* s = reinterpret_cast<detail::Win32State*>(o->native);
    if (!s) return;

    o->frame_cb_ptr = [&frame, o]() { frame(o->ui); };
    // 窗口过程（WM_SIZE / WM_PAINT）要能在 user32 的模态循环里自己补一帧
    o->render_fn = [this]() { render(); };

    while (o->running) {
        // 先把消息队列抽干
        if (!poll()) break;

        const bool anim = o->ui.animating();
        if (o->need_frame || anim) {
            o->self_repaint = false;                 // 本轮"自请求"标记清零
            const uint64_t t0 = detail::win_ticks_us();
            render();
            const uint64_t el_us = detail::win_ticks_us() - t0;
            if (anim || o->self_repaint) {
                if (o->frame_log) detail::win_frame_log(o, el_us);
                const uint64_t budget_us = uint64_t(o->anim_budget_ms) * 1000u;
                if (el_us < budget_us) Sleep(DWORD((budget_us - el_us + 999) / 1000));
            }
        } else {
            // 没有待办 → 阻塞在内核里等消息。这就是空闲 0% CPU 的来源。
            WaitMessage();
        }
    }
    o->frame_cb_ptr = nullptr;
    o->render_fn = nullptr;
}

inline void Window::set_title(const std::string& title) {
    Window::Impl* o = impl;
    if (!o) return;
    detail::Win32State* s = reinterpret_cast<detail::Win32State*>(o->native);
    if (s && s->hwnd) SetWindowTextW(s->hwnd, detail::to_wide(title).c_str());
}

inline void Window::set_size(int w, int h) {
    Window::Impl* o = impl;
    detail::Win32State* s = reinterpret_cast<detail::Win32State*>(o->native);
    if (!s || !s->hwnd) return;
    RECT rc = {0, 0, LONG(float(w) * o->scale), LONG(float(h) * o->scale)};
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);
    SetWindowPos(s->hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

inline void Window::close() {
    Window::Impl* o = impl;
    if (!o) return;
    o->running = false;
    // 主循环多数时间阻塞在 WaitMessage() 里 —— 只置标志位叫不醒它，得投一条
    // 消息把阻塞打断。从别的线程（后台任务、自动化探针）调 close() 时这条是
    // 必须的，否则进程会一直挂着不退出。WM_NULL 对窗口本身没有任何副作用。
    detail::Win32State* s = reinterpret_cast<detail::Win32State*>(o->native);
    if (s && s->hwnd) PostMessageW(s->hwnd, WM_NULL, 0, 0);
}

inline void Window::set_clipboard(const std::string& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    std::wstring w = detail::to_wide(text);
    size_t bytes = (w.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem) {
        void* dst = GlobalLock(mem);
        if (dst) { memcpy(dst, w.c_str(), bytes); GlobalUnlock(mem); SetClipboardData(CF_UNICODETEXT, mem); }
    }
    CloseClipboard();
}

inline std::string Window::clipboard() const {
    std::string out;
    if (!OpenClipboard(nullptr)) return out;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = reinterpret_cast<const wchar_t*>(GlobalLock(h));
        if (p) { out = detail::from_wide(std::wstring(p)); GlobalUnlock(h); }
    }
    CloseClipboard();
    return out;
}

#endif // AINO_PLATFORM_WINDOWS

// =============================================================================
//  8.14  Linux / X11 后端
//        用 Xlib 的 FontSet 直接画 UTF-8，不引入 freetype/fontconfig，
//        保持"无第三方依赖"。中文依赖系统已装的 CJK 字体。
//        注意：本后端在编写时没有 Linux 环境可验证，逻辑按 Xlib 标准用法写。
// =============================================================================
#if defined(AINO_PLATFORM_X11)
namespace detail {

class X11TextRenderer : public TextRenderer {
public:
    void init(float, const std::string& family, const std::string&) override {
        dpy_ = XOpenDisplay(nullptr);
        family_ = family.empty() ? std::string("*") : family;
    }
    void release() override {
        for (auto& kv : sets_) if (kv.second) XFreeFontSet(dpy_, kv.second);
        sets_.clear();
        dpy_ = nullptr;
    }
    float measure(const std::string& utf8, float size, int) override {
        XFontSet fs = fontset(size);
        if (!fs) return float(utf8.size()) * size * 0.55f;
        XRectangle ink{}, logical{};
        Xutf8TextExtents(fs, utf8.c_str(), int(utf8.size()), &ink, &logical);
        return float(logical.width);
    }
    float ascent(float size, int) override {
        XFontSet fs = fontset(size);
        if (!fs) return size * 0.8f;
        XFontSetExtents* e = XExtentsOfFontSet(fs);
        if (!e) return size * 0.8f;
        return float(-e->max_logical_extent.y);
    }
    float line_height(float size, int w) override {
        XFontSet fs = fontset(size);
        if (!fs) return size * 1.4f;
        XFontSetExtents* e = XExtentsOfFontSet(fs);
        if (!e) return size * 1.4f;
        return float(e->max_logical_extent.height);
        (void)w;
    }
    void draw(Canvas& cv, const std::string& utf8, float x, float y_top,
              float size, int weight, const Color& c, const Rect& clip) override {
        XFontSet fs = fontset(size);
        Display* d = cv.platform_dc ? reinterpret_cast<Display*>(cv.platform_dc) : dpy_;
        if (!fs || !d) return;
        // Canvas 只有在被 render 时才拿得到 GC 句柄，见 x11_present
        GC gc = reinterpret_cast<GC>(cv.platform_gc);
        if (!gc) return;
        (void)weight;
        XSetForeground(d, gc, (unsigned long(c.r) << 16) | (unsigned long(c.g) << 8) | c.b);
        float base_y = y_top + ascent(size, weight);
        if (clip.w >= 0.0f) {
            // Xlib 没有 save/restore，改完要还原
            XRectangle old{};
            int oclip = 0;
            (void)oclip;
            XRectangle r;
            r.x = short(std::max(0.0f, clip.x)); r.y = short(std::max(0.0f, clip.y));
            r.width = short(std::max(0.0f, clip.w)); r.height = short(std::max(0.0f, clip.h));
            XSetClipRectangles(d, gc, 0, 0, &r, 1, Unsorted);
            Xutf8DrawString(d, gc_target(d), fs, gc, int(x), int(base_y), utf8.c_str(), int(utf8.size()));
            XSetClipMask(d, gc, None);
            (void)old;
        } else {
            Xutf8DrawString(d, gc_target(d), fs, gc, int(x), int(base_y), utf8.c_str(), int(utf8.size()));
        }
    }

    void set_drawable(Display* d, Drawable target, GC gc) { dpy_ = d; target_ = target; gc_ = gc; }

private:
    Drawable gc_target(Display*) const { return target_; }
    XFontSet fontset(float size) {
        int q = int(size * 2.0f + 0.5f);
        auto it = sets_.find(q);
        if (it != sets_.end()) return it->second;
        if (!dpy_) return nullptr;
        char pattern[256];
        snprintf(pattern, sizeof(pattern), "-*-%s-*-*-*-*-*-%d-*-*-*-*-*-*",
                 family_.c_str(), int(size + 0.5f));
        char** missing = nullptr;
        int nmissing = 0;
        char* def = nullptr;
        XFontSet fs = XCreateFontSet(dpy_, pattern, &missing, &nmissing, &def);
        if (missing) XFreeStringList(missing);
        if (!fs) fs = XCreateFontSet(dpy_, "-*-*-*-*-*-*-*-*-*-*-*-*-*-*", &missing, &nmissing, &def);
        sets_[q] = fs;
        return fs;
    }
    Display* dpy_ = nullptr;
    Drawable target_ = 0;
    GC gc_ = nullptr;
    std::string family_;
    std::unordered_map<int, XFontSet> sets_;
};

struct X11State {
    Window::Impl* owner = nullptr;
    Display* dpy = nullptr;
    ::Window win = 0;
    GC gc = nullptr;
    XImage* img = nullptr;
    std::vector<uint8_t> buffer;
    Atom wm_delete = 0;
    XIM im = nullptr;
    XIC ic = nullptr;
    int pw = 0, ph = 0;

    float mx = -1e6f, my = -1e6f;
    bool  btn_down[3] = {false, false, false};
    bool  btn_pressed[3] = {false, false, false};
    bool  btn_released[3] = {false, false, false};
    float wheel = 0.0f;
    int   clicks = 0;
    std::string text_in;
    bool  key_down[KEY_COUNT] = {};
    bool  key_pressed[KEY_COUNT] = {};
    bool  key_ctrl[KEY_COUNT]  = {};
    bool  key_shift[KEY_COUNT] = {};
    bool  key_alt[KEY_COUNT]   = {};
};

inline int x11_map_keysym(KeySym ks) {
    if (ks >= XK_a && ks <= XK_z) return int('A' + (ks - XK_a));
    if (ks >= XK_A && ks <= XK_Z) return int(ks);
    if (ks >= XK_0 && ks <= XK_9) return int(ks);
    switch (ks) {
        case XK_BackSpace: return KEY_BACKSPACE;
        case XK_Tab:       return KEY_TAB;
        case XK_Return:    return KEY_ENTER;
        case XK_Escape:    return KEY_ESC;
        case XK_space:     return KEY_SPACE;
        case XK_Delete:    return KEY_DELETE;
        case XK_Left:      return KEY_LEFT;
        case XK_Right:     return KEY_RIGHT;
        case XK_Up:        return KEY_UP;
        case XK_Down:      return KEY_DOWN;
        case XK_Home:      return KEY_HOME;
        case XK_End:       return KEY_END;
        case XK_Prior:     return KEY_PAGEUP;
        case XK_Next:      return KEY_PAGEDOWN;
        case XK_Shift_L: case XK_Shift_R: return KEY_SHIFT;
        case XK_Control_L: case XK_Control_R: return KEY_CTRL;
        case XK_Alt_L: case XK_Alt_R: return KEY_ALT;
        case XK_Super_L: case XK_Super_R: return KEY_SUPER;
        case XK_F1: return KEY_F1; case XK_F2: return KEY_F2; case XK_F3: return KEY_F3;
        case XK_F4: return KEY_F4; case XK_F5: return KEY_F5; case XK_F6: return KEY_F6;
        case XK_F7: return KEY_F7; case XK_F8: return KEY_F8; case XK_F9: return KEY_F9;
        case XK_F10: return KEY_F10; case XK_F11: return KEY_F11; case XK_F12: return KEY_F12;
        default: return 0;
    }
}

inline void x11_create_backbuffer(X11State* s, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (s->pw == w && s->ph == h && s->img) return;
    if (s->img) { s->img->data = nullptr; XDestroyImage(s->img); s->img = nullptr; }

    XWindowAttributes wa{};
    XGetWindowAttributes(s->dpy, s->win, &wa);
    s->buffer.assign(size_t(w) * size_t(h) * 4u, 0);
    s->img = XCreateImage(s->dpy, wa.visual, unsigned(wa.depth), ZPixmap, 0,
                          reinterpret_cast<char*>(s->buffer.data()), unsigned(w), unsigned(h),
                          32, w * 4);
    if (!s->img) return;
    s->pw = w; s->ph = h;

    Window::Impl* o = s->owner;
    o->canvas.px = s->buffer.data();
    o->canvas.w = w; o->canvas.h = h;
    o->canvas.stride = w * 4;
    o->canvas.platform_dc = s->dpy;
    o->canvas.platform_gc = s->gc;
    o->canvas.clear_clip();
    o->client_w = w; o->client_h = h;
}

inline void x11_present(X11State* s) {
    if (!s->img) return;
    XPutImage(s->dpy, s->win, s->gc, s->img, 0, 0, 0, 0, unsigned(s->pw), unsigned(s->ph));
    XFlush(s->dpy);
}

inline void x11_apply_cursor(X11State* s) {
    if (!s->owner || !s->win) return;
    unsigned shape = XC_left_ptr;
    switch (s->owner->cursor) {
        case Cursor::Hand:    shape = XC_hand2; break;
        case Cursor::IBeam:   shape = XC_xterm; break;
        case Cursor::ResizeH: shape = XC_sb_h_double_arrow; break;
        case Cursor::ResizeV: shape = XC_sb_v_double_arrow; break;
        default:              shape = XC_left_ptr; break;
    }
    Cursor c = XCreateFontCursor(s->dpy, shape);
    if (c) { XDefineCursor(s->dpy, s->win, c); XFreeCursor(s->dpy, c); }
}

inline void x11_feed_input(X11State* s) {
    Window::Impl* o = s->owner;
    Ui::Impl* u = o->ui.impl;
    Input& in = u->input;
    const Vec2 prev = in.mouse;
    in.mouse = Vec2(s->mx / o->scale, s->my / o->scale);
    in.mouse_prev = prev;
    in.mouse_delta = Vec2(in.mouse.x - prev.x, in.mouse.y - prev.y);
    for (int i = 0; i < 3; ++i) { in.pressed[i] = s->btn_pressed[i]; in.released[i] = s->btn_released[i]; in.down[i] = s->btn_down[i]; }
    in.wheel = s->wheel;
    in.double_click = (s->clicks >= 2);
    in.click_count = s->clicks;
    in.text_input = s->text_in;
    for (int i = 0; i < KEY_COUNT; ++i) {
        in.key_down[i] = s->key_down[i]; in.key_pressed[i] = s->key_pressed[i];
        in.key_ctrl[i] = s->key_ctrl[i]; in.key_shift[i] = s->key_shift[i]; in.key_alt[i] = s->key_alt[i];
    }
    in.dragging = s->btn_down[0];
    for (int i = 0; i < 3; ++i) { s->btn_pressed[i] = false; s->btn_released[i] = false; }
    s->wheel = 0.0f; s->clicks = 0; s->text_in.clear();
    for (int i = 0; i < KEY_COUNT; ++i) s->key_pressed[i] = false;
}

inline bool x11_handle(X11State* s, XEvent& e) {
    Window::Impl* o = s->owner;
    switch (e.type) {
    case Expose:
        o->need_frame = true;
        break;
    case ConfigureNotify:
        x11_create_backbuffer(s, e.xconfigure.width, e.xconfigure.height);
        o->need_frame = true;
        break;
    case MotionNotify:
        s->mx = float(e.xmotion.x); s->my = float(e.xmotion.y);
        o->need_frame = true;
        break;
    case ButtonPress: {
        s->mx = float(e.xbutton.x); s->my = float(e.xbutton.y);
        if (e.xbutton.button == Button4) { s->wheel += 1.0f; o->need_frame = true; break; }
        if (e.xbutton.button == Button5) { s->wheel -= 1.0f; o->need_frame = true; break; }
        int idx = (e.xbutton.button == Button1) ? 0 : (e.xbutton.button == Button3 ? 1 : 2);
        s->btn_down[idx] = true; s->btn_pressed[idx] = true;
        o->need_frame = true;
        break;
    }
    case ButtonRelease: {
        s->mx = float(e.xbutton.x); s->my = float(e.xbutton.y);
        int idx = (e.xbutton.button == Button1) ? 0 : (e.xbutton.button == Button3 ? 1 : 2);
        s->btn_down[idx] = false; s->btn_released[idx] = true;
        o->need_frame = true;
        break;
    }
    case KeyPress: {
        KeySym ks = XLookupKeysym(&e.xkey, 0);
        int k = x11_map_keysym(ks);
        if (k > 0 && k < KEY_COUNT) {
            if (!s->key_down[k]) {
                s->key_pressed[k] = true;
                s->key_ctrl[k]  = s->key_down[KEY_CTRL];
                s->key_shift[k] = s->key_down[KEY_SHIFT];
                s->key_alt[k]   = s->key_down[KEY_ALT];
            }
            s->key_down[k] = true;
        }
        if (s->ic) {
            char buf[64];
            Status st = 0;
            KeySym dummy;
            int n = Xutf8LookupString(s->ic, &e.xkey, buf, sizeof(buf) - 1, &dummy, &st);
            if (n > 0 && (st == XLookupChars || st == XLookupBoth)) {
                buf[n] = 0;
                for (int i = 0; i < n; ++i) if (uint8_t(buf[i]) >= 32 || buf[i] == '\t') s->text_in.push_back(buf[i]);
            }
        } else {
            char buf[16];
            int n = XLookupString(&e.xkey, buf, sizeof(buf) - 1, nullptr, nullptr);
            for (int i = 0; i < n; ++i) if (uint8_t(buf[i]) >= 32) s->text_in.push_back(buf[i]);
        }
        o->need_frame = true;
        break;
    }
    case KeyRelease: {
        KeySym ks = XLookupKeysym(&e.xkey, 0);
        int k = x11_map_keysym(ks);
        if (k > 0 && k < KEY_COUNT) s->key_down[k] = false;
        o->need_frame = true;
        break;
    }
    case ClientMessage:
        if (Atom(e.xclient.data.l[0]) == s->wm_delete) { o->running = false; return false; }
        break;
    case LeaveNotify:
        s->mx = -1e6f; s->my = -1e6f;
        o->need_frame = true;
        break;
    default: break;
    }
    return true;
}

inline void x11_create(Window* self, int w, int h, const std::string& title) {
    Window::Impl* o = self->impl;
    X11State* s = new X11State();
    s->owner = o;
    o->native = s;

    setlocale(LC_ALL, "");
    s->dpy = XOpenDisplay(nullptr);
    if (!s->dpy) { delete s; o->native = nullptr; return; }

    int screen = DefaultScreen(s->dpy);
    ::Window root = RootWindow(s->dpy, screen);
    s->win = XCreateSimpleWindow(s->dpy, root, 0, 0, unsigned(w), unsigned(h), 0,
                                 BlackPixel(s->dpy, screen), WhitePixel(s->dpy, screen));
    XStoreName(s->dpy, s->win, title.c_str());
    XSelectInput(s->dpy, s->win,
                 ExposureMask | StructureNotifyMask | PointerMotionMask |
                 ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask |
                 EnterWindowMask | LeaveWindowMask);
    s->wm_delete = XInternAtom(s->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(s->dpy, s->win, &s->wm_delete, 1);
    s->gc = XCreateGC(s->dpy, s->win, 0, nullptr);

    // 输入法：中文输入需要 XIM/XIC
    if (XSetLocaleModifiers("") != nullptr) {
        s->im = XOpenIM(s->dpy, nullptr, nullptr, nullptr);
        if (s->im) {
            s->ic = XCreateIC(s->im, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                              XNClientWindow, s->win, XNFocusWindow, s->win, nullptr);
            if (s->ic) XSetICFocus(s->ic);
        }
    }

    XMapWindow(s->dpy, s->win);
    XFlush(s->dpy);

    X11TextRenderer* tr = new X11TextRenderer();
    tr->init(o->scale, o->theme.font_family, o->theme.font_family_mono);
    tr->set_drawable(s->dpy, s->win, s->gc);
    o->text = tr;
    o->ui.impl->text = tr;

    x11_create_backbuffer(s, w, h);
    o->ui.impl->canvas = &o->canvas;
    o->need_frame = true;
}

inline void x11_destroy(Window* self) {
    Window::Impl* o = self->impl;
    X11State* s = reinterpret_cast<X11State*>(o->native);
    if (!s) return;
    if (s->img) { s->img->data = nullptr; XDestroyImage(s->img); s->img = nullptr; }
    if (s->ic) { XDestroyIC(s->ic); s->ic = nullptr; }
    if (s->im) { XCloseIM(s->im); s->im = nullptr; }
    if (s->gc) { XFreeGC(s->dpy, s->gc); s->gc = nullptr; }
    if (s->win) XDestroyWindow(s->dpy, s->win);
    if (s->dpy) XCloseDisplay(s->dpy);
    delete s;
    o->native = nullptr;
}

inline uint64_t x11_ticks_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000u + uint64_t(ts.tv_nsec / 1000000);
}

} // namespace detail

inline bool Window::poll() {
    Window::Impl* o = impl;
    detail::X11State* s = reinterpret_cast<detail::X11State*>(o->native);
    if (!s || !o->running) return false;
    while (XPending(s->dpy)) {
        XEvent e;
        XNextEvent(s->dpy, &e);
        if (!detail::x11_handle(s, e)) break;
    }
    return o->running;
}

inline void Window::render() {
    Window::Impl* o = impl;
    detail::X11State* s = reinterpret_cast<detail::X11State*>(o->native);
    if (!s || !s->img) return;

    static uint64_t last = 0;
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = uint64_t(ts.tv_sec) * 1000u + uint64_t(ts.tv_nsec / 1000000);
    float dt = last ? float(now - last) / 1000.0f : (1.0f / 60.0f);
    if (dt > 0.25f) dt = 0.25f;
    last = now;

    detail::x11_feed_input(s);
    o->need_frame = false;   // 同 Windows：帧前清，帧内 repaint() 可再次置位
    Ui& ui = o->ui;
    ui.begin_frame(float(s->pw) / o->scale, float(s->ph) / o->scale, dt, o->scale);
    if (o->frame_cb_ptr) o->frame_cb_ptr();
    ui.end_frame();
    o->cursor = ui.cursor();
    detail::x11_present(s);
    detail::x11_apply_cursor(s);
}

inline void Window::run(const std::function<void(Ui&)>& frame) {
    Window::Impl* o = impl;
    detail::X11State* s = reinterpret_cast<detail::X11State*>(o->native);
    if (!s) return;
    o->frame_cb_ptr = [&frame, o]() { frame(o->ui); };

    while (o->running) {
        if (!poll()) break;
        const bool anim = o->ui.animating();
        if (o->need_frame || anim) {
            o->self_repaint = false;
            const uint64_t t0 = detail::x11_ticks_ms();
            render();
            // 连续动画节流到 anim_fps（默认 60fps），并减去本帧渲染耗时。
            if (anim || o->self_repaint) {
                const uint64_t el = detail::x11_ticks_ms() - t0;
                if (el < o->anim_budget_ms) usleep(useconds_t((o->anim_budget_ms - el) * 1000));
            }
        } else {
            // 阻塞等事件（XNextEvent 会睡在内核里）
            XEvent e;
            XNextEvent(s->dpy, &e);
            detail::x11_handle(s, e);
        }
    }
    o->frame_cb_ptr = nullptr;
}

inline void Window::set_title(const std::string& title) {
    detail::X11State* s = reinterpret_cast<detail::X11State*>(impl->native);
    if (s && s->dpy && s->win) XStoreName(s->dpy, s->win, title.c_str());
}
inline void Window::set_size(int w, int h) {
    detail::X11State* s = reinterpret_cast<detail::X11State*>(impl->native);
    if (s && s->dpy && s->win) XResizeWindow(s->dpy, s->win, unsigned(w), unsigned(h));
}
inline void Window::close() { if (impl) impl->running = false; }

inline void Window::set_clipboard(const std::string& text) {
    detail::X11State* s = reinterpret_cast<detail::X11State*>(impl->native);
    if (!s || !s->dpy) return;
    Atom sel = XInternAtom(s->dpy, "CLIPBOARD", False);
    XSetSelectionOwner(s->dpy, sel, s->win, CurrentTime);
    (void)text;   // 完整实现需要响应 SelectionRequest，这里只做形式上的所有权声明
}
inline std::string Window::clipboard() const { return std::string(); }

#endif // AINO_PLATFORM_X11

// =============================================================================
//  8.15  macOS 后端
//        纯 C++ 通过 objc runtime 调 AppKit（不写 Objective-C 语法，
//        这样头文件在普通 .cpp 里也能编译）；绘制用 CoreGraphics，
//        文字用 CoreText。同样未经真机验证。
// =============================================================================
#if defined(AINO_PLATFORM_MACOS)
extern "C" {
    id objc_getClass(const char* name);
    SEL sel_registerName(const char* name);
    id objc_msgSend(id self, SEL op, ...);
    Class objc_allocateClassPair(Class superclass, const char* name, size_t extraBytes);
    void objc_registerClassPair(Class cls);
    BOOL class_addMethod(Class cls, SEL name, IMP imp, const char* types);
}

namespace detail {

class CoreTextRenderer : public TextRenderer {
public:
    void init(float, const std::string& family, const std::string&) override {
        family_ = family.empty() ? std::string("Helvetica Neue") : family;
    }
    void release() override { for (auto& kv : fonts_) if (kv.second) CFRelease(kv.second); fonts_.clear(); }

    float measure(const std::string& utf8, float size, int weight) override {
        CTLineRef line = make_line(utf8, size, weight, Color(0, 0, 0));
        if (!line) return 0.0f;
        double w = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
        CFRelease(line);
        return float(w);
    }
    float ascent(float size, int weight) override {
        CTFontRef f = font(size, weight);
        if (!f) return size * 0.8f;
        return float(CTFontGetAscent(f));
    }
    float line_height(float size, int weight) override {
        CTFontRef f = font(size, weight);
        if (!f) return size * 1.4f;
        return float(CTFontGetAscent(f) + CTFontGetDescent(f) + CTFontGetLeading(f));
    }
    void draw(Canvas& cv, const std::string& utf8, float x, float y_top,
              float size, int weight, const Color& c, const Rect& clip) override {
        CGContextRef ctx = reinterpret_cast<CGContextRef>(cv.platform_dc);
        if (!ctx) return;
        CTLineRef line = make_line(utf8, size, weight, c);
        if (!line) return;

        CGContextSaveGState(ctx);
        if (clip.w >= 0.0f) CGContextClipToRect(ctx, CGRectMake(clip.x, clip.y, clip.w, clip.h));
        // CoreGraphics 原点在左下角，需要翻转
        CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);
        CGContextTranslateCTM(ctx, 0.0, CGFloat(cv.h));
        CGContextScaleCTM(ctx, 1.0, -1.0);
        CGContextSetTextPosition(ctx, x, y_top + ascent(size, weight));
        CTLineDraw(line, ctx);
        CGContextRestoreGState(ctx);
        CFRelease(line);
    }

private:
    CTFontRef font(float size, int weight) {
        int q = int(size * 2.0f + 0.5f);
        uint64_t key = (uint64_t(uint32_t(q)) << 32) | uint32_t(weight < 0 ? 400 : weight);
        auto it = fonts_.find(key);
        if (it != fonts_.end()) return it->second;
        CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, family_.c_str(), kCFStringEncodingUTF8);
        CGFloat w = CGFloat(weight < 0 ? 400 : weight) / 400.0;
        const CTFontSymbolicTraits traits = (weight >= 600) ? kCTFontBoldTrait : 0;
        CTFontRef base = CTFontCreateWithName(name, CGFloat(float(q) / 2.0f), nullptr);
        CTFontRef f = traits ? CTFontCreateCopyWithSymbolicTraits(base, 0.0, nullptr, traits, traits) : base;
        if (f != base) CFRelease(base);
        if (!f) f = CTFontCreateWithName(CFSTR("Helvetica"), CGFloat(float(q) / 2.0f), nullptr);
        CFRelease(name);
        (void)w;
        fonts_[key] = f;
        return f;
    }
    CTLineRef make_line(const std::string& utf8, float size, int weight, const Color& c) {
        CTFontRef f = font(size, weight);
        if (!f) return nullptr;
        CFStringRef str = CFStringCreateWithBytes(kCFAllocatorDefault,
            reinterpret_cast<const UInt8*>(utf8.c_str()), CFIndex(utf8.size()),
            kCFStringEncodingUTF8, false);
        if (!str) return nullptr;
        CFMutableAttributedStringRef attr = CFAttributedStringCreateMutable(kCFAllocatorDefault, 0);
        CFAttributedStringReplaceString(attr, CFRangeMake(0, 0), str);
        CFRange full = CFRangeMake(0, CFStringGetLength(str));
        CFAttributedStringSetAttribute(attr, full, kCTFontAttributeName, f);
        CGFloat comps[4] = { CGFloat(c.r) / 255.0, CGFloat(c.g) / 255.0, CGFloat(c.b) / 255.0, CGFloat(c.a) / 255.0 };
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGColorRef cg = CGColorCreate(space, comps);
        CFAttributedStringSetAttribute(attr, full, kCTForegroundColorAttributeName, cg);
        CTLineRef line = CTLineCreateWithAttributedString(attr);
        CGColorRelease(cg);
        CGColorSpaceRelease(space);
        CFRelease(str);
        CFRelease(attr);
        return line;
    }
    std::string family_;
    std::unordered_map<uint64_t, CTFontRef> fonts_;
};

struct CocoaState {
    Window::Impl* owner = nullptr;
    id window = nullptr;
    id view = nullptr;
    CGContextRef ctx = nullptr;
    std::vector<uint8_t> buffer;
    int pw = 0, ph = 0;

    float mx = -1e6f, my = -1e6f;
    bool  btn_down[3] = {false, false, false};
    bool  btn_pressed[3] = {false, false, false};
    bool  btn_released[3] = {false, false, false};
    float wheel = 0.0f;
    int   clicks = 0;
    std::string text_in;
    bool  key_down[KEY_COUNT] = {};
    bool  key_pressed[KEY_COUNT] = {};
};

inline void cocoa_create_backbuffer(CocoaState* s, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (s->pw == w && s->ph == h && s->ctx) return;
    if (s->ctx) { CGContextRelease(s->ctx); s->ctx = nullptr; }
    s->buffer.assign(size_t(w) * size_t(h) * 4u, 0);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    s->ctx = CGBitmapContextCreate(s->buffer.data(), size_t(w), size_t(h), 8, size_t(w) * 4u,
                                   space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(space);
    if (!s->ctx) return;
    s->pw = w; s->ph = h;
    Window::Impl* o = s->owner;
    o->canvas.px = s->buffer.data();
    o->canvas.w = w; o->canvas.h = h;
    o->canvas.stride = w * 4;
    o->canvas.platform_dc = s->ctx;
    o->canvas.clear_clip();
    o->client_w = w; o->client_h = h;
}

inline void cocoa_present(CocoaState* s) {
    if (!s->ctx || !s->view) return;
    CGImageRef image = CGBitmapContextCreateImage(s->ctx);
    if (!image) return;
    objc_msgSend(s->view, sel_registerName("setNeedsDisplay:"), (BOOL)YES);
    // 真正的绘制发生在 NSView 的 drawRect: 回调里，这里把图像先收起来
    s->pending_image = image;
    objc_msgSend(s->view, sel_registerName("display"));
}

inline void cocoa_feed_input(CocoaState* s) {
    Window::Impl* o = s->owner;
    Ui::Impl* u = o->ui.impl;
    Input& in = u->input;
    const Vec2 prev = in.mouse;
    in.mouse = Vec2(s->mx / o->scale, s->my / o->scale);
    in.mouse_prev = prev;
    in.mouse_delta = Vec2(in.mouse.x - prev.x, in.mouse.y - prev.y);
    for (int i = 0; i < 3; ++i) { in.pressed[i] = s->btn_pressed[i]; in.released[i] = s->btn_released[i]; in.down[i] = s->btn_down[i]; }
    in.wheel = s->wheel;
    in.click_count = s->clicks;
    in.double_click = (s->clicks >= 2);
    in.text_input = s->text_in;
    for (int i = 0; i < KEY_COUNT; ++i) {
        in.key_down[i] = s->key_down[i]; in.key_pressed[i] = s->key_pressed[i];
        in.key_ctrl[i] = s->key_ctrl[i]; in.key_shift[i] = s->key_shift[i]; in.key_alt[i] = s->key_alt[i];
    }
    in.dragging = s->btn_down[0];
    for (int i = 0; i < 3; ++i) { s->btn_pressed[i] = false; s->btn_released[i] = false; }
    s->wheel = 0.0f; s->clicks = 0; s->text_in.clear();
    for (int i = 0; i < KEY_COUNT; ++i) s->key_pressed[i] = false;
}

// --- NSView 子类的事件回调（C 函数，通过 class_addMethod 注册）---
inline CocoaState* cocoa_state_of(id self) {
    return reinterpret_cast<CocoaState*>(uintptr_t(objc_msgSend(self, sel_registerName("tag"))));
}

inline void cocoa_view_draw(id self, SEL, id) {
    CocoaState* s = cocoa_state_of(self);
    if (!s) return;
    id ctx_obj = objc_msgSend(objc_getClass("NSGraphicsContext"), sel_registerName("currentContext"));
    CGContextRef cg = reinterpret_cast<CGContextRef>(objc_msgSend(ctx_obj, sel_registerName("CGContext")));
    if (!cg || !s->ctx) return;
    CGImageRef img = CGBitmapContextCreateImage(s->ctx);
    if (!img) return;
    CGRect r = CGRectMake(0, 0, s->pw, s->ph);
    // NSView 默认原点在左下，图片需要翻转
    CGContextSaveGState(cg);
    CGContextTranslateCTM(cg, 0, CGFloat(s->ph));
    CGContextScaleCTM(cg, 1, -1);
    CGContextDrawImage(cg, r, img);
    CGContextRestoreGState(cg);
    CGImageRelease(img);
}

inline void cocoa_mouse_moved(id self, SEL, id ev) {
    CocoaState* s = cocoa_state_of(self);
    if (!s) return;
    CGPoint p = reinterpret_cast<CGPoint(*)(id, SEL)>(objc_msgSend)(ev, sel_registerName("locationInWindow"));
    s->mx = float(p.x);
    s->my = float(s->ph) - float(p.y);
    s->owner->need_frame = true;
}

inline void cocoa_mouse_down(id self, SEL, id ev) {
    CocoaState* s = cocoa_state_of(self);
    if (!s) return;
    cocoa_mouse_moved(self, nullptr, ev);
    s->btn_down[0] = true; s->btn_pressed[0] = true;
    s->owner->need_frame = true;
    (void)ev;
}

inline void cocoa_mouse_up(id self, SEL, id ev) {
    CocoaState* s = cocoa_state_of(self);
    if (!s) return;
    cocoa_mouse_moved(self, nullptr, ev);
    s->btn_down[0] = false; s->btn_released[0] = true;
    s->owner->need_frame = true;
}

inline void cocoa_key_down(id self, SEL, id ev) {
    CocoaState* s = cocoa_state_of(self);
    if (!s) return;
    CFStringRef chars = reinterpret_cast<CFStringRef(*)(id, SEL)>(objc_msgSend)(ev, sel_registerName("characters"));
    if (chars) {
        CFIndex n = CFStringGetLength(chars);
        char buf[8];
        for (CFIndex i = 0; i < n; ++i) {
            UniChar u = CFStringGetCharacterAtIndex(chars, i);
            if (u >= 32) {
                int len = 0;
                if (u < 0x80) { buf[0] = char(u); len = 1; }
                else if (u < 0x800) { buf[0] = char(0xC0 | (u >> 6)); buf[1] = char(0x80 | (u & 0x3F)); len = 2; }
                else { buf[0] = char(0xE0 | (u >> 12)); buf[1] = char(0x80 | ((u >> 6) & 0x3F)); buf[2] = char(0x80 | (u & 0x3F)); len = 3; }
                s->text_in.append(buf, size_t(len));
            }
        }
    }
    s->key_down[KEY_BACKSPACE] = false;
    s->owner->need_frame = true;
    (void)ev;
}

inline void cocoa_create(Window* self, int w, int h, const std::string& title) {
    Window::Impl* o = self->impl;
    CocoaState* s = new CocoaState();
    s->owner = o;
    o->native = s;

    Class app_cls = objc_getClass("NSApplication");
    id app = objc_msgSend(app_cls, sel_registerName("sharedApplication"));
    objc_msgSend(app, sel_registerName("setActivationPolicy:"), (long)0);

    // 动态注册 NSView 子类
    static Class view_cls = nullptr;
    if (!view_cls) {
        view_cls = objc_allocateClassPair(objc_getClass("NSView"), "AinoView", 0);
        class_addMethod(view_cls, sel_registerName("drawRect:"), reinterpret_cast<IMP>(cocoa_view_draw), "v@:@");
        class_addMethod(view_cls, sel_registerName("mouseMoved:"), reinterpret_cast<IMP>(cocoa_mouse_moved), "v@:@");
        class_addMethod(view_cls, sel_registerName("mouseDown:"), reinterpret_cast<IMP>(cocoa_mouse_down), "v@:@");
        class_addMethod(view_cls, sel_registerName("mouseUp:"), reinterpret_cast<IMP>(cocoa_mouse_up), "v@:@");
        class_addMethod(view_cls, sel_registerName("keyDown:"), reinterpret_cast<IMP>(cocoa_key_down), "v@:@");
        objc_registerClassPair(view_cls);
    }

    CGRect rect = CGRectMake(0, 0, CGFloat(w), CGFloat(h));
    id window = objc_msgSend(objc_msgSend(objc_getClass("NSWindow"), sel_registerName("alloc")),
                             sel_registerName("initWithContentRect:styleMask:backing:defer:"),
                             rect, (unsigned long)15, (unsigned long)2, (BOOL)NO);
    id view = objc_msgSend(objc_msgSend(view_cls, sel_registerName("alloc")),
                           sel_registerName("initWithFrame:"), rect);
    objc_msgSend(view, sel_registerName("setTag:"), (long)uintptr_t(s));
    objc_msgSend(view, sel_registerName("setWantsLayer:"), (BOOL)YES);
    objc_msgSend(window, sel_registerName("setContentView:"), view);

    CFStringRef t = CFStringCreateWithCString(kCFAllocatorDefault, title.c_str(), kCFStringEncodingUTF8);
    objc_msgSend(window, sel_registerName("setTitle:"), t);
    CFRelease(t);

    objc_msgSend(window, sel_registerName("makeKeyAndOrderFront:"), (id)nullptr);
    objc_msgSend(app, sel_registerName("activateIgnoringOtherApps:"), (BOOL)YES);

    s->window = window;
    s->view = view;
    o->scale = 1.0f;

    CoreTextRenderer* tr = new CoreTextRenderer();
    tr->init(o->scale, o->theme.font_family, o->theme.font_family_mono);
    o->text = tr;
    o->ui.impl->text = tr;

    cocoa_create_backbuffer(s, w, h);
    o->ui.impl->canvas = &o->canvas;
    o->need_frame = true;
}

inline void cocoa_destroy(Window* self) {
    Window::Impl* o = self->impl;
    CocoaState* s = reinterpret_cast<CocoaState*>(o->native);
    if (!s) return;
    if (s->ctx) { CGContextRelease(s->ctx); s->ctx = nullptr; }
    if (s->window) objc_msgSend(s->window, sel_registerName("close"));
    delete s;
    o->native = nullptr;
}

} // namespace detail

inline bool Window::poll() {
    Window::Impl* o = impl;
    detail::CocoaState* s = reinterpret_cast<detail::CocoaState*>(o->native);
    if (!s) return false;
    // 抽干一次 runloop
    id distant = objc_msgSend(objc_msgSend(objc_getClass("NSDate"), sel_registerName("alloc")),
                              sel_registerName("init"));
    while (true) {
        id ev = objc_msgSend(objc_msgSend(objc_getClass("NSApplication"), sel_registerName("sharedApplication")),
                             sel_registerName("nextEventMatchingMask:untilDate:inMode:dequeue:"),
                             (unsigned long)-1, distant, objc_msgSend(objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), "kCFRunLoopDefaultMode"), (BOOL)YES);
        if (!ev) break;
        objc_msgSend(objc_msgSend(objc_getClass("NSApplication"), sel_registerName("sharedApplication")),
                     sel_registerName("sendEvent:"), ev);
    }
    objc_msgSend(objc_msgSend(objc_getClass("NSApplication"), sel_registerName("sharedApplication")),
                 sel_registerName("updateWindows"));
    return o->running;
}

inline void Window::render() {
    Window::Impl* o = impl;
    detail::CocoaState* s = reinterpret_cast<detail::CocoaState*>(o->native);
    if (!s || !s->ctx) return;
    static uint64_t last = 0;
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = uint64_t(ts.tv_sec) * 1000u + uint64_t(ts.tv_nsec / 1000000);
    float dt = last ? float(now - last) / 1000.0f : (1.0f / 60.0f);
    if (dt > 0.25f) dt = 0.25f;
    last = now;

    detail::cocoa_feed_input(s);
    o->need_frame = false;   // 同 Windows：帧前清
    Ui& ui = o->ui;
    ui.begin_frame(float(s->pw) / o->scale, float(s->ph) / o->scale, dt, o->scale);
    if (o->frame_cb_ptr) o->frame_cb_ptr();
    ui.end_frame();
    o->cursor = ui.cursor();
    detail::cocoa_present(s);
}

inline void Window::run(const std::function<void(Ui&)>& frame) {
    Window::Impl* o = impl;
    detail::CocoaState* s = reinterpret_cast<detail::CocoaState*>(o->native);
    if (!s) return;
    o->frame_cb_ptr = [&frame, o]() { frame(o->ui); };
    while (o->running) {
        if (!poll()) break;
        const bool anim = o->ui.animating();
        if (o->need_frame || anim) {
            o->self_repaint = false;
            timespec t0{}; clock_gettime(CLOCK_MONOTONIC, &t0);
            const uint64_t s0 = uint64_t(t0.tv_sec) * 1000000u + uint64_t(t0.tv_nsec / 1000);
            render();
            // 常驻动画节流到 anim_fps（同 Windows/X11），空闲则让出 CPU。
            if (anim || o->self_repaint) {
                timespec t1{}; clock_gettime(CLOCK_MONOTONIC, &t1);
                const uint64_t s1 = uint64_t(t1.tv_sec) * 1000000u + uint64_t(t1.tv_nsec / 1000);
                const uint64_t el_us = s1 - s0;
                const uint64_t budget_us = uint64_t(o->anim_budget_ms) * 1000u;
                if (el_us < budget_us) usleep(useconds_t(budget_us - el_us));
            } else {
                usleep(2000);   // 静态帧：小睡一下，别空转
            }
        } else {
            usleep(2000);
        }
    }
    o->frame_cb_ptr = nullptr;
}

inline void Window::set_title(const std::string& title) {
    detail::CocoaState* s = reinterpret_cast<detail::CocoaState*>(impl->native);
    if (!s || !s->window) return;
    CFStringRef t = CFStringCreateWithCString(kCFAllocatorDefault, title.c_str(), kCFStringEncodingUTF8);
    objc_msgSend(s->window, sel_registerName("setTitle:"), t);
    CFRelease(t);
}
inline void Window::set_size(int w, int h) {
    detail::CocoaState* s = reinterpret_cast<detail::CocoaState*>(impl->native);
    if (s && s->window) objc_msgSend(s->window, sel_registerName("setContentSize:"), CGSizeMake(CGFloat(w), CGFloat(h)));
}
inline void Window::close() { if (impl) impl->running = false; }
inline void Window::set_clipboard(const std::string&) {}
inline std::string Window::clipboard() const { return std::string(); }

#endif // AINO_PLATFORM_MACOS

} // namespace aino

#endif // AINO_IMPLEMENTED
#endif // AINO_IMPLEMENTATION
