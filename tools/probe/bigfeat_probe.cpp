// 大块文本 / 虚拟滚动 / 编辑器撤销 / 动画 / 变换 —— 数值与像素断言
//
// 全部离屏（自建 32bpp DIB + 内存 DC），不开窗口、不抢焦点、秒级完成。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>

int g_pass = 0, g_fail = 0;

static void check(bool ok, const char* what, const std::string& detail = std::string()) {
    if (ok) { ++g_pass; }
    else    { ++g_fail; std::printf("  [FAIL] %s  %s\n", what, detail.c_str()); }
}

// ---------------------------------------------------------------- 离屏画布
struct Surface {
    HBITMAP bmp = nullptr;
    HDC dc = nullptr;
    void* bits = nullptr;
    aino::detail::Canvas cv;
    int w = 0, h = 0;

    Surface(int W, int H) : w(W), h(H) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        dc = CreateCompatibleDC(nullptr);
        SelectObject(dc, bmp);
        cv.px = (uint8_t*)bits;
        cv.w = W; cv.h = H; cv.stride = W * 4;
        cv.platform_dc = dc;
    }
    ~Surface() {
        if (dc) DeleteDC(dc);
        if (bmp) DeleteObject(bmp);
    }
    void clear(uint8_t r = 255, uint8_t g = 255, uint8_t b = 255) {
        std::memset(bits, 255, size_t(w) * size_t(h) * 4);
        uint8_t* p = (uint8_t*)bits;
        for (int i = 0; i < w * h; ++i) { p[i * 4 + 0] = b; p[i * 4 + 1] = g; p[i * 4 + 2] = r; p[i * 4 + 3] = 255; }
    }
    // BGRA
    void px(int x, int y, int* r, int* g, int* b) const {
        if (x < 0 || y < 0 || x >= w || y >= h) { *r = *g = *b = -1; return; }
        const uint8_t* p = (uint8_t*)bits + (size_t(y) * size_t(w) + size_t(x)) * 4;
        *b = p[0]; *g = p[1]; *r = p[2];
    }
    // 非白像素的包围盒
    bool ink_box(int* x0, int* y0, int* x1, int* y1, int thr = 250) const {
        *x0 = w; *y0 = h; *x1 = -1; *y1 = -1;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int r, g, b; px(x, y, &r, &g, &b);
                if (r < thr || g < thr || b < thr) {
                    if (x < *x0) *x0 = x;
                    if (y < *y0) *y0 = y;
                    if (x > *x1) *x1 = x;
                    if (y > *y1) *y1 = y;
                }
            }
        return *x1 >= 0;
    }
};

static aino::Ui* g_ui = nullptr;

struct Harness {
    aino::Ui ui;
    aino::detail::GdiTextRenderer tr;
    Surface* surf = nullptr;

    explicit Harness(Surface* s) : surf(s) {
        tr.init(1.0f, std::string(), std::string());
        ui.impl->text = &tr;
        ui.impl->canvas = &surf->cv;
        ui.set_theme(aino::Theme::light());
        ui.begin_frame(float(surf->w), float(surf->h), 1.0f / 60.0f, 1.0f);
    }
    ~Harness() { tr.release(); }
};

static std::string make_cn(size_t bytes) {
    std::string s;
    s.reserve(bytes + 8);
    const char* unit = "快速布局器把一段很长的中文文本切开，逐字测量宽度之后决定在哪里换行。";
    while (s.size() < bytes) s += unit;
    while (s.size() > bytes) s.pop_back();
    // 保证不切坏 UTF-8
    while (!s.empty() && (uint8_t(s.back()) & 0xC0) == 0x80) s.pop_back();
    return s;
}

