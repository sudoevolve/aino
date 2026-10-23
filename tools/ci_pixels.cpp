// =============================================================================
//  ci_pixels.cpp —— 跨平台像素断言（Windows / Linux / macOS 共用）
//
//  跟 tools/verify_gallery.py 的分工：
//    verify_gallery.py  在 Windows 真桌面上用 PrintWindow 抓窗口，断言的是
//                       「整个 gallery 长什么样」（27 项），只能在 Windows 跑；
//    本程序              直接从后备缓冲（Canvas.px）读像素，三个平台都能跑，
//                       断言的是「图元画得对不对」（几何 / 颜色 / 裁剪 / 阴影 / 文字）。
//
//  为什么不直接比对固定截图：三个平台的字形光栅完全不同（GDI 灰度 AA / Xft /
//  CoreText），同一段文字的像素必然不同，逐像素比一定红。所以这里只断言
//  **与字体无关**的性质：
//    · 实心填充的颜色与位置（不经过抗锯齿的内部像素必须精确相等）
//    · 抗锯齿确实产生了"介于两者之间"的过渡像素（否则边缘是硬的，说明没走 AA）
//    · 裁剪区外一个像素都不许动
//    · 阴影峰值不超过传入的 alpha（历史上叠层累积到 148 的老 bug）
//    · 文字画出了墨（能抓"字体加载失败导致全白"这种 Linux 特有故障），
//      且没画到框外
//    · 控件绘制范围 == 传入的 box（抓"DPI 缩放乘了两次"这类偏移 bug）
//
//  用法：
//    Linux    xvfb-run -a ./ci_pixels
//    macOS    ./ci_pixels
//    Windows  ci_pixels.exe
//
//  返回：0 = 全过；1 = 有断言失败；2 = 窗口创建失败
//  环境变量 AINO_PIX_BMP=1 时把最后一个场景打成 ci_frame.bmp（CI 里当 artifact 看）
//
//  作者：sudoevolve <sudoevolve@gmail.com>
//  编译：g++ -std=c++17 -O2 ci_pixels.cpp -o ci_pixels -lX11 -lXft
//        clang++ -std=c++17 -O2 ci_pixels.cpp -o ci_pixels \
//                -framework Cocoa -framework CoreText -framework CoreGraphics
// =============================================================================
#define AINO_IMPLEMENTATION
#include "../aino.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace aino;
// 通道下标定义在 detail 里（Windows 的 DIB 是 BGRA，X11/macOS 是 RGBA），
// 断言一律走这三个常量，不写死偏移。
using aino::detail::CH_A;
using aino::detail::CH_B;
using aino::detail::CH_G;
using aino::detail::CH_R;
// 注意：Window **不能**靠 using 引入 —— Xlib 里 Window 是个类型
// （`typedef XID Window`），和 aino::Window 在全局作用域直接二义。
// 所以下面一律写 aino::Window。

static int g_pass = 0;
static int g_fail = 0;

// -----------------------------------------------------------------------------
//  一帧像素快照
//  直接从 Window 的后备缓冲拷一份出来：三个平台都是 RGBA/BGRA 的 px + stride，
//  通道顺序由库里的 CH_R/CH_G/CH_B 常量统一（Windows 是 BGRA，其余是 RGBA）。
// -----------------------------------------------------------------------------
struct Shot {
    std::vector<uint8_t> px;
    int   w = 0, h = 0, stride = 0;
    float scale = 1.0f;         // 逻辑 → 物理像素倍率（Windows 高 DPI 下是 1.5/2.0）

    // 按**逻辑坐标**取色 —— 断言里写的坐标和 layout 用的是同一套数字
    Color at(float lx, float ly) const {
        int x = int(lx * scale + 0.5f);
        int y = int(ly * scale + 0.5f);
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= w) x = w - 1;
        if (y >= h) y = h - 1;
        const uint8_t* p = &px[size_t(y) * size_t(stride) + size_t(x) * 4u];
        return Color(p[CH_R], p[CH_G], p[CH_B], p[CH_A]);
    }

    // 逻辑矩形 → 物理矩形（左闭右开）
    void to_phys(const Rect& r, int& x0, int& y0, int& x1, int& y1) const {
        x0 = int(r.x * scale + 0.5f);
        y0 = int(r.y * scale + 0.5f);
        x1 = int(r.right() * scale + 0.5f);
        y1 = int(r.bottom() * scale + 0.5f);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > w) x1 = w;
        if (y1 > h) y1 = h;
    }
};

