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
//  编译：g++ -std=c++17 -O2 ci_pixels.cpp -o ci_pixels -lX11 -ldl
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

#if defined(AINO_PLATFORM_X11)
#include <cerrno>
#include <dirent.h>          // 只为失败诊断数一下打开的 fd
#endif

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

// 窗口建不起来时，光说一句"创建失败"没法查（Linux 上可能是 DISPLAY 没了、
// 也可能是 fd 被前面场景打开的字体吃光）。第一次踩到时 CI 日志里只有一行
// "窗口创建失败"，白跑了两轮。
static void win_fail(const char* tag) {
    std::printf("[pixels] %s 窗口创建失败\n", tag);
#if defined(AINO_PLATFORM_X11)
    int nfd = 0;
    if (DIR* d = ::opendir("/proc/self/fd")) {
        while (::readdir(d)) ++nfd;
        ::closedir(d);
    }
    const char* disp = std::getenv("DISPLAY");
    std::printf("[pixels]   DISPLAY=%s 打开的 fd=%d errno=%d\n",
                disp ? disp : "(null)", nfd, errno);
#endif
}

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
    if (!win.ok()) { win_fail("S1"); return false; }
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
    if (!win.ok()) { win_fail("S2"); return false; }
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

        // ---- 10. 中文：要靠 fontconfig 的逐字符兜底才画得出来 ----
        ui.label("中国字", Rect(20.0f, 290.0f, 160.0f, 24.0f));

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

    // ===== 中文 =====
    {
        // 三条一起看：① 有墨；② 宽度 ≈ 3 个全角字；③ 中间三分之一列里有墨。
        // .notdef 的豆腐方块是个**空心矩形**，中间列除了上下两条边什么都没有 ——
        // 所以第 ③ 条才是「中文全是方框」这个回归的真正钉子。
        int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
        const Rect box(20.0f, 290.0f, 160.0f, 24.0f);
        if (!ink_bbox(shot, box, bg, bx0, by0, bx1, by1)) {
            fail("S2 中文画出了墨（CJK 兜底字体）", "文字框里一个墨点都没有");
        } else {
            char buf[192];
            const float ink_w = float(bx1 - bx0 + 1) / shot.scale;
            std::snprintf(buf, sizeof buf,
                          "中文墨迹宽 %.1fpx（字号 %.1f，期望约 3 个全角字）",
                          double(ink_w), double(th.font_size));
            want_true(ink_w >= th.font_size * 2.0f, "S2 中文墨迹宽度像 3 个全角字", buf);

            const int mid0 = bx0 + (bx1 - bx0 + 1) / 3;
            const int mid1 = bx0 + (bx1 - bx0 + 1) * 2 / 3;
            int rows = 0, hit = 0;
            for (int y = by0; y <= by1; ++y) {
                bool any = false, mid = false;
                for (int x = bx0; x <= bx1; ++x) {
                    const uint8_t* p = &shot.px[size_t(y) * size_t(shot.stride) + size_t(x) * 4u];
                    if (!chan_diff(Color(p[CH_R], p[CH_G], p[CH_B], 255), bg, 8)) continue;
                    any = true;
                    if (x >= mid0 && x < mid1) mid = true;
                }
                if (any) { ++rows; if (mid) ++hit; }
            }
            std::snprintf(buf, sizeof buf,
                          "%d 个有墨的行里只有 %d 行在中间三分之一列有墨；"
                          "方块字形（.notdef）只在左右边框有墨", rows, hit);
            want_true(rows > 0 && float(hit) >= float(rows) * 0.45f,
                      "S2 中文是字形而不是 tofu 方块", buf);
        }
    }

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
        // 抗锯齿：1bit 点阵字体只有"有墨 / 没墨"两档，FreeType 灰度才会
        // 出现大量中间灰。这条专门钉 Ubuntu 上「文字像点阵马赛克、标题和正文一样细」
        // 那个回归 —— 后端悄悄退回 XFontSet 时，上面那条墨迹断言照样绿。
        const int levels = text_gray_levels(shot, Rect(20.0f, 170.0f, 120.0f, 24.0f), bg);
        char buf[192];