// ---------------------------------------------------------------- 1. 折行缓存
static void test_wrap_cache() {
    std::printf("[1] 折行缓存\n");
    Surface s(400, 300);
    Harness h(&s);
    aino::Ui& ui = h.ui;

    std::vector<std::string> direct, cached1, cached2;
    size_t mismatches = 0;
    const char* samples[] = {
        "hello world this is a fairly long english sentence that has to wrap somewhere",
        "短",
        "中文换行测试：首先第一行应该到边界就断开，然后第二行继续，最后一行收尾。",
        "mixed 中英混排 text with spaces 还有中文 and punctuation, like this: a.b.c",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",   // 超长单词
        "第一行\n第二行\n\n第四行",
        "",
    };
    const float widths[] = { 37.0f, 120.0f, 260.0f, 900.0f };
    for (const char* t : samples) {
        const std::string text(t);
        for (float w : widths) {
            direct.clear();
            aino::detail::layout_wrapped_lines(ui.impl->text, text, w, 14.0f, 400, true, direct, nullptr);
            const std::vector<std::string>& c1 = aino::detail::wrap_layout(ui.impl, text, w, 14.0f, 400, true);
            const std::vector<std::string>& c2 = aino::detail::wrap_layout(ui.impl, text, w, 14.0f, 400, true);
            cached1.assign(c1.begin(), c1.end());
            cached2.assign(c2.begin(), c2.end());
            if (cached1 != direct) ++mismatches;
            if (cached2 != direct) ++mismatches;
        }
    }
    char d[128];
    std::snprintf(d, sizeof(d), "(%zu 处不一致 / 共 %zu 组)", mismatches, sizeof(samples) / sizeof(samples[0]) * 4);
    check(mismatches == 0, "缓存命中后与直接折行逐行一致", d);

    // 复用地址但内容变化：不能返回旧结果
    {
        std::string a = "AAAA BBBB CCCC DDDD";
        aino::detail::wrap_layout(ui.impl, a, 40.0f, 14.0f, 400, true);
        std::string b = "1111 2222 3333 4444";      // 同长度、不同内容
        const std::vector<std::string>& lb = aino::detail::wrap_layout(ui.impl, b, 40.0f, 14.0f, 400, true);
        check(!lb.empty() && lb[0].compare(0, 4, "1111") == 0, "同长度不同内容不会串缓存",
              lb.empty() ? "(空)" : ("(" + lb[0] + ")"));
    }

    // 内存上限：塞 40 段 20 万字的文本，缓存字节数不能无限涨
    {
        const size_t before = ui.impl->wrap_bytes;
        std::vector<std::string> docs;               // 各自独立的缓冲，地址不会复用
        docs.reserve(40);
        for (int i = 0; i < 40; ++i) {
            docs.push_back(make_cn(200000));
            docs.back()[0] = char('A' + (i % 26));
            aino::detail::wrap_layout(ui.impl, docs.back(), 600.0f, 14.0f, 400, true);
        }
        std::snprintf(d, sizeof(d), "(缓存 %.1f MB，淘汰 %zu 段)",
                      double(ui.impl->wrap_bytes) / 1048576.0, ui.impl->wrap_evictions);
        check(ui.impl->wrap_bytes <= size_t(8u << 20), "缓存有内存上限，不会无限增长", d);
        check(ui.impl->wrap_evictions > 0, "超出上限时会淘汰旧段", d);
        (void)before;
    }
}