static bool grab(aino::Window& win, Shot& s) {
    Canvas& cv = win.impl->canvas;
    if (!cv.px || cv.w <= 0 || cv.h <= 0 || cv.stride <= 0) return false;
    s.w = cv.w;
    s.h = cv.h;
    s.stride = cv.stride;
    s.scale = win.dpi_scale();
    if (s.scale <= 0.0f) s.scale = 1.0f;
    s.px.resize(size_t(s.stride) * size_t(s.h));
    std::memcpy(s.px.data(), cv.px, s.px.size());
    return true;
}

// 渲染固定帧数后抓图。为什么要跑 2 帧而不是 1 帧：第一帧里 animate_value 之类
// 的插值状态还是初始值，某些组件会请求下一帧；跑两帧拿到的才是"稳定态"。
static bool render_scene(aino::Window& win, const std::function<void(Ui&)>& body, Shot& out) {
    int n = 0;
    win.run([&](Ui& ui) {
        body(ui);
        if (++n >= 2) win.close();
        else          ui.repaint();
    });
    return grab(win, out);
}

// -----------------------------------------------------------------------------
//  断言小工具
// -----------------------------------------------------------------------------
static bool chan_diff(Color a, Color b, int tol) {
    const int dr = std::abs(int(a.r) - int(b.r));
    const int dg = std::abs(int(a.g) - int(b.g));
    const int db = std::abs(int(a.b) - int(b.b));
    return dr > tol || dg > tol || db > tol;
}

static std::string c2s(const Color& c) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "(%d,%d,%d)", int(c.r), int(c.g), int(c.b));
    return std::string(buf);
}

static void pass(const char* name) {
    ++g_pass;
    std::printf("[  ok  ] %s\n", name);
}

static void fail(const char* name, const std::string& why) {
    ++g_fail;
    std::printf("[ FAIL ] %s —— %s\n", name, why.c_str());
}

// 精确比色：只能用在"不经过抗锯齿的内部像素"上
static void want_color(const Shot& s, float lx, float ly, const Color& want, const char* name) {
    const Color got = s.at(lx, ly);
    if (!chan_diff(got, want, 0)) { pass(name); return; }
    char pos[64];
    std::snprintf(pos, sizeof pos, "at(%g,%g) ", double(lx), double(ly));
    fail(name, std::string(pos) + "got " + c2s(got) + " want " + c2s(want));
}

// 逻辑矩形内"与背景不同的像素数"
static int count_diff(const Shot& s, const Rect& r, const Color& bg) {
    int x0, y0, x1, y1;
    s.to_phys(r, x0, y0, x1, y1);
    int n = 0;
    for (int y = y0; y < y1; ++y) {
        const uint8_t* row = &s.px[size_t(y) * size_t(s.stride)];
        for (int x = x0; x < x1; ++x) {
            const uint8_t* p = row + size_t(x) * 4u;
            if (chan_diff(Color(p[CH_R], p[CH_G], p[CH_B], 255), bg, 8)) ++n;
        }
    }
    return n;
}

// 必须是"干净背景"：一个像素都不许被动过
static void want_clean(const Shot& s, const Rect& r, const Color& bg, const char* name) {
    const int n = count_diff(s, r, bg);
    if (n == 0) { pass(name); return; }
    char buf[96];
    std::snprintf(buf, sizeof buf, "该区域本应全是背景 %s，却有 %d 个像素被改过",
                  c2s(bg).c_str(), n);
    fail(name, buf);
}

// 该区域必须画了东西（数量下限），用来抓"根本没画上去"
static void want_ink(const Shot& s, const Rect& r, const Color& bg, int min_px, const char* name) {
    const int n = count_diff(s, r, bg);
    if (n >= min_px) { pass(name); return; }
    char buf[96];
    std::snprintf(buf, sizeof buf, "只画上 %d 个像素（至少期望 %d）", n, min_px);
    fail(name, buf);
}

