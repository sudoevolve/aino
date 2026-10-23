// hover 淡入淡出 —— 中间帧不许比两端更暗（"hover 就莫名其妙闪一下"的根因）
//
// 问题：ghost / outline 这类**透明底**控件，底色是 Color::none() = (0,0,0,0)。
// 用 `base.mix(hover, t)` 去淡入，RGB 和 alpha 会被**一起**从黑色拉上来：
//     t=0.5 时得到 (122,122,122, a=128)
// 光栅器再拿它去 blend，中间帧是 244*0.25 + 249*0.75 ≈ 185 —— 比背景(249)
// 和终态(244)都暗得多。于是每次 hover 都先"闪一下深灰"再回到几乎看不见的
// accent。用户看到的就是"hover 莫名其妙的闪"，而且只有带 hover 动画的控件
// （按钮 / 导航行 / 下拉项）有这毛病，静态表格没有。
//
// 正确做法：透明底淡入只推 alpha，RGB 始终等于目标色。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

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
    // 逐通道读回（缓冲是 BGRA）
    aino::Color at(int x, int y) const {
        const uint8_t* p = cv.px + size_t(y) * size_t(cv.stride) + size_t(x) * 4;
        return aino::Color(p[2], p[1], p[0], p[3]);
    }
};

struct Harness {
    aino::Ui ui;
    aino::detail::GdiTextRenderer tr;
    Surface* surf = nullptr;

    explicit Harness(Surface* s) : surf(s) {
        tr.init(1.0f, std::string(), std::string());
        ui.impl->text = &tr;
        ui.impl->canvas = &surf->cv;
        ui.set_theme(aino::Theme::light());
    }
    ~Harness() { tr.release(); }

    void frame(float mx, float my, int w, int h, const std::function<void()>& draw) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        ui.impl->input.mouse = aino::Vec2(mx, my);
        draw();
        ui.end_frame();
    }
};

static float luma(const aino::Color& c) { return (float(c.r) + float(c.g) + float(c.b)) / 3.0f; }

// 一帧一帧地量：返回每帧采样点的亮度
static std::vector<float> fade_curve(Surface& s, Harness& h,
                                     const std::function<void()>& draw,
                                     float from_x, float from_y,
                                     float to_x, float to_y,
                                     int x, int y, int frames) {
    const int w = s.w, hh = s.h;
    std::vector<float> out;
    // 起点：先在别处停留足够久，保证动画已经收敛
    for (int i = 0; i < 30; ++i) h.frame(from_x, from_y, w, hh, draw);
    for (int i = 0; i < frames; ++i) {
        h.frame(to_x, to_y, w, hh, draw);
        out.push_back(luma(s.at(x, y)));
    }
    return out;
}

// 序列里"有没有比最终值更暗的中间帧"。settled 是最后一帧。
static bool no_dip(const std::vector<float>& v, float floor_luma, std::string* detail) {
    float worst = 1e9f;
    for (float f : v) if (f < worst) worst = f;
    if (worst < floor_luma - 2.0f) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "最低 %.1f < 下限 %.1f（曲线 %.0f→%.0f）",
                      double(worst), double(floor_luma), double(v.front()), double(v.back()));
        *detail = buf;
        return false;
    }
    return true;
}

