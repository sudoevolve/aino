// =============================================================================
//  新增组件的【无窗口数值探针】
//
//  思路：aino 是立即模式，Ui::impl 是公开的，所以可以完全绕开窗口：
//    · begin_frame(w, h, dt, scale) 初始化帧状态；
//    · 直接往 impl->input 里填鼠标/键盘（pressed/released/text_input/key_pressed），
//      就能像真用户一样驱动组件；
//    · 断言组件改动的状态（*value / impl->caret / impl->flags）或画布像素。
//  比"截图看一眼"强得多：没有 DPI / 焦点 / 真光标干扰，可重复、可进 CI。
//
//  编译：cl /std:c++17 /EHsc /O2 /utf-8 newcomp_probe.cpp
// =============================================================================
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using aino::Color;
using aino::Rect;
using aino::Style;
using aino::Ui;
using aino::Vec2;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* what) {
    if (ok) { ++g_pass; }
    else { ++g_fail; std::printf("  FAIL  %s\n", what); }
}
static bool approx(float a, float b, float eps = 0.02f) { return std::fabs(a - b) <= eps; }

static const float W = 640.0f, H = 480.0f;

// -----------------------------------------------------------------------------
//  迷你驱动台
// -----------------------------------------------------------------------------
struct Harness {
    Ui ui;
    std::vector<uint8_t> buf;
    aino::Canvas cv;

    Harness() {
        buf.assign(size_t(W) * size_t(H) * 4u, 0);
        cv.px = buf.data();
        cv.w = int(W);
        cv.h = int(H);
        cv.stride = int(W) * 4;
        ui.begin_frame(W, H, 1.0f / 60.0f, 1.0f);
        ui.impl->canvas = &cv;       // 给光栅化用；impl->text 保持 null（不依赖字体）
        ui.end_frame();
    }

    void at(float x, float y) { ui.impl->input.mouse = Vec2(x, y); }
    void down(int b = 0) { ui.impl->input.down[b] = true;  ui.impl->input.pressed[b] = true; }
    void up(int b = 0)   { ui.impl->input.down[b] = false; ui.impl->input.released[b] = true; }
    void key(int k)      { ui.impl->input.key_pressed[k] = true; }
    void type(const std::string& s) { ui.impl->input.text_input = s; }
    void wheel(float d)  { ui.impl->input.wheel = d; }

    // 取某点像素（物理坐标 == 逻辑坐标，scale=1）
    Color px(int x, int y) const {
        const uint8_t* p = buf.data() + size_t(y) * size_t(cv.stride) + size_t(x) * 4u;
        return Color(p[aino::detail::CH_R], p[aino::detail::CH_G], p[aino::detail::CH_B], p[aino::detail::CH_A]);
    }
    bool px_is(int x, int y, const Color& c) const {
        const Color g = px(x, y);
        return std::abs(int(g.r) - int(c.r)) <= 3 && std::abs(int(g.g) - int(c.g)) <= 3
            && std::abs(int(g.b) - int(c.b)) <= 3;
    }
    // 铺一整块不透明底色。**测半透明 token 必须先铺底**：canvas 初值是黑，
    // 而 "10% 的黑" 叠在纯黑上还是纯黑 —— 直接比较 RGB 会永远为真（假通过）。
    void fill_all(const Color& c) {
        for (int y = 0; y < int(H); ++y)
            for (int x = 0; x < int(W); ++x) {
                uint8_t* q = buf.data() + size_t(y) * size_t(cv.stride) + size_t(x) * 4u;
                q[aino::detail::CH_R] = c.r;
                q[aino::detail::CH_G] = c.g;
                q[aino::detail::CH_B] = c.b; q[3] = 255;
            }
    }
    void blank(int x0, int y0, int w, int h) {
        for (int y = y0; y < y0 + h; ++y)
            for (int x = x0; x < x0 + w; ++x) {
                uint8_t* p = buf.data() + size_t(y) * size_t(cv.stride) + size_t(x) * 4u;
                p[0] = p[1] = p[2] = 0; p[3] = 255;
            }
    }
    void begin() { ui.begin_frame(W, H, 1.0f / 60.0f, 1.0f); }
    void end()   { ui.end_frame(); }

    // 一帧：把 draw 的结果带出来
    template <class F> bool step(F&& draw) { begin(); const bool r = draw(); end(); return r; }