static void want_true(bool cond, const char* name, const std::string& why) {
    if (cond) pass(name);
    else      fail(name, why);
}

// 某逻辑矩形内最暗的像素亮度（0..255）。阴影断言用它。
static int darkest(const Shot& s, const Rect& r) {
    int x0, y0, x1, y1;
    s.to_phys(r, x0, y0, x1, y1);
    int lo = 255;
    for (int y = y0; y < y1; ++y) {
        const uint8_t* row = &s.px[size_t(y) * size_t(s.stride)];
        for (int x = x0; x < x1; ++x) {
            const uint8_t* p = row + size_t(x) * 4u;
            const int lum = (int(p[CH_R]) * 299 + int(p[CH_G]) * 587 + int(p[CH_B]) * 114) / 1000;
            if (lum < lo) lo = lum;
        }
    }
    return lo;
}

// 数一个矩形里有多少档灰度（8 级一档，跳过背景）。用来区分"灰度抗锯齿"和"1bit 点阵"：
// 点阵字体只会有 1~2 档，Xft / CoreText / GDI 的 ClearType 都会有十几档。
static int text_gray_levels(const Shot& s, const Rect& r, const Color& bg) {
    const int bg_l = (int(bg.r) * 77 + int(bg.g) * 150 + int(bg.b) * 29) >> 8;
    int x0, y0, x1, y1;
    s.to_phys(r, x0, y0, x1, y1);
    bool seen[32] = {};
    int levels = 0;
    for (int y = y0; y < y1; ++y) {
        const uint8_t* row = &s.px[size_t(y) * size_t(s.stride)];
        for (int x = x0; x < x1; ++x) {
            const uint8_t* p = row + size_t(x) * 4u;
            const int l = (int(p[CH_R]) * 77 + int(p[CH_G]) * 150 + int(p[CH_B]) * 29) >> 8;
            if (std::abs(l - bg_l) <= 8) continue;      // 背景，不算墨
            const int b = l >> 3;
            if (!seen[b]) { seen[b] = true; ++levels; }
        }
    }
    return levels;
}

// 文字墨迹的外接框（物理像素），返回 false 表示一个墨点都没有
static bool ink_bbox(const Shot& s, const Rect& r, const Color& bg, int& bx0, int& by0, int& bx1, int& by1) {
    int x0, y0, x1, y1;
    s.to_phys(r, x0, y0, x1, y1);
    bx0 = bx1 = by0 = by1 = -1;
    for (int y = y0; y < y1; ++y) {
        const uint8_t* row = &s.px[size_t(y) * size_t(s.stride)];
        for (int x = x0; x < x1; ++x) {
            const uint8_t* p = row + size_t(x) * 4u;
            if (!chan_diff(Color(p[CH_R], p[CH_G], p[CH_B], 255), bg, 8)) continue;
            if (bx0 < 0) { bx0 = x; by0 = y; }
            bx0 = std::min(bx0, x); bx1 = std::max(bx1, x);
            by0 = std::min(by0, y); by1 = std::max(by1, y);
        }
    }
    return bx0 >= 0;
}

// -----------------------------------------------------------------------------
//  BMP 落盘（给 CI artifact 用，任何看图工具都能开）
// -----------------------------------------------------------------------------
static void write_bmp(const Shot& s, const char* path) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) { std::printf("[pixels] 写不了 %s\n", path); return; }
    const int w = s.w, h = s.h;
    const int pad = (4 - (w * 3) % 4) % 4;
    const int row = w * 3 + pad;
    const uint32_t data = uint32_t(row) * uint32_t(h);
    const uint32_t file = 54u + data;
    const uint32_t off = 54u, bisize = 40u;
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
    for (int y = h - 1; y >= 0; --y) {           // BMP 自下而上
        const uint8_t* src = &s.px[size_t(y) * size_t(s.stride)];
        for (int x = 0; x < w; ++x) {
            line[size_t(x) * 3u + 0u] = src[size_t(x) * 4u + CH_B];
            line[size_t(x) * 3u + 1u] = src[size_t(x) * 4u + CH_G];
            line[size_t(x) * 3u + 2u] = src[size_t(x) * 4u + CH_R];
        }
        std::fwrite(line.data(), 1, line.size(), f);
    }
    std::fclose(f);
    std::printf("[pixels] 已写出 %s（%dx%d）\n", path, w, h);
}