// ---------------------------------------------------------------- 2. 大文本性能
static void test_wrap_perf() {
    std::printf("[2] 大文本性能（20 万字）\n");
    Surface s(900, 600);
    Harness h(&s);
    aino::Ui& ui = h.ui;

    std::string doc = make_cn(200000);
    const float w = 700.0f;

    auto now = [] { return std::chrono::high_resolution_clock::now(); };
    auto us = [](std::chrono::high_resolution_clock::time_point a,
                 std::chrono::high_resolution_clock::time_point b) {
        return std::chrono::duration<double, std::micro>(b - a).count();
    };

    // 首次（cold：还要建单字宽度缓存）
    auto t0 = now();
    const std::vector<std::string>& l0 = aino::detail::wrap_layout(ui.impl, doc, w, 14.0f, 400, true);
    auto t1 = now();
    const double cold = us(t0, t1);
    const size_t n = l0.size();

    // 再来一帧（命中缓存）
    double warm = 1e9, warm_sum = 0.0;
    for (int i = 0; i < 20; ++i) {
        auto a = now();
        const std::vector<std::string>& l = aino::detail::wrap_layout(ui.impl, doc, w, 14.0f, 400, true);
        auto b = now();
        const double d = us(a, b);
        warm = d < warm ? d : warm;
        warm_sum += d;
        (void)l;
    }

    char d[192];
    std::snprintf(d, sizeof(d), "(首次 %.2f ms / 命中 %.3f µs 均值，%zu 行)",
                  cold / 1000.0, warm_sum / 20.0, n);
    check(warm < 60.0, "缓存命中的折行 < 60µs", d);
    check(cold > 0.0 && warm_sum / 20.0 < cold * 0.05, "命中比首次重排快 20 倍以上", d);

    // 直接折行（不缓存）做对照
    std::vector<std::string> raw;
    auto a = now();
    aino::detail::layout_wrapped_lines(ui.impl->text, doc, w, 14.0f, 400, true, raw, nullptr);
    auto b = now();
    std::snprintf(d, sizeof(d), "(不缓存 %.2f ms → 命中 %.3f µs)",
                  us(a, b) / 1000.0, warm_sum / 20.0);
    check(us(a, b) / 1000.0 > 1.0, "不缓存时确实要毫秒级（说明优化有意义）", d);
    std::printf("     %s\n", d);
}

// ---------------------------------------------------------------- 3. 虚拟滚动
static void test_vscroll() {
    std::printf("[3] 虚拟滚动\n");
    Surface s(600, 400);
    Harness h(&s);
    aino::Ui& ui = h.ui;

    const aino::Rect box(20.0f, 20.0f, 300.0f, 200.0f);
    const float item_h = 20.0f;
    const int count = 1000000;

    int drawn = 0;
    drawn = ui.scroll(box, count, item_h, [&](int, const aino::Rect&) { ++drawn; });
    // 上面 lambda 里的 drawn 自增被 scroll_view 的返回值重复计数，这里用返回值为准
    int first = 0, last = -1;
    ui.scroll_visible(box, count, item_h, &first, &last);
    char d[160];
    std::snprintf(d, sizeof(d), "(可见 %d..%d，绘制 %d 项，总数 %d)", first, last, drawn, count);
    check(last - first + 1 <= 12, "只绘制一屏的量（≤12 项）", d);
    check(last < 50, "首个可见项在视口内（没有全量遍历）", d);

    // 滚轮：把鼠标放进视口，滚 3 格
    ui.impl->input.mouse = aino::Vec2(box.cx(), box.cy());
    ui.impl->input.wheel = -3.0f;
    int f2 = 0, l2 = -1;
    const float off = ui.scroll_visible(box, count, item_h, &f2, &l2);
    ui.impl->input.wheel = 0.0f;
    std::snprintf(d, sizeof(d), "(滚 3 格后 offset=%.1f，首个可见 %d)", off, f2);
    check(off > 100.0f && f2 > 5, "滚轮生效且可见区间跟着走", d);

    // 跳转
    ui.scroll_to_index(box, 500000, item_h, count);
    int f3 = 0, l3 = -1;
    const float off3 = ui.scroll_visible(box, count, item_h, &f3, &l3);
    std::snprintf(d, sizeof(d), "(跳到 500000 项后 offset=%.0f，可见 %d..%d)", off3, f3, l3);
    check(f3 <= 500000 && l3 >= 500000, "scroll_to_index 把目标项放进视口", d);

    // 统一性：通用滚动区与虚拟列表**共用同一份偏移**（同一个 box = 同一个滚动区）。
    // 这条是 0.2.1 合并两套 API 的核心不变量：以前它们各存一份，同一个 box
    // 切个模式就"位置丢了"，看着像两个东西。
    {
        const aino::Rect same(20.0f, 240.0f, 300.0f, 70.0f);
        ui.begin_scroll(same, 900.0f);
        ui.scroll_to(137.0f);
        ui.end_scroll();
        int fs = 0, ls = -1;
        const float shared = ui.scroll_visible(same, 30, 30.0f, &fs, &ls);
        std::snprintf(d, sizeof(d), "(通用区 scroll_to(137) → 虚拟模式读到 %.1f)", shared);
        check(std::fabs(shared - 137.0f) < 0.01f, "通用与虚拟共用同一份偏移", d);
        std::snprintf(d, sizeof(d), "(scroll_offset(%.0f) / 内部 %.1f)", ui.scroll_offset(same), shared);
        check(std::fabs(ui.scroll_offset(same) - shared) < 0.01f, "scroll_offset 读到同一份", d);
    }

    // 内容不足一屏时不应该有滚动条 / 偏移
    ui.impl->input.wheel = -5.0f;
    const float off4 = ui.scroll_visible(box, 3, item_h, &f3, &l3);
    ui.impl->input.wheel = 0.0f;
    std::snprintf(d, sizeof(d), "(3 项时 offset=%.1f)", off4);
    check(off4 == 0.0f, "内容不足一屏时偏移恒为 0", d);
}