#if defined(AINO_PLATFORM_X11)
        std::snprintf(buf, sizeof buf,
                      "文字框里有 %d 档灰度；1 档 = 纯 1bit 点阵，≥4 才说明走了灰度抗锯齿"
                      "（freetype 是运行时 dlopen 的，加载不到就退回点阵）",
                      levels);
        want_true(levels >= 4, "S2 文字是灰度抗锯齿渲染（FreeType 运行时加载成功）", buf);
#else
        std::snprintf(buf, sizeof buf,
                      "非 X11 平台：文字框里有 %d 档灰度", levels);
        want_true(levels >= 1, "S2 文字灰度档位", buf);
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
    if (!win.ok()) { win_fail("S3"); return false; }
    win.set_theme(th);

    Shot shot;
    const bool got = render_scene(win, [&](Ui& ui) {
        ui.clear(bg);
        ui.panel(Rect(20.0f, 20.0f, 160.0f, 100.0f));
    }, shot);
    if (!got) { fail("S3 抓到后备缓冲", "grab 失败"); return true; }

    want_color(shot, 10.0f,  10.0f, bg,   "S3 深色主题背景色正确");
    // panel 现在是 macOS Group Box 语义：**不铺卡片色**，只叠 1.5% 的白
    // （Figma Group Box = r12 + #00000008 再乘 50%）。所以断言不能写死成
    // card（0x252525）—— 那每调一次底价就假红一次。这里直接按 token 算出
    // "背景 + fill5" 的期望值，跟着主题走。
    // panel 现在是 Fills - Opaque/Tertiary（5%）：kit 的 Group Box 裸值
    // （3%@50% ≈ 1.5%）在白/深底上都看不见，分组容器取系统 grouped 背景那一档。
    const Color panel_fill = th.fill3.over(bg);
    want_color(shot, 100.0f, 70.0f, panel_fill, "S3 panel 中心是 背景+5% 叠加（Group Box 不铺卡片色）");
    want_true(chan_diff(bg, card, 0), "S3 深色主题里卡片色 ≠ 背景色", "两者相同，看不出层次");
    want_clean(shot, Rect(200.0f, 20.0f, 80.0f, 100.0f), bg, "S3 panel 没有画到 box 右侧外面");

    if (std::getenv("AINO_PIX_BMP")) write_bmp(shot, "ci_frame_3_dark.bmp");
    return true;
}