// =============================================================================
//  场景 1：只 clear 的一块画布
//  查的是"整块后备缓冲都被刷成背景色"——能抓到 stride 算错（右侧出现垃圾）、
//  缓冲没清零（_Win32 上新建 DIB 全 0）、部分区域没画（历史上有过"缩放变黑"）。
// =============================================================================
static bool scene_clear() {
    const Theme th = Theme::light();
    const Color bg = th.background;

    aino::Window win(200, 150, "aino pixels: clear");
    if (!win.ok()) { std::printf("[pixels] 窗口创建失败（Linux 上多半是没有 DISPLAY）\n"); return false; }
    win.set_theme(th);

    Shot shot;
    const bool got = render_scene(win, [&](Ui& ui) {
        ui.clear(bg);
    }, shot);
    if (!got) { fail("S1 抓到后备缓冲", "grab 失败（canvas.px 为空）"); return true; }

    // 后备缓冲尺寸必须与逻辑尺寸 × DPI 倍率一致（容差 2px：小数倍率会取整）
    {
        const float want_w = 200.0f * shot.scale;
        const float want_h = 150.0f * shot.scale;
        char buf[128];
        std::snprintf(buf, sizeof buf, "canvas = %dx%d，期望约 %dx%d（scale=%.2f）",
                      shot.w, shot.h, int(want_w + 0.5f), int(want_h + 0.5f), double(shot.scale));
        want_true(std::fabs(float(shot.w) - want_w) <= 2.0f &&
                  std::fabs(float(shot.h) - want_h) <= 2.0f,
                  "S1 后备缓冲尺寸 = 逻辑尺寸 × DPI", buf);
    }

    // 13 x 10 的网格全采样：任何一个点不是背景色都算失败
    {
        int bad = 0;
        Color first_bad;
        float bx = 0.0f, by = 0.0f;
        for (int gy = 0; gy < 10; ++gy) {
            for (int gx = 0; gx < 13; ++gx) {
                // 刻意采到距离右/下边缘 2px 的位置，stride 写错会立刻暴露
                const float lx = 2.0f + float(gx) * 16.0f;
                const float ly = 2.0f + float(gy) * 16.0f;
                const Color c = shot.at(lx, ly);
                if (chan_diff(c, bg, 0)) {
                    if (bad == 0) { first_bad = c; bx = lx; by = ly; }
                    ++bad;
                }
            }
        }
        char buf[160];
        std::snprintf(buf, sizeof buf, "130 个采样点里有 %d 个不是背景：首个 at(%g,%g) = %s，期望 %s",
                      bad, double(bx), double(by), c2s(first_bad).c_str(), c2s(bg).c_str());
        want_true(bad == 0, "S1 整块缓冲都是背景色（130 点）", buf);
    }

    if (std::getenv("AINO_PIX_BMP")) write_bmp(shot, "ci_frame_1_clear.bmp");
    return true;
}