// ---------------------------------------------------------------- 4. 编辑器撤销
static void test_undo() {
    std::printf("[4] 编辑器撤销 / 重做\n");
    Surface s(400, 200);
    Harness h(&s);
    aino::Ui& ui = h.ui;
    aino::Ui::Impl* im = ui.impl;

    const uint64_t id = 0xABCDEFull;
    std::string text = "hi";
    int caret = 2, anchor = -1;

    // 连续打 3 个字（同一组）
    im->clock = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const std::string before = text;
        const int c0 = caret, a0 = anchor;
        text.push_back(char('x' + i));
        caret = int(text.size());
        im->clock += 0.1f;
        aino::detail::edit_snapshot(im, id, before, c0, a0, aino::detail::EDIT_TYPE);
    }
    char d[128];
    std::snprintf(d, sizeof(d), "(栈深 %zu，应为 1)", im->edit_stack[id].undo.size());
    check(im->edit_stack[id].undo.size() == 1, "0.7s 内的连续输入合并成一步", d);

    // 停顿后再打一个字 → 新的一步
    im->clock += 1.2f;
    {
        const std::string before = text;
        text.push_back('!');
        caret = int(text.size());
        aino::detail::edit_snapshot(im, id, before, caret, -1, aino::detail::EDIT_TYPE);
    }
    check(im->edit_stack[id].undo.size() == 2, "停顿之后另起一步",
          "(栈深 " + std::to_string(im->edit_stack[id].undo.size()) + ")");

    const std::string full = text;
    bool ok1 = aino::detail::edit_undo(im, id, &text, &caret, &anchor);
    std::snprintf(d, sizeof(d), "(撤销后 \"%s\"，期望 \"%s\")", text.c_str(), "hixyz");
    check(ok1 && text == "hixyz", "第一次撤销退掉最后一步", d);

    bool ok2 = aino::detail::edit_undo(im, id, &text, &caret, &anchor);
    std::snprintf(d, sizeof(d), "(撤销后 \"%s\"，期望 \"hi\")", text.c_str());
    check(ok2 && text == "hi", "第二次撤销退掉整组输入", d);
    check(caret == 2, "撤销后光标回到当时的位置", "(caret=" + std::to_string(caret) + ")");

    bool ok3 = aino::detail::edit_undo(im, id, &text, &caret, &anchor);
    check(!ok3, "到底了再撤销返回 false（不崩）");

    bool ok4 = aino::detail::edit_redo(im, id, &text, &caret, &anchor);
    check(ok4 && text == "hixyz", "重做恢复第一次撤销前的内容", "(\"" + text + "\")");
    bool ok5 = aino::detail::edit_redo(im, id, &text, &caret, &anchor);
    check(ok5 && text == full, "重做到底", "(\"" + text + "\")");

    // 撤销后再输入 → redo 栈必须清空（否则重做会把新内容覆盖掉）
    aino::detail::edit_undo(im, id, &text, &caret, &anchor);
    im->clock += 2.0f;
    {
        const std::string before = text;
        text += "Z";
        caret = int(text.size());
        aino::detail::edit_snapshot(im, id, before, caret, -1, aino::detail::EDIT_OTHER);
    }
    check(im->edit_stack[id].redo.empty(), "撤销后新输入会清空 redo 栈");

    // 按词跳转
    {
        const std::vector<uint32_t> cps = aino::utf8_codepoints("hello world foo_bar 中文测试");
        const int a = aino::detail::word_right(cps, 0);
        const int b = aino::detail::word_right(cps, 5);
        const int c = aino::detail::word_left(cps, 11);
        std::snprintf(d, sizeof(d), "(右跳=%d 再右跳=%d 左跳=%d)", a, b, c);
        check(a == 5 && b == 6 && c == 6, "Ctrl+←/→ 按词边界跳", d);
        int wa = 0, wb = 0;
        aino::detail::word_span(cps, 15, &wa, &wb);
        std::snprintf(d, sizeof(d), "(双击选中 [%d,%d))", wa, wb);
        check(wa == 12 && wb == 19, "双击选词只选中 foo_bar 这一段", d);
    }
}