// =============================================================================
//  场景 4：显式指定一个**没有汉字**的字体族，汉字仍然必须画得出来
//
//  这是 0.4.6 在用户机器上的原样复现：「中文全消失了，但代码块里的中文还有」。
//  根因是兜底链以前只取 FcFontSort 的**前 16 条**，而排序表里同一个家族的每个
//  style 各占一条（DejaVu Sans 一家就能吃掉 Book / Bold / Oblique / Condensed
//  七八条），CJK 字体被挤到 16 名之外。一个都找不到时 glyph() 只留半格空白、
//  不落墨（宁可空白也不吐豆腐块）—— 于是整片中文变成空白。
//  等宽通道因为字体种类少、CJK 挤进了前 16，代码块反而正常 ——
//  「代码块的中文还在、别处全没了」这个不对称就是它的指纹。
//
//  修法是按 charset 探测 CJK 字体、钉进兜底链。这里把主字体显式钉成一个只有
//  拉丁的家族，让「汉字必须靠兜底」变成必然；三个平台都要能画出墨。
//  Windows / macOS 上这些家族名未必存在，顺带也考一次「指定家族不可用时退回
//  系统字体」—— 那种情况下中文照样得出来。
// =============================================================================
static bool scene_cjk_fallback() {
    const Theme th = Theme::light();
    const Color bg = th.background;

#if defined(AINO_PLATFORM_WINDOWS)
    const char* latin_only = "Consolas";        // 随系统带，正文里没有汉字
#elif defined(AINO_PLATFORM_MACOS)
    const char* latin_only = "Menlo";           // 同上
#else
    const char* latin_only = "DejaVu Sans";     // Ubuntu 默认 sans，无汉字
#endif

    aino::WindowConfig cfg;
    cfg.width  = 260;
    cfg.height = 120;
    cfg.title  = "aino pixels: cjk fallback";
    cfg.font_family = latin_only;
#if defined(AINO_PLATFORM_X11)
    // 把兜底链砍到 4 条：DejaVu Sans 的 Book / Bold / Oblique / BoldOblique
    // 正好把名额占满，汉字一条都排不进来 —— 用户机器上是"前 N 条全被同一家族
    // 的各个 style 吃掉、CJK 排在 N 之后"，这里把 N 压到最小，让回归必然暴露。
    // 只对这个场景生效，S2 仍跑默认的 32 条。
    ::setenv("AINO_X11_CHAIN_CAP", "4", 1);
#endif
    aino::Window win(cfg);
    if (!win.ok()) { win_fail("S4"); return false; }
    win.set_theme(th);

    const Rect box(20.0f, 40.0f, 220.0f, 28.0f);
    Shot shot;
    const bool got = render_scene(win, [&](Ui& ui) {
        ui.clear(bg);
        ui.label("中文兜底", box);
    }, shot);
    if (!got) { fail("S4 抓到后备缓冲", "grab 失败"); return true; }
#if defined(AINO_PLATFORM_X11)
    ::unsetenv("AINO_X11_CHAIN_CAP");      // 后面的场景回到默认
#endif

    int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    if (!ink_bbox(shot, box, bg, bx0, by0, bx1, by1)) {
        fail("S4 主字体没有汉字时，兜底链仍须画出中文",
             std::string("指定家族 = ") + latin_only + "；文字框里一个墨点都没有");
        return true;
    }

    {
        char buf[192];
        const float ink_w = float(bx1 - bx0 + 1) / shot.scale;
        std::snprintf(buf, sizeof buf, "主字体 %s，中文墨迹宽 %.1fpx（字号 %.1f）",
                      latin_only, double(ink_w), double(th.font_size));
        want_true(ink_w >= th.font_size * 2.0f, "S4 墨迹宽度像 4 个全角字", buf);
    }

    {
        // 同上一条场景的道理：.notdef 的方块只有左右两条竖边，中间三分之一列
        // 是空的。这条才钉得住「有墨但是豆腐块」。
        const int mid0 = bx0 + (bx1 - bx0 + 1) / 3;
        const int mid1 = bx0 + (bx1 - bx0 + 1) * 2 / 3;
        int rows = 0, hit = 0;
        for (int y = by0; y <= by1; ++y) {
            bool any = false, mid = false;
            for (int x = bx0; x <= bx1; ++x) {
                const uint8_t* p = &shot.px[size_t(y) * size_t(shot.stride) + size_t(x) * 4u];
                if (!chan_diff(Color(p[CH_R], p[CH_G], p[CH_B], 255), bg, 8)) continue;
                any = true;
                if (x >= mid0 && x < mid1) mid = true;
            }
            if (any) { ++rows; if (mid) ++hit; }
        }
        char buf[192];
        std::snprintf(buf, sizeof buf, "%d 个有墨的行里 %d 行在中间三分之一列有墨",
                      rows, hit);
        want_true(rows > 0 && float(hit) >= float(rows) * 0.45f,
                  "S4 不是豆腐方块（中间列有墨）", buf);
    }

    if (std::getenv("AINO_PIX_BMP")) write_bmp(shot, "ci_frame_4_cjk.bmp");
    return true;
}