// =============================================================================
//  场景 2：图元全考一遍（浅色主题）
// =============================================================================
static bool scene_primitives() {
    const Theme th = Theme::light();
    const Color bg      = th.background;          // 纯白
    const Color c_red   = Color(214, 40, 40);
    const Color c_blue  = Color(0, 120, 255);
    const Color c_green = Color(30, 180, 90);
    const Color c_yell  = Color(255, 200, 0);

    aino::Window win(420, 380, "aino pixels: primitives");
    if (!win.ok()) { std::printf("[pixels] 窗口创建失败\n"); return false; }
    win.set_theme(th);

    // 文字度量要在帧内取（TextRenderer 是 Window 构造时才建好的）
    float tw_hg = 0.0f;

    Shot shot;
    const bool got = render_scene(win, [&](Ui& ui) {
        ui.clear(bg);

        // ---- 1. 直角实心矩形：内部像素必须**精确**等于填充色 ----
        ui.fill_rect(Rect(20.0f, 20.0f, 80.0f, 40.0f), c_red, 0.0f);

        // ---- 2. 圆角矩形 ----
        ui.fill_rect(Rect(120.0f, 20.0f, 80.0f, 40.0f), c_blue, 14.0f);

        // ---- 3. 圆（圆心与半径都刻意取非整数：让圆周穿在像素中间，
        //        这样才有半覆盖像素可验，否则边缘正好压在像素边界上，
        //        几何正确但也测不到抗锯齿）----
        ui.fill_circle(250.35f, 60.4f, 27.35f, c_green);

        // ---- 4. 描边（不填充）----
        ui.stroke_rect(Rect(20.0f, 90.0f, 100.0f, 40.0f), c_red, 8.0f, 2.0f);

        // ---- 5. 裁剪：画一块比裁剪区大得多的黄色，多出来的部分必须被吃掉 ----
        ui.push_clip(Rect(150.0f, 90.0f, 60.0f, 40.0f));
        ui.fill_rect(Rect(120.0f, 80.0f, 120.0f, 60.0f), c_yell, 0.0f);
        ui.pop_clip();

        // ---- 6. 阴影：alpha=60 的黑影落在纯白上，最暗处应该正好是 195 ----
        ui.drop_shadow(Rect(300.0f, 90.0f, 60.0f, 60.0f), 8.0f, 10.0f, Color(0, 0, 0, 60), 0.0f);

        // ---- 7. 文字：正常一行 ----
        ui.label("Hg", Rect(20.0f, 170.0f, 120.0f, 24.0f));

        // ---- 8. 文字：超长串必须被框裁掉，不许溢出到右边 ----
        ui.label("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
                 Rect(20.0f, 210.0f, 100.0f, 24.0f));

        // ---- 9. 按钮：绘制范围必须等于传入的 box（抓 DPI 缩放乘两次）----
        ui.button("OK", Rect(20.0f, 250.0f, 80.0f, 36.0f));

        tw_hg = ui.text_width("Hg", th.font_size);
    }, shot);
    if (!got) { fail("S2 抓到后备缓冲", "grab 失败"); return true; }

    // ===== 1. 直角矩形 =====
    want_color(shot,  60.0f,  40.0f, c_red, "S2 fill_rect 内部是精确填充色");
    want_color(shot,  12.0f,  40.0f, bg,    "S2 fill_rect 左侧紧邻处仍是背景");
    want_color(shot,  60.0f,  14.0f, bg,    "S2 fill_rect 上方紧邻处仍是背景");

    // ===== 2. 圆角矩形 =====
    want_color(shot, 160.0f,  40.0f, c_blue, "S2 圆角矩形中心是填充色");
    // (123,23) 到圆角弧心 (134,34) 的距离 ≈ 15.6 > r=14 —— 完全在圆角外，必须是背景
    want_color(shot, 123.0f,  23.0f, bg,     "S2 圆角矩形的圆角外是背景（没被方角填满）");

    // ===== 3. 圆 =====
    want_color(shot, 250.0f,  60.0f, c_green, "S2 fill_circle 中心是填充色");
    want_color(shot, 250.0f,  94.0f, bg,      "S2 圆外 6px 处是背景");
    {
        // 圆周边一圈里，必须存在"既不是绿也不是白"的过渡像素。
        // 这是跨平台最好的抗锯齿证据：字形 AA 各平台不同，但几何图形的 AA 必须存在。
        // （只在周长上找，不用管内部：内部本来就是纯色。）
        int blended = 0;
        int x0, y0, x1, y1;
        shot.to_phys(Rect(220.0f, 30.0f, 60.0f, 58.0f), x0, y0, x1, y1);
        for (int y = y0; y < y1; ++y) {
            const uint8_t* row = &shot.px[size_t(y) * size_t(shot.stride)];
            for (int x = x0; x < x1; ++x) {
                const uint8_t* q = row + size_t(x) * 4u;
                const Color c(q[CH_R], q[CH_G], q[CH_B], 255);
                if (chan_diff(c, bg, 3) && chan_diff(c, c_green, 3)) ++blended;
            }
        }
        char buf[128];
        std::snprintf(buf, sizeof buf, "圆周边只找到 %d 个过渡像素（期望 ≥ 8）", blended);
        want_true(blended >= 8, "S2 圆的边缘有抗锯齿过渡色", buf);
    }

    // ===== 4. 描边 =====
    {
        const Color top = shot.at(70.0f, 90.0f);
        char buf[160];
        std::snprintf(buf, sizeof buf, "上边中点 (70,90) = %s，期望红色描边", c2s(top).c_str());
        want_true(top.r > 150 && top.g < 90 && top.b < 90, "S2 stroke_rect 描边画在框线上", buf);
    }
    want_color(shot, 70.0f, 110.0f, bg, "S2 stroke_rect 内部没有被填充");

    // ===== 5. 裁剪 =====
    want_color(shot, 180.0f, 110.0f, c_yell, "S2 裁剪区内正常绘制");
    want_color(shot, 130.0f, 110.0f, bg,     "S2 裁剪区左侧溢出被裁掉");
    want_color(shot, 230.0f, 110.0f, bg,     "S2 裁剪区右侧溢出被裁掉");
    want_color(shot, 180.0f,  84.0f, bg,     "S2 裁剪区上方溢出被裁掉");

    // ===== 6. 阴影 =====
    {
        // SDF 阴影：alpha = A * clamp(1 - d/blur, 0, 1)^2，内部 d=0 → alpha = A = 60。
        // 纯白 255 叠 60/255 的黑 → 255 * (1 - 60/255) ≈ 195。
        // 早期 bug 是叠 N 层把有效 alpha 累加到 148（也就是亮度掉到 107），
        // 所以这里**卡上限**，同时卡下限确认阴影真的画了。
        const int lo = darkest(shot, Rect(315.0f, 105.0f, 30.0f, 30.0f));
        char buf[160];
        std::snprintf(buf, sizeof buf, "阴影最深亮度 %d，期望 195±8（alpha=60 的黑影落在纯白上）", lo);
        want_true(lo >= 187 && lo <= 203, "S2 阴影峰值不超过传入的 alpha", buf);
    }
    want_color(shot, 270.0f, 120.0f, bg, "S2 阴影外 2×blur 处仍是背景");

    // ===== 7~8. 文字 =====
    {
        const int n = count_diff(shot, Rect(20.0f, 170.0f, 120.0f, 24.0f), bg);
        char buf[160];
        std::snprintf(buf, sizeof buf, "文字框里只有 %d 个墨点，text_width(\"Hg\") = %.1f —— "
                      "%s（Linux 上看 core X 字体是否装了：xfonts-base）", n, double(tw_hg),
                      tw_hg <= 0.0f ? "字体根本没量出来，XCreateFontSet 失败或缺字"
                                    : "能量出宽度却没画上，问题在绘制路径");
        want_true(n >= 20, "S2 文字画出了墨（字体加载成功）", buf);
    }
    {
        // 抗锯齿：1bit 点阵字体只有"有墨 / 没墨"两档，Xft（FreeType 灰度）才会
        // 出现大量中间灰。这条专门钉 Ubuntu 上「文字像点阵马赛克、标题和正文一样细」
        // 那个回归 —— 后端悄悄退回 XFontSet 时，上面那条墨迹断言照样绿。
        const int levels = text_gray_levels(shot, Rect(20.0f, 170.0f, 120.0f, 24.0f), bg);
        char buf[192];
#if defined(AINO_HAVE_XFT)
        std::snprintf(buf, sizeof buf,
                      "文字框里有 %d 档灰度；1 档 = 纯 1bit 点阵，≥4 才说明走了灰度抗锯齿",
                      levels);
        want_true(levels >= 4, "S2 文字是灰度抗锯齿渲染（Xft 生效）", buf);
#else
        std::snprintf(buf, sizeof buf,
                      "未编入 Xft（缺 libxft-dev），文字是 1bit 点阵：%d 档灰度属预期", levels);
        want_true(levels >= 1, "S2 文字灰度档位（未编入 Xft，只验有墨）", buf);
#endif
    }
    {
        int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
        const bool any = ink_bbox(shot, Rect(20.0f, 170.0f, 120.0f, 24.0f), bg, bx0, by0, bx1, by1);
        if (!any) {
            fail("S2 文字墨迹在框内", "没有墨迹，跳过边界判断");
        } else {
            const float ink_w = float(bx1 - bx0 + 1) / shot.scale;
            char buf[192];
            std::snprintf(buf, sizeof buf,
                          "墨迹宽 %.1fpx，框宽 120px，text_width 报告 %.1fpx",
                          double(ink_w), double(tw_hg));
            // 墨迹不可能超出框（draw_text 会裁到 clip∩box）；顺带确认"量宽"和"画字"
            // 用的是同一套字体度量 —— 偏差超过 60% 就说明两边对不上。
            const bool ok = ink_w > 0.0f && ink_w <= 120.0f + 2.0f &&
                            tw_hg > 0.0f && std::fabs(ink_w - tw_hg) <= tw_hg * 0.6f;
            want_true(ok, "S2 文字墨迹在框内且与 text_width 一致", buf);
        }
    }
    want_clean(shot, Rect(150.0f, 170.0f, 260.0f, 24.0f), bg, "S2 文字没有溢出到框右侧");
    want_clean(shot, Rect(126.0f, 210.0f, 284.0f, 24.0f), bg, "S2 超长文字被框裁掉，没溢到右边");

    // ===== 9. 按钮 =====
    want_true(chan_diff(shot.at(28.0f, 268.0f), bg, 8),
              "S2 button 在 box 内部画了底色", "box 内 (28,268) 还是背景色");
    want_clean(shot, Rect(6.0f, 258.0f, 10.0f, 20.0f), bg, "S2 button 没有画到 box 左侧外面");
    want_clean(shot, Rect(104.0f, 258.0f, 20.0f, 20.0f), bg, "S2 button 没有画到 box 右侧外面");

    if (std::getenv("AINO_PIX_BMP")) write_bmp(shot, "ci_frame_2_primitives.bmp");
    return true;
}