static bool monotonic(const std::vector<float>& v, bool down, std::string* detail) {
    for (size_t i = 1; i < v.size(); ++i) {
        const float d = v[i] - v[i - 1];
        if (down ? (d > 2.0f) : (d < -2.0f)) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "第 %zu 帧回弹 %.1f→%.1f", i, double(v[i - 1]), double(v[i]));
            *detail = buf;
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------- 1. 单位：state_fill
static void test_state_fill() {
    std::printf("[1] state_fill 在透明底色上只推 alpha\n");
    const aino::Color accent = aino::Color::hex(0xF4F4F5);
    const aino::Color press  = aino::detail::press_color(accent);

    const aino::Color half = aino::detail::state_fill(aino::Color::none(), accent, press, 0.5f, 0.0f);
    check(half.r == accent.r && half.g == accent.g && half.b == accent.b,
          "RGB 保持目标色（不被拉黑）",
          "得到 (" + std::to_string(half.r) + "," + std::to_string(half.g) + "," +
              std::to_string(half.b) + ")");
    check(half.a >= 125 && half.a <= 130, "alpha 走到一半",
          "a=" + std::to_string(half.a));

    const aino::Color full = aino::detail::state_fill(aino::Color::none(), accent, press, 1.0f, 0.0f);
    check(full.r == accent.r && full.b == accent.b && full.a == 255, "t=1 就是 accent 本身",
          "(" + std::to_string(full.r) + "," + std::to_string(full.a) + ")");

    const aino::Color zero = aino::detail::state_fill(aino::Color::none(), accent, press, 0.0f, 0.0f);
    check(zero.a == 0, "t=0 完全透明", "a=" + std::to_string(zero.a));

    // 不透明底色的行为不能变：淡入仍然是两端之间插值
    const aino::Color sec = aino::Color::hex(0xF4F4F5);
    const aino::Color m = aino::detail::state_fill(sec, aino::detail::hover_color(sec),
                                                   aino::detail::press_color(sec), 0.5f, 0.0f);
    check(m.a == 255 && m.r < sec.r && m.r > 200, "不透明底照旧插值",
          "r=" + std::to_string(m.r) + " a=" + std::to_string(m.a));
}

// ---------------------------------------------------------------- 2. 按钮：淡入 / 淡出
static void test_button_fade() {
    std::printf("[2] ghost 按钮的 hover 淡入淡出没有暗闪\n");
    Surface s(260, 160);
    Harness h(&s);
    auto draw = [&]() {
        h.ui.clear(aino::Color::hex(0xF9F9FA));       // 照抄 gallery 侧边栏底色
        h.ui.button("", aino::Rect(20.0f, 20.0f, 200.0f, 40.0f), aino::Style().ghost());
    };
    const int x = 120, y = 40;

    std::string d;
    const std::vector<float> fade_in = fade_curve(s, h, draw, 120.0f, 130.0f, 120.0f, 40.0f, x, y, 40);
    const float settled = fade_in.back();
    check(fade_in.front() > settled + 2.0f, "起点确实比终态亮（真的在淡入）",
          "起点 " + std::to_string(int(fade_in.front())));
    check(no_dip(fade_in, settled, &d), "淡入无暗闪", d);
    check(monotonic(fade_in, true, &d), "淡入单调", d);

    const std::vector<float> fade_out = fade_curve(s, h, draw, 120.0f, 40.0f, 120.0f, 130.0f, x, y, 40);
    check(fade_out.back() > fade_out.front() + 2.0f, "淡出回到背景色",
          std::to_string(int(fade_out.front())) + "→" + std::to_string(int(fade_out.back())));
    check(no_dip(fade_out, fade_out.front(), &d), "淡出无暗闪", d);
    check(monotonic(fade_out, false, &d), "淡出单调", d);
}

// ---------------------------------------------------------------- 3. 导航行（走的是另一套底色）
static void test_nav_item_fade() {
    std::printf("[3] nav_item 的 hover 淡入没有暗闪\n");
    Surface s(260, 160);
    Harness h(&s);
    auto draw = [&]() {
        h.ui.clear(aino::Color::hex(0xF9F9FA));
        h.ui.nav_item("item", false, aino::Rect(10.0f, 20.0f, 220.0f, 30.0f));
    };
    const int x = 150, y = 35;
    std::string d;
    const std::vector<float> v = fade_curve(s, h, draw, 150.0f, 130.0f, 150.0f, 35.0f, x, y, 40);
    check(v.front() > v.back() + 1.0f, "起点比终态亮",
          std::to_string(int(v.front())) + "→" + std::to_string(int(v.back())));
    check(no_dip(v, v.back(), &d), "淡入无暗闪", d);
}

// ---------------------------------------------------------------- 4. outline 按钮（描边 + 透明底）
static void test_outline_fade() {
    std::printf("[4] outline 按钮的 hover 淡入没有暗闪\n");
    Surface s(260, 160);
    Harness h(&s);
    auto draw = [&]() {
        h.ui.clear(aino::Color::hex(0xF9F9FA));
        h.ui.button("", aino::Rect(20.0f, 20.0f, 200.0f, 40.0f), aino::Style().outline());
    };
    const int x = 120, y = 40;
    std::string d;
    const std::vector<float> v = fade_curve(s, h, draw, 120.0f, 130.0f, 120.0f, 40.0f, x, y, 40);
    check(no_dip(v, v.back(), &d), "淡入无暗闪", d);
}

int main() {
    test_state_fill();
    test_button_fade();
    test_nav_item_fade();
    test_outline_fade();
    std::printf("通过 %d 项，失败 %d 项\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