// =============================================================================
//  场景 5（只有 Windows 考）：混合编码字面量仍须解得出中文
//
//  MSVC 没加 /utf-8 时，字面量按系统 ANSI 代码页编。串里只要有一个"ANSI 里也
//  有单字节表示"的字符（"·" U+00B7 最典型），编译结果就是**混合编码**：中文
//  留 UTF-8 字节、那个字符变单字节。整串既不合法 UTF-8 也不是纯 ANSI。
//  老的 to_wide 在严格判定失败后整串回退 CP_ACP，每个 UTF-8 字节都被解成一个
//  西欧字母 —— "最小"显示成 "æœ€å°"。这里直接把这种混合串喂给 to_wide：
//  中文必须原样出来。
//
//  注意这段只解释 **Windows 编译器**这一种来源。曾经在这里断言过「源码被当成
//  Latin-1 读」的第三种坏法并配套写了还原逻辑，后来查明那是误判 —— 标题乱的真
//  实原因在 X11（见场景 6），那套还原逻辑已随 0.4.12 删除。
// =============================================================================
static bool scene_mixed_literal() {
#if !defined(AINO_PLATFORM_WINDOWS)
    return true;        // to_wide 是 Windows 专有路径，别的平台没有可考的
#else
    std::string s = "\xE6\x9C\x80\xE5\xB0\x8F";   // "最小" 的 UTF-8 字节
    s.push_back(char(0xB7));                      // 被编译器转成单字节的 "·"
    s += " 0.4.7";
    const std::wstring w = aino::detail::to_wide(s);
    char buf[192];
    std::snprintf(buf, sizeof buf, "解出 %zu 个字符，首个码位 U+%04X（期望 U+6700）",
                  w.size(), w.empty() ? 0u : unsigned(w[0]));
    want_true(w.size() >= 2 && w[0] == wchar_t(0x6700) && w[1] == wchar_t(0x5C0F),
              "S5 混合编码串里的中文没被当成西欧字母", buf);

    // 再考一次**真的字面量**。这条在"没加 /utf-8"的 MSVC 下才会暴露问题：
    // 串里那个 "·" 会被编成单字节，中文留 UTF-8 字节 —— 正好是用户遇到的那种
    // 混合编码。pragma execution_character_set 生效（或者 to_wide 兜住）才对。
    const std::string lit = "最小 · 示例";
    const std::wstring wl = aino::detail::to_wide(lit);
    std::snprintf(buf, sizeof buf, "字面量解出 %zu 个字符，首码位 U+%04X",
                  wl.size(), wl.empty() ? 0u : unsigned(wl[0]));
    want_true(wl.size() >= 2 && wl[0] == wchar_t(0x6700) && wl[1] == wchar_t(0x5C0F),
              "S5 编译期字面量按 UTF-8 处理（/utf-8 或 pragma 生效）", buf);

    // 再钉住"不许退化成整片西欧字母"：混合串解出来必须接近原义长度
    // （"最小"2 + 分隔符 1 + " 0.4.7"6 = 9），而不是每个 UTF-8 字节一个字符。
    std::snprintf(buf, sizeof buf, "长度 %zu（期望 9）末字符 U+%04X",
                  w.size(), w.empty() ? 0u : unsigned(w.back()));
    want_true(w.size() == 9, "S5 混合串没有退化成「每个字节一个西欧字母」", buf);

    // 那个孤立的坏字节要么解成 "·"（单字节 ACP），要么落替换符 —— 不能是别的。
    want_true(w.size() > 2 && (w[2] == wchar_t(0x00B7) || w[2] == wchar_t(0xFFFD)),
              "S5 孤立坏字节按系统 ANSI 单字节解", buf);

    // 路径 (a)：整串是系统 ANSI（用户没加 /utf-8，且串里全是中文）。按 ACP 编成
    // 字节再喂回来必须还原 —— 逐段混合解码不能把这条老路弄坏。
    //
    // 但样本得挑：CI 的 Windows runner 系统代码页是 **1252**，
    // WideCharToMultiByte 会把「最小示例」替换成 4 个 '?'（U+003F），这种情况下
    // 还原不了也不该还原 —— 这条断言因此在 CI 上红了好几轮，本地是 936 看不见。
    // 所以先问 ACP 能不能表示中文：能就用「最小示例」，不能就退到「café」。
    // 后者在 1252 是单字节 0xE9、在 936 是双字节 A8A6，两条 ACP 路径都能考到。
    {
        const wchar_t cjk[] = {0x6700, 0x5C0F, 0x793A, 0x4F8B, 0};   // 最小示例
        char ansi[32] = {0};
        const int got = ::WideCharToMultiByte(CP_ACP, 0, cjk, 4, ansi, int(sizeof ansi),
                                              nullptr, nullptr);
        wchar_t back[8] = {0};
        const int backn = got > 0 ? ::MultiByteToWideChar(CP_ACP, 0, ansi, got, back, 8) : 0;
        const bool cjk_ok = got > 0 && backn == 4 &&
                            back[0] == wchar_t(0x6700) && back[3] == wchar_t(0x4F8B);

        const wchar_t lat[] = {0x63, 0x61, 0x66, 0xE9, 0};           // café
        const wchar_t* src = cjk_ok ? cjk : lat;
        char enc[32] = {0};
        const int n = ::WideCharToMultiByte(CP_ACP, 0, src, 4, enc, int(sizeof enc),
                                            nullptr, nullptr);
        const std::wstring wa = aino::detail::to_wide(std::string(enc, size_t(n > 0 ? n : 0)));
        std::snprintf(buf, sizeof buf, "ACP=%d 样本=%s 编出 %d 字节，解回 %zu 个字符，首码位 U+%04X",
                      int(::GetACP()), cjk_ok ? "最小示例" : "café", n, wa.size(),
                      wa.empty() ? 0u : unsigned(wa[0]));
        want_true(wa.size() == 4 && wa[0] == wchar_t(src[0]) && wa[3] == wchar_t(src[3]),
                  "S5 整串系统 ANSI 仍按 CP_ACP 解", buf);
    }

    // 补充平面（4 字节 UTF-8）要出代理对，别被当两段坏字节按 ACP 解掉。
    {
        const std::wstring we = aino::detail::to_wide(std::string("\xF0\x9F\x98\x80"));  // 😀
        std::snprintf(buf, sizeof buf, "解出 %zu 个字符，首码位 U+%04X",
                      we.size(), we.empty() ? 0u : unsigned(we[0]));
        want_true(we.size() == 2 && we[0] == wchar_t(0xD83D) && we[1] == wchar_t(0xDE00),
                  "S5 4 字节 UTF-8 转成代理对", buf);
    }
    return true;
#endif
}