// ---------------------------------------------------------------- 5. 动画
static void test_anim() {
    std::printf("[5] 动画\n");
    Surface s(200, 200);
    Harness h(&s);
    aino::Ui& ui = h.ui;

    const uint64_t id = 7;
    float v0 = ui.tween(id, 100.0f, 0.2f);            // 首次调用直接落到目标
    check(v0 == 100.0f, "首帧不动画（避免进场闪一下）", "(v=" + std::to_string(v0) + ")");

    ui.impl->dt = 1.0f / 60.0f;
    float last = v0;
    bool monotonic = true;
    for (int i = 0; i < 30; ++i) {
        const float v = ui.tween(id, 0.0f, 0.2f, aino::easing::linear);
        if (v > last + 0.001f) monotonic = false;
        last = v;
    }
    char d[128];
    std::snprintf(d, sizeof(d), "(30 帧 0.5s 后 %.3f)", double(last));
    check(last == 0.0f, "0.2s 的补间在 0.5s 后精确落到目标", d);
    check(monotonic, "线性缓动单调下降", d);

    // 缓动端点
    check(aino::easing::out_cubic(0.0f) == 0.0f && aino::easing::out_cubic(1.0f) == 1.0f,
          "out_cubic 端点精确");
    check(aino::easing::out_back(1.0f) == 1.0f, "out_back 终点精确（过冲只在中间）");
    check(aino::easing::out_back(0.6f) > 1.0f, "out_back 中段确实过冲");

    // 中途改目标：从当前值继续，不跳变
    ui.impl->dt = 1.0f / 60.0f;
    ui.tween(99, 0.0f, 0.2f);
    float mid = 0.0f;
    for (int i = 0; i < 6; ++i) mid = ui.tween(99, 100.0f, 0.2f, aino::easing::linear);
    const float cont = ui.tween(99, 200.0f, 0.2f, aino::easing::linear);
    std::snprintf(d, sizeof(d), "(转向时 %.3f → %.3f)", double(mid), double(cont));
    check(cont > mid && cont - mid <= 13.0f, "目标中途改变时从当前显示值续跑（不跳）", d);

    // 到达目标后不再要帧 —— 空闲 0% CPU 的前提
    {
        int frames = 0;
        ui.impl->request_repaint = [&] { ++frames; };
        ui.impl->dt = 1.0f / 60.0f;
        ui.tween(4242, 0.0f, 0.1f);
        for (int i = 0; i < 20; ++i) ui.tween(4242, 100.0f, 0.1f, aino::easing::linear);
        const int during = frames;
        frames = 0;
        for (int i = 0; i < 20; ++i) ui.tween(4242, 100.0f, 0.1f, aino::easing::linear);
        char dd[128];
        std::snprintf(dd, sizeof(dd), "(运行中 %d 次请求，静止后 %d 次)", during, frames);
        check(during > 0 && frames == 0, "补间跑完就不再请求重绘（回到 0 帧）", dd);

        // 颜色补间同样会停
        frames = 0;
        ui.tween_color(77, aino::Color::hex(0x000000), 0.1f);          // 先落一个初值
        for (int i = 0; i < 20; ++i) ui.tween_color(77, aino::Color::hex(0x123456), 0.1f);
        const int c1 = frames;
        frames = 0;
        for (int i = 0; i < 10; ++i) ui.tween_color(77, aino::Color::hex(0x123456), 0.1f);
        std::snprintf(dd, sizeof(dd), "(运行中 %d 次，静止后 %d 次)", c1, frames);
        check(c1 > 0 && frames == 0, "颜色补间跑完也会停", dd);
        ui.impl->request_repaint = nullptr;
    }

    // smooth：帧率无关
    Surface s2(100, 100);
    aino::Ui a, b;
    a.impl->text = &h.tr; a.impl->canvas = &s2.cv;
    b.impl->text = &h.tr; b.impl->canvas = &s2.cv;
    a.begin_frame(100, 100, 1.0f / 60.0f, 1.0f);
    b.begin_frame(100, 100, 1.0f / 30.0f, 1.0f);
    float va = 0, vb = 0;
    for (int i = 0; i < 60; ++i) va = a.smooth(va, 100.0f, 14.0f);
    for (int i = 0; i < 30; ++i) vb = b.smooth(vb, 100.0f, 14.0f);
    std::snprintf(d, sizeof(d), "(60fps=%.3f 30fps=%.3f)", double(va), double(vb));
    check(std::fabs(va - vb) < 1.0f, "smooth 在 60fps / 30fps 下落点一致", d);
}