// =============================================================================
//  场景 3：深色主题 —— 确认切主题后绘制用的是新配色（历史上 set_theme 后
//  组件仍用旧色是常见 bug），同时确认卡片色与背景色是**两个不同的颜色**
//  （深色主题里卡片故意比背景亮一点点，只有这样才能看出层次）。
// =============================================================================
static bool scene_dark() {
    const Theme th = Theme::dark();
    const Color bg   = th.background;   // 0x09090B
    const Color card = th.card;         // 0x101012

    aino::Window win(300, 220, "aino pixels: dark");
    if (!win.ok()) { std::printf("[pixels] 窗口创建失败\n"); return false; }
    win.set_theme(th);

    Shot shot;
    const bool got = render_scene(win, [&](Ui& ui) {
        ui.clear(bg);
        ui.panel(Rect(20.0f, 20.0f, 160.0f, 100.0f));
    }, shot);
    if (!got) { fail("S3 抓到后备缓冲", "grab 失败"); return true; }

    want_color(shot, 10.0f,  10.0f, bg,   "S3 深色主题背景色正确");
    want_color(shot, 100.0f, 70.0f, card, "S3 panel 中心是卡片色");
    want_true(chan_diff(bg, card, 0), "S3 深色主题里卡片色 ≠ 背景色", "两者相同，看不出层次");
    want_clean(shot, Rect(200.0f, 20.0f, 80.0f, 100.0f), bg, "S3 panel 没有画到 box 右侧外面");

    if (std::getenv("AINO_PIX_BMP")) write_bmp(shot, "ci_frame_3_dark.bmp");
    return true;
}

int main() {
    std::printf("=== aino ci_pixels ===\n");

    if (!scene_clear())       return 2;
    if (!scene_primitives())  return 2;
    if (!scene_dark())        return 2;

    std::printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