// =============================================================================
//  X11 标题：必须同时设 EWMH 的 _NET_WM_NAME（类型 UTF8_STRING）
//
//  ICCCM 规定 WM_NAME 是 ISO-8859-1 语义。只调 XStoreName 的话，塞进去的 UTF-8
//  字节会被 GTK/GNOME/KDE **逐字节**当 latin1 解码 —— 标题栏里就成了
//  "æœ€å°ç¤ºä¾‹"，而落进 U+0080..U+009F 的字节还会被画成带十六进制的方框。
//  现代窗口管理器读的是 _NET_WM_NAME，所以这里查的就是那个属性：
//  在不在、类型对不对、字节是不是原样 —— 中文能不能过，全看这三条。
// =============================================================================
#if defined(AINO_PLATFORM_X11)
// 读一个 X11 属性回来，顺带把实际类型和格式带出来
static bool x11_read_prop(Display* dpy, ::Window w, const char* name,
                          std::string& out, Atom& type, int& format) {
    out.clear();
    type = None;
    format = 0;
    const Atom a = XInternAtom(dpy, name, False);
    Atom at = None;
    int af = 0;
    unsigned long n = 0, after = 0;
    unsigned char* p = nullptr;
    if (XGetWindowProperty(dpy, w, a, 0, 4096, False, AnyPropertyType,
                           &at, &af, &n, &after, &p) != Success || !p) {
        if (p) XFree(p);
        return false;
    }
    out.assign(reinterpret_cast<const char*>(p), size_t(n));
    type = at;
    format = af;
    XFree(p);
    return true;
}
#endif