// ---------------------------------------------------------------- 6. 变换
static void test_transform() {
    std::printf("[6] 变换（平移/缩放/旋转）\n");
    Surface s(300, 300);
    Harness h(&s);
    aino::Ui& ui = h.ui;
    const aino::Color red = aino::Color::hex(0xE11D48);

    // 6.1 平移：矩形整体右移 40
    {
        s.clear();
        ui.fill_rect(aino::Rect(50, 50, 40, 30), red, 0.0f);
        int x0, y0, x1, y1;
        s.ink_box(&x0, &y0, &x1, &y1);
        const int base_x0 = x0;

        s.clear();
        aino::Transform t;
        t.translate = aino::Vec2(40.0f, 25.0f);
        ui.push_transform(t);
        ui.fill_rect(aino::Rect(50, 50, 40, 30), red, 0.0f);
        ui.pop_transform();
        s.ink_box(&x0, &y0, &x1, &y1);
        char d[128];
        std::snprintf(d, sizeof(d), "(x0 %d→%d, y0 %d, 期望 +40/+25)", base_x0, x0, y0);
        check(x0 == base_x0 + 40 && y0 == 75, "平移精确落位", d);
    }

    // 6.2 缩放：绕中心放大 2 倍，尺寸翻倍、中心不动
    {
        s.clear();
        ui.fill_rect(aino::Rect(100, 100, 40, 30), red, 0.0f);
        int ax0, ay0, ax1, ay1;
        s.ink_box(&ax0, &ay0, &ax1, &ay1);

        s.clear();
        aino::Transform t;
        t.scale = 2.0f;
        t.origin = aino::Vec2(120.0f, 115.0f);       // 矩形中心
        ui.push_transform(t);
        ui.fill_rect(aino::Rect(100, 100, 40, 30), red, 0.0f);
        ui.pop_transform();
        int bx0, by0, bx1, by1;
        s.ink_box(&bx0, &by0, &bx1, &by1);
        const int aw = ax1 - ax0 + 1, ah = ay1 - ay0 + 1;
        const int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
        const double acx = (ax0 + ax1) * 0.5, bcx = (bx0 + bx1) * 0.5;
        char d[160];
        std::snprintf(d, sizeof(d), "(尺寸 %dx%d → %dx%d，中心 x %.1f → %.1f)", aw, ah, bw, bh, acx, bcx);
        check(bw == aw * 2 && bh == ah * 2, "2 倍缩放尺寸正确", d);
        check(std::fabs(acx - bcx) < 1.0, "绕 origin 缩放，中心不动", d);
    }

    // 6.3 旋转 90°：宽高互换
    {
        s.clear();
        ui.fill_rect(aino::Rect(120, 100, 60, 20), red, 0.0f);
        int ax0, ay0, ax1, ay1;
        s.ink_box(&ax0, &ay0, &ax1, &ay1);

        s.clear();
        aino::Transform t;
        t.rotate = 90.0f;
        t.origin = aino::Vec2(150.0f, 110.0f);       // 矩形中心
        ui.push_transform(t);
        ui.fill_rect(aino::Rect(120, 100, 60, 20), red, 0.0f);
        ui.pop_transform();
        int bx0, by0, bx1, by1;
        s.ink_box(&bx0, &by0, &bx1, &by1);
        const int aw = ax1 - ax0 + 1, ah = ay1 - ay0 + 1;
        const int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
        const double acx = (ax0 + ax1) * 0.5, bcx = (bx0 + bx1) * 0.5;
        const double acy = (ay0 + ay1) * 0.5, bcy = (by0 + by1) * 0.5;
        char d[192];
        std::snprintf(d, sizeof(d), "(尺寸 %dx%d → %dx%d，中心 (%.1f,%.1f)→(%.1f,%.1f))",
                      aw, ah, bw, bh, acx, acy, bcx, bcy);
        check(std::abs(bw - ah) <= 2 && std::abs(bh - aw) <= 2, "旋转 90° 后宽高互换", d);
        check(std::fabs(acx - bcx) < 1.5 && std::fabs(acy - bcy) < 1.5, "旋转围绕中心，位置不跑", d);
    }

    // 6.4 旋转后的像素覆盖率：不能有空洞（多边形填充是否真的填满）
    {
        s.clear();
        aino::Transform t;
        t.rotate = 30.0f;
        t.origin = aino::Vec2(150.0f, 150.0f);
        ui.push_transform(t);
        ui.fill_rect(aino::Rect(100, 100, 100, 100), red, 12.0f);
        ui.pop_transform();
        // 中心必须是纯红
        int r, g, b;
        s.px(150, 150, &r, &g, &b);
        char d[128];
        std::snprintf(d, sizeof(d), "(中心 RGB=%d,%d,%d)", r, g, b);
        check(r == 0xE1 && g == 0x1D && b == 0x48, "旋转 30° 的圆角矩形内部是实心的", d);
        // 面积应接近原来的 100x100
        int cnt = 0;
        for (int y = 0; y < s.h; ++y)
            for (int x = 0; x < s.w; ++x) {
                s.px(x, y, &r, &g, &b);
                if (r < 250 || g < 250 || b < 250) ++cnt;
            }
        std::snprintf(d, sizeof(d), "(着色 %d px，期望 ≈10000)", cnt);
        check(cnt > 9000 && cnt < 11000, "旋转后面积不变（圆角没被吃掉）", d);
    }

    // 6.5 旋转的文字：墨迹包围盒应是竖长条
    {
        s.clear();
        ui.draw_text("AAAA", aino::Rect(100, 140, 100, 20), aino::Color::hex(0x000000));
        int ax0, ay0, ax1, ay1;
        s.ink_box(&ax0, &ay0, &ax1, &ay1);
        const int aw = ax1 - ax0 + 1, ah = ay1 - ay0 + 1;

        s.clear();
        aino::Transform t;
        t.rotate = 90.0f;
        t.origin = aino::Vec2(150.0f, 150.0f);
        ui.push_transform(t);
        ui.draw_text("AAAA", aino::Rect(100, 140, 100, 20), aino::Color::hex(0x000000));
        ui.pop_transform();
        int bx0, by0, bx1, by1;
        s.ink_box(&bx0, &by0, &bx1, &by1);
        const int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
        char d[192];
        std::snprintf(d, sizeof(d), "(墨迹 %dx%d → %dx%d)", aw, ah, bw, bh);
        check(std::abs(bw - ah) <= 4 && std::abs(bh - aw) <= 4, "文字旋转 90° 后宽高互换（真的转了）", d);

        // 中心要落在原点（矩形中心旋转后仍在原处）
        const double acx = (ax0 + ax1) * 0.5, acy = (ay0 + ay1) * 0.5;
        const double bcx = (bx0 + bx1) * 0.5, bcy = (by0 + by1) * 0.5;
        // 左对齐的文字墨迹中心不在盒子中心，所以按同一个旋转式子推期望值：
        //   p' = origin + R(90°) * (p - origin)，屏幕坐标顺时针
        const double ex = 150.0 - (acy - 150.0);
        const double ey = 150.0 + (acx - 150.0);
        std::snprintf(d, sizeof(d), "(中心 (%.1f,%.1f)→(%.1f,%.1f)，期望 (%.1f,%.1f))",
                      acx, acy, bcx, bcy, ex, ey);
        check(std::fabs(bcx - ex) < 2.0 && std::fabs(bcy - ey) < 2.0,
              "旋转文字围绕 origin，中心不漂", d);
    }

    // 6.6 嵌套：外层平移 + 内层缩放
    {
        s.clear();
        aino::Transform outer;
        outer.translate = aino::Vec2(30.0f, 0.0f);
        aino::Transform inner;
        inner.scale = 2.0f;
        inner.origin = aino::Vec2(50.0f, 50.0f);
        ui.push_transform(outer);
        ui.push_transform(inner);
        ui.fill_rect(aino::Rect(40, 40, 20, 20), red, 0.0f);
        ui.pop_transform();
        ui.pop_transform();
        int x0, y0, x1, y1;
        s.ink_box(&x0, &y0, &x1, &y1);
        char d[160];
        std::snprintf(d, sizeof(d), "(bmp %d..%d x %d..%d，期望 60..100)", x0, x1, y0, y1);
        check(std::abs(x0 - 60) <= 1 && std::abs(x1 - 99) <= 1, "嵌套变换按父→子顺序复合", d);
    }

    // 6.7 pop 之后必须完全复原
    {
        s.clear();
        aino::Transform t;
        t.translate = aino::Vec2(77.0f, 33.0f);
        t.rotate = 45.0f;
        t.scale = 1.7f;
        ui.push_transform(t);
        ui.pop_transform();
        ui.fill_rect(aino::Rect(10, 10, 20, 20), red, 0.0f);
        int x0, y0, x1, y1;
        s.ink_box(&x0, &y0, &x1, &y1);
        char d[128];
        std::snprintf(d, sizeof(d), "(bmp %d..%d x %d..%d，期望 10..29)", x0, x1, y0, y1);
        check(x0 == 10 && x1 == 29 && y0 == 10, "pop 之后变换彻底复原", d);
        check(!ui.transformed(), "pop 之后 transformed() == false");
    }
}

int main() {
    test_wrap_cache();
    test_wrap_perf();
    test_vscroll();
    test_undo();
    test_anim();
    test_transform();
    std::printf("\n通过 %d 项，失败 %d 项\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