    // 一次完整点击（按下帧 + 抬起帧），返回抬起帧的返回值
    template <class F> bool click(float x, float y, F&& draw) {
        at(x, y); down(0); step(draw);
        at(x, y); up(0);   return step(draw);
    }
};

// UI 状态是跨帧存活的（anim/flags/focus），所以每个用例用独立的 Harness。

int main() {
    const aino::Theme& LT = aino::Theme::light();

    // -------------------------------------------------------------------------
    std::printf("[T1] radio_group\n");
    {
        Harness h;
        int idx = 0;
        const std::vector<std::string> items = { "a", "b", "c" };
        const Rect box(10, 10, 120, 66);          // 3 行 × 22
        auto draw = [&]() { return h.ui.radio_group(items, &idx, box, Style()); };
        h.step(draw);
        check(idx == 0, "初始 index 不动");
        check(h.click(20, 10 + 22 * 2 + 11, draw), "点第 3 行返回 changed");
        check(idx == 2, "index 变成 2");
        check(!h.click(20, 10 + 22 * 2 + 11, draw), "再点同一行不返回 changed");
        check(idx == 2, "index 保持 2");
        check(!h.step(draw), "空闲帧不返回 changed");
    }

    // -------------------------------------------------------------------------
    std::printf("[T2] stepper\n");
    {
        Harness h;
        float v = 5.0f;
        const Rect box(10, 10, 140, 34);
        // NSStepper 的几何：右边缘 **20 宽竖条**，上半 = ＋、下半 = －。
        // 旧的"[−] 值 [+]"横向三段是 Web 的 Number Input，已经改掉了。
        const float bx = box.right() - 10.0f;
        const float up_y = box.y + box.h * 0.25f;
        const float dn_y = box.y + box.h * 0.75f;
        auto draw = [&]() { return h.ui.stepper(&v, 1.0f, 0.0f, 6.0f, box, Style()); };
        h.step(draw);
        check(approx(v, 5.0f), "初始值不变");
        check(h.click(bx, up_y, draw), "点 + 返回 changed");
        check(approx(v, 6.0f), "+ 之后 6");
        check(!h.click(bx, up_y, draw), "到上限后再点 + 不算 changed");
        check(approx(v, 6.0f), "上限被夹住");
        for (int i = 0; i < 6; ++i) h.click(bx, dn_y, draw);
        check(approx(v, 0.0f), "连点 6 次 − 到 0");
        h.click(bx, dn_y, draw);
        check(approx(v, 0.0f), "下限被夹住");
    }

    // -------------------------------------------------------------------------
    std::printf("[T3] rating（含 raster_poly 星形）\n");
    {
        Harness h;
        int stars = 0;
        const Rect box(10, 10, 120, 24);          // size = min(120-16)/5, 24) = 20.8
        const float size = std::min((120.0f - 4.0f * 4.0f) / 5.0f, 24.0f);
        const float cx0 = 10.0f + size * 0.5f;
        auto draw = [&]() { return h.ui.rating(&stars, 5, box, Style()); };
        h.step(draw);
        check(h.click(cx0 + (size + 4.0f) * 2.0f, 22.0f, draw), "点第 3 颗星返回 changed");
        check(stars == 3, "stars == 3");
        check(h.click(cx0 + (size + 4.0f) * 2.0f, 22.0f, draw), "再点第 3 颗星返回 changed");
        check(stars == 0, "再点当前值 → 清零");

        // 星形填充的像素断言：第 1 颗星中心应被填成 primary（5 颗星时 stars=0
        // 是 muted，所以先把值设成 5 再画）
        stars = 5;
        h.blank(0, 0, int(W), int(H));
        for (int i = 0; i < 40; ++i) h.step(draw);   // 等动画收敛到 1.0
        const Color primary = LT.primary;
        check(h.px_is(int(cx0), 22, primary), "星心被填成 primary");
        // 星形的四个"凹角"（外接圆角落）不应该被填
        const int corner_x = int(cx0 + size * 0.48f);
        const int corner_y = int(22.0f - size * 0.46f);
        const Color c = h.px(corner_x, corner_y);
        check(!(std::abs(int(c.r) - int(primary.r)) <= 3 && std::abs(int(c.g) - int(primary.g)) <= 3),
              "星形凹角是空的（说明不是把方框填了）");
    }

    // -------------------------------------------------------------------------
    std::printf("[T4] textarea 多行编辑\n");
    {
        Harness h;
        std::string v = "abc\ndef";
        const Rect box(0, 0, 200, 100);
        const float lh = std::max(h.ui.line_height(14.0f), 14.0f * 1.15f);
        auto draw = [&]() { return h.ui.textarea(&v, box, Style()); };

        h.step(draw);
        // 点第 2 行行首（padx=10, pady=8）
        h.click(12.0f, 8.0f + lh * 1.5f, draw);
        check(h.ui.impl->focus != nullptr, "点击后拿到焦点");
        check(h.ui.impl->caret == 4, "光标落在第 2 行行首（码点 4）");

        h.at(12.0f, 8.0f + lh * 0.5f);
        h.type("X");
        check(h.step(draw), "输入字符返回 changed");
        check(v == "abc\nXdef", "字符插到第 2 行行首");
        check(h.ui.impl->caret == 5, "光标前进 1");

        h.key(aino::KEY_BACKSPACE);
        h.step(draw);
        check(v == "abc\ndef", "退格删掉刚输入的 X");

        h.key(aino::KEY_ENTER);
        h.step(draw);
        check(v == "abc\n\ndef", "Enter 插入换行");

        // 此时 v = "abc\n\ndef"，光标在码点 5 = 第 3 行（索引 2）行首
        check(h.ui.impl->caret == 5, "Enter 后光标在新空行的下一行行首");
        h.key(aino::KEY_HOME);
        h.step(draw);
        check(h.ui.impl->caret == 5, "Home：已在行首则不动");
        h.key(aino::KEY_END);
        h.step(draw);
        check(h.ui.impl->caret == 8, "End：跳到第 3 行行尾（码点 8）");
        h.key(aino::KEY_UP);
        h.step(draw);
        check(h.ui.impl->caret == 4, "↑ 到中间那个空行（码点 4）");
        h.key(aino::KEY_DOWN);
        h.step(draw);
        check(h.ui.impl->caret == 5, "↓ 回到第 3 行行首");
        h.key(aino::KEY_DELETE);
        h.step(draw);
        check(v == "abc\n\nef", "Delete 删掉第 3 行第一个字符");
        check(h.ui.impl->caret == 5, "Delete 后光标不动");

        // 选区 + 覆盖输入：Ctrl+A 之后打字应整段替换
        h.ui.impl->sel_anchor = 0;
        h.ui.impl->caret = int(v.size());
        h.type("z");
        h.step(draw);
        check(v == "z", "有选区时输入直接替换整段");
        check(h.ui.impl->caret == 1, "替换后光标在末尾");
        check(h.ui.impl->sel_anchor == -1, "替换后选区清空");
    }

    // -------------------------------------------------------------------------
    std::printf("[T5] accordion\n");
    {
        Harness h;
        bool open = false;
        const Rect box(10, 10, 200, 38);
        auto draw = [&]() { return h.ui.accordion("标题", &open, box, Style()); };
        h.step(draw);
        check(!open, "默认收起");
        check(h.click(60.0f, 29.0f, draw), "点标题返回 changed");
        check(open, "展开");
        check(h.click(60.0f, 29.0f, draw), "再点返回 changed");
        check(!open, "收起");
        check(h.click(195.0f, 29.0f, draw), "点右侧箭头区域也算命中标题行");
        check(open, "箭头区域点击后展开");
    }

    // -------------------------------------------------------------------------
    std::printf("[T6] menu 交互在帧内、绘制在帧末\n");
    {
        Harness h;
        int picked = -1;
        const std::vector<std::string> items = { "m0", "m1", "m2" };
        const Rect box(10, 10, 100, 34);
        auto draw = [&]() { return h.ui.menu("菜单", items, &picked, box, Style()); };
        const uint64_t id = aino::detail::make_id("menu", box);

        h.step(draw);
        check(h.ui.impl->flags[id] < 0.5f, "初始关闭");
        h.click(60.0f, 27.0f, draw);
        check(h.ui.impl->flags[id] > 0.5f, "点触发按钮 → 展开");

        // 弹层位置：list 在按钮下方 4px，条目高 32，左右各 4px 内缩
        const float item_h = 32.0f;
        const float iy = box.bottom() + 4.0f + 4.0f + item_h * 1.0f + item_h * 0.5f;
        h.at(60.0f, iy);
        const bool chosen = h.click(60.0f, iy, draw);
        check(chosen, "选中第 2 项时同一帧就返回 true");
        check(picked == 1, "clicked_index == 1");
        check(h.ui.impl->flags[id] < 0.5f, "选中后自动收起");
    }

    // -------------------------------------------------------------------------
    std::printf("[T7] dialog\n");
    {
        Harness h;
        bool open = true;                    // 已打开
        const Rect box = Rect::centered(W * 0.5f, H * 0.5f, 300.0f, 180.0f);
        h.step([&]() { return h.ui.dialog("t", "b", &open, box, Style()); });
        check(open, "点在框外之前保持打开");
        // 点面板外面 → 关闭
        h.click(20.0f, 20.0f, [&]() { return h.ui.dialog("t", "b", &open, box, Style()); });
        check(!open, "点遮罩 → 关闭");
        // Esc 关闭
        open = true;
        h.step([&]() { return h.ui.dialog("t", "b", &open, box, Style()); });
        h.at(500.0f, 500.0f);
        h.key(aino::KEY_ESC);
        h.step([&]() { return h.ui.dialog("t", "b", &open, box, Style()); });
        check(!open, "Esc → 关闭");
        // macOS 的 alert 没有右上角 ×（关闭靠按钮 / Esc）。所以这里反过来钉住：
        // 点面板内部的空白**不能**把它关掉 —— 以前那个 × 的位置就在右上角，
        // 一旦有人把叉加回来，这条会立刻红。
        open = true;
        h.step([&]() { return h.ui.dialog("t", "b", &open, box, Style()); });
        h.click(box.right() - 30.0f, box.y + 26.0f,
                [&]() { return h.ui.dialog("t", "b", &open, box, Style()); });
        check(open, "面板右上角没有关闭按钮（点它不关）");
        // 关闭后不再绘制
        open = false;
        check(!h.step([&]() { return h.ui.dialog("t", "b", &open, box, Style()); }), "关闭后返回 false");
    }

    // -------------------------------------------------------------------------
    // 悬停 0.4 s 才出浮层（对齐网页端 delayDuration）。划过一排按钮时如果
    // 立即弹出，鼠标扫过去会一路连环闪，所以这里断言三件事：延时内不登记、
    // 延时内主动请求重绘（否则静止不动就永远等不到）、越界后才登记。
    std::printf("[T8] tooltip 悬停延时\n");
    {
        Harness h;
        int repaints = 0;
        h.ui.impl->request_repaint = [&repaints]() { ++repaints; };
        const Rect anchor(50, 50, 80, 30);
        auto tip = [&]() { h.ui.tooltip(anchor, "hi"); return h.ui.impl->defer.size(); };

        h.at(10, 10);
        h.begin(); const size_t n_out = tip(); h.end();
        check(n_out == 0, "鼠标在外 → 不登记弹层");

        h.at(90, 65);
        h.begin(); repaints = 0; const size_t n_first = tip(); h.end();
        check(n_first == 0, "刚进入的第一帧不登记（延时未到）");
        check(repaints == 1, "延时窗口内请求重绘，时钟才会往前走");

        // 30 帧 × 1/60 s = 0.50 s，越过 0.40 s 门槛
        for (int i = 0; i < 30; ++i) { h.begin(); tip(); h.end(); }
        h.begin(); const size_t n_in = tip(); h.end();
        check(n_in == 1, "悬停超过 0.4 s → 登记 1 个弹层");

        // 鼠标离开要重新计时：回来那一帧不能立刻就弹
        h.at(10, 10);
        h.begin(); tip(); h.end();
        h.at(90, 65);
        h.begin(); const size_t n_again = tip(); h.end();
        check(n_again == 0, "离开再回来 → 重新计时");

        h.begin(); const size_t n_empty = (h.ui.tooltip(anchor, ""), h.ui.impl->defer.size()); h.end();
        check(n_empty == 0, "空文本不登记");
    }

    // -------------------------------------------------------------------------
    std::printf("[T9] 滚动容器\n");
    {
        Harness h;
        const Rect box(0, 0, 200, 100);
        const float content_h = 400.0f;
        h.step([&]() { h.ui.begin_scroll(box, content_h); h.ui.end_scroll(); return true; });
        check(approx(h.ui.scroll_y(), 0.0f), "初始偏移 0");
        h.at(100, 50);
        h.wheel(-2.0f);                       // 向下滚 2 格 × 44
        h.step([&]() { h.ui.begin_scroll(box, content_h); h.ui.end_scroll(); return true; });
        check(approx(h.ui.scroll_y(), 88.0f), "滚 2 格 → 88");
        h.at(100, 50);
        h.wheel(-100.0f);
        h.step([&]() { h.ui.begin_scroll(box, content_h); h.ui.end_scroll(); return true; });
        check(approx(h.ui.scroll_y(), 300.0f), "夹到 max = content_h - box.h");
        h.at(600, 400);                       // 鼠标在容器外
        h.wheel(5.0f);
        h.step([&]() { h.ui.begin_scroll(box, content_h); h.ui.end_scroll(); return true; });
        check(approx(h.ui.scroll_y(), 300.0f), "容器外滚轮无效");
        // group 与 clip 都要能正确弹出/弹回
        check(h.ui.impl->group.size() == 1, "end_scroll 弹回 group");
        check(h.ui.impl->clips.empty(), "end_scroll 弹回 clip");
        check(h.ui.impl->scroll_stack.empty(), "end_scroll 弹回上下文栈");
        // 内容比视口矮 → 不允许滚
        h.at(100, 50); h.wheel(-5.0f);
        h.step([&]() { h.ui.begin_scroll(box, 60.0f); h.ui.end_scroll(); return true; });
        check(approx(h.ui.scroll_y(), 0.0f), "内容更矮时不滚");
    }

    // -------------------------------------------------------------------------
    std::printf("[T10] begin_scroll 里的 row() 真的吃到偏移\n");
    {
        Harness h;
        const Rect box(0, 0, 200, 100);
        float first_y = -1.0f;
        auto draw = [&]() {
            h.ui.begin_scroll(box, 400.0f);
            for (int i = 0; i < 6; ++i) {
                const Rect r = h.ui.row(50.0f);
                if (i == 0) first_y = r.y;
            }
            h.ui.end_scroll();
            return true;
        };
        h.step(draw);
        check(approx(first_y, 0.0f), "未滚动时第一行从 0 开始");
        h.at(100, 50); h.wheel(-1.0f);        // 下滚 44
        h.step(draw);
        check(approx(first_y, -44.0f), "滚动后第一行上移到 -44");
    }

    // -------------------------------------------------------------------------
    std::printf("[T11] bars / sparkline 像素\n");
    {
        Harness h;
        h.blank(0, 0, int(W), int(H));
        const std::vector<float> vals = { 1.0f, 0.5f };
        const Rect box(0, 0, 20, 100);
        h.step([&]() { h.ui.bars(vals, box, Style()); return true; });
        const Color primary = LT.primary;
        // bw = (20 - gap*1)/2，gap = max(2, 20*0.02) = 2 → bw = 9：
        // 第 0 柱 x∈[0,9] 值 1.0 满高；第 1 柱 x∈[11,20] 值 0.5 只占下半
        check(h.px_is(4, 98, primary), "满高柱的底部有色");
        check(h.px_is(4, 2, primary), "满高柱顶到框顶");
        check(h.px_is(15, 75, primary), "半高柱下半有色");
        check(!h.px_is(15, 10, primary), "半高柱上半是空的");

        h.blank(0, 0, int(W), int(H));
        const std::vector<float> line = { 0.0f, 1.0f, 0.0f };
        const Rect lb(0, 0, 100, 50);
        h.step([&]() { h.ui.sparkline(line, lb, Style()); return true; });
        // 折线经过 (0, bottom) → (50, top) → (100, bottom)，中点附近必有笔画
        bool found = false;
        for (int y = 0; y < 50 && !found; ++y)
            for (int x = 0; x < 100; ++x)
                if (h.px_is(x, y, primary)) { found = true; break; }
        check(found, "sparkline 画出了折线");
        // 面积填充：折线下方（靠近底边、但不在折线上）应有半透明 primary 与黑底混合
        const Color under = h.px(25, 45);
        check(under.r > 0 || under.g > 0 || under.b > 0, "折线下方有面积填充");
    }

    // -------------------------------------------------------------------------
    std::printf("[T12] avatar 像素\n");
    {
        Harness h;
        h.fill_all(LT.window_bg);          // 白底：半透明 token 必须叠在底上才可比
        const Rect box(0, 0, 64, 64);
        h.step([&]() { h.ui.avatar("AB", box, Style()); return true; });
        const Color av_bg = LT.fill.over(LT.window_bg);
        check(h.px_is(32, 8, av_bg), "圆内上部是 fill 底色（10% 占位，不是 5%）");
        check(h.px_is(1, 1, LT.window_bg), "圆外角落未被填（是圆形不是方块）");
        // 默认必须有 1px border 描边。理由不是审美：圆底是 muted，一旦落在浅灰容器
        // （gallery 预览板 / card() / panel()）上就跟背景同色，只剩一个孤零零的字。
        // 用户原话"头像咋只有个文字"。轮廓是唯一在任何底色上都能立住的线索。
        {
            const Color edge  = h.px(32, 0);
            const Color inner = h.px(32, 8);
            check(edge.luminance() + 0.02f < inner.luminance(),
                  "圆边缘有 1px border 描边（比圆底暗）");
            check(edge.luminance() + 0.02f < 1.0f, "描边不是纯白（确实画了）");
        }
    }

    // -------------------------------------------------------------------------
    std::printf("[T13] table / breadcrumb / kbd / alert / spinner 不崩且落像素\n");
    {
        Harness h;
        h.blank(0, 0, int(W), int(H));
        const Rect box(0, 0, 200, 140);
        const std::vector<std::string> heads = { "h1", "h2" };
        const std::vector<std::vector<std::string>> rows = { { "a", "b" }, { "c", "d" } };
        h.step([&]() { h.ui.table(heads, rows, box, Style()); return true; });
        check(h.px_is(100, 5, LT.muted), "表头是 muted 底色（无字体时也能断言底色）");
        check(h.px_is(100, 139, LT.border), "底边框是 border 色");

        h.fill_all(LT.window_bg);
        h.step([&]() { h.ui.alert("T", "D", box, Style()); return true; });
        // macOS 没有"整块底色的提示条"：Finder / 系统设置里的警告就是一枚符号 +
        // 两行文字直接贴在版面上。铺底会让它在一层 Group Box 里变成"卡片套卡片"。
        check(h.px_is(100, 70, LT.window_bg), "alert 默认不铺底（与窗口底同色）");
        // 符号是 SF Symbol 那种**描边**（info.circle / exclamationmark.triangle），
        // 不是"饱和色实心圆片里挖白字"—— 后者是 Bootstrap 的 alert 图标。
        auto icon_ink = [&]() {
            int n = 0;
            for (int y = 10; y < 30; ++y)
                for (int x = 11; x < 29; ++x)
                    if (h.px(x, y).luminance() < 0.75f) ++n;
            return n;
        };
        const int ink = icon_ink();
        check(ink > 12, "alert 左上角画出了符号（中性 50%）");
        check(ink < 110, "符号是描边环/三角，不是实心圆片");

        h.fill_all(LT.window_bg);
        Style bad;
        bad.variant = Style::Destructive;
        h.step([&]() { h.ui.alert("T", "D", box, bad); return true; });
        bool red_sym = false;
        for (int y = 10; y < 30 && !red_sym; ++y)
            for (int x = 11; x < 29; ++x) {
                const Color p = h.px(x, y);
                if (p.r > 180 && p.g < 110) { red_sym = true; break; }
            }
        check(red_sym, "Destructive 的符号是系统红（标题不染红）");

        h.blank(0, 0, int(W), int(H));
        const Rect sp(0, 0, 40, 40);
        h.step([&]() { h.ui.spinner(sp, Style()); return true; });
        bool any_track = false;
        for (int y = 0; y < 40 && !any_track; ++y)
            for (int x = 0; x < 40; ++x)
                if (!h.px_is(x, y, Color(0, 0, 0))) { any_track = true; break; }
        check(any_track, "spinner 画了圆环");

        h.blank(0, 0, int(W), int(H));
        h.step([&]() { h.ui.toast("toast", Rect(0, 0, 160, 44), Style()); return true; });
        check(h.px_is(80, 8, LT.popover), "toast 用 popover 底色");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