static bool scene_x11_title() {
#if !defined(AINO_PLATFORM_X11)
    return true;        // EWMH 属性是 X11 专有的，别的平台没有可考的
#else
    // 标题用显式转义写，别让源码编码再插一手（这条断言考的是属性，不是编译期）
    const std::string t1("\xE6\x9C\x80\xE5\xB0\x8F\xE7\xA4\xBA\xE4\xBE\x8B");  // 最小示例
    aino::Window win(260, 140, t1);
    aino::detail::X11State* s = reinterpret_cast<aino::detail::X11State*>(win.impl->native);
    if (!s || !s->dpy || !s->win) { win_fail("x11_title"); return false; }

    const Atom utf8 = XInternAtom(s->dpy, "UTF8_STRING", False);
    std::string v;
    Atom ty = None;
    int fm = 0;
    const bool has = x11_read_prop(s->dpy, s->win, "_NET_WM_NAME", v, ty, fm);
    char buf[192];
    std::snprintf(buf, sizeof buf, "_NET_WM_NAME %s，类型 %lu（期望 %lu），格式 %d",
                  has ? "存在" : "缺失", static_cast<unsigned long>(ty),
                  static_cast<unsigned long>(utf8), fm);
    want_true(has && !v.empty() && ty == utf8 && fm == 8,
              "S6 _NET_WM_NAME 已设为 UTF8_STRING", buf);
    std::snprintf(buf, sizeof buf, "读到 %zu 字节，期望 %zu 字节",
                  v.size(), t1.size());
    want_true(v == t1, "S6 标题字节原样写进属性（中文没被 latin1 化）", buf);

    // Alt+Tab / 任务栏读的是 _NET_WM_ICON_NAME，同一个坑，一起钉住
    std::string vi;
    Atom tyi = None;
    int fmi = 0;
    const bool hasi = x11_read_prop(s->dpy, s->win, "_NET_WM_ICON_NAME", vi, tyi, fmi);
    std::snprintf(buf, sizeof buf, "读到 %zu 字节，类型 %lu（期望 %lu），格式 %d",
                  vi.size(), static_cast<unsigned long>(tyi),
                  static_cast<unsigned long>(utf8), fmi);
    want_true(hasi && tyi == utf8 && vi == t1,
              "S6 _NET_WM_ICON_NAME 同为 UTF8_STRING", buf);

    // 运行中改标题：set_title 也必须走同一条路（不只是建窗那一刻）
    const std::string t2("\xE7\xA4\xBA\xE4\xBE\x8B");  // 示例
    win.set_title(t2);
    std::string v2;
    Atom ty2 = None;
    int fm2 = 0;
    const bool has2 = x11_read_prop(s->dpy, s->win, "_NET_WM_NAME", v2, ty2, fm2);
    std::snprintf(buf, sizeof buf, "读到 %zu 字节，期望 %zu 字节",
                  v2.size(), t2.size());
    want_true(has2 && ty2 == utf8 && v2 == t2,
              "S6 set_title() 后 _NET_WM_NAME 跟着变", buf);
    return true;
#endif
}

int main() {
    std::printf("=== aino ci_pixels ===\n");

    if (!scene_clear())       return 2;
    if (!scene_primitives())  return 2;
    if (!scene_dark())        return 2;
    if (!scene_cjk_fallback()) return 2;
    if (!scene_mixed_literal()) return 2;
    if (!scene_x11_title())      return 2;

    std::printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
