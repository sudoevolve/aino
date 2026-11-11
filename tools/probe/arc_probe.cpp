// raster_arc 的像素级断言。
//
// 要证明的只有一件事：**环上没有"拼接"**。
// 旧实现用 84 段 draw_line 拼一圈，每段两端是圆头，相邻段在接缝处重叠 →
// 同一个像素被 blend 两次 → 半透明色在接缝处明显更深，看起来一圈一节一节的。
// 无接缝判据：
//   半透明整圈，沿角度采样中心线，所有采样点必须**完全相同**（max-min == 0）。
// 旧实现这一项会失败（实测 max-min 可达 40 以上），新实现必须是 0。
//
// 其余断言：角度边界、渐变单调、端点圆帽、包围盒不越界。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace aino;

static int g_pass = 0, g_fail = 0;

static void check(bool ok, const char* what, const char* detail = "") {
    if (ok) { ++g_pass; std::printf("  [ok  ] %s %s\n", what, detail); }
    else    { ++g_fail; std::printf("  [FAIL] %s %s\n", what, detail); }
}

// 旧实现（spinner 改之前那一版）：把一圈拆成 48 段 draw_line 拼出来。
// 作为反例，验证无接缝断言能检测到重复混合。
static void legacy_ring(detail::Canvas& cv, float cx, float cy, float rad, float lw, const Color& c) {
    const int seg = 48;
    for (int i = 0; i < seg; ++i) {
        const float a0 = float(i) / float(seg) * 6.2831853f;
        const float a1 = float(i + 1) / float(seg) * 6.2831853f;
        detail::raster_line(cv, cx + std::cos(a0) * rad, cy + std::sin(a0) * rad,
                            cx + std::cos(a1) * rad, cy + std::sin(a1) * rad, c, lw);
    }
}

struct Buf {
    std::vector<uint8_t> px;
    detail::Canvas cv;
    int w, h;
    Buf(int W, int H) : px(size_t(W) * size_t(H) * 4, 255), w(W), h(H) {
        cv.px = px.data(); cv.w = W; cv.h = H; cv.stride = W * 4;
    }
    // 取像素的灰度（画布底色 255，画面用黑色 → 值越小越黑）
    int lum(int x, int y) const {
        const uint8_t* p = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
        return (int(p[0]) * 30 + int(p[1]) * 59 + int(p[2]) * 11) / 100;
    }
};

int main() {
    std::printf("== raster_arc 像素断言 ==\n");

    // ---------------------------------------------------------------- 1) 无接缝
    // 半透明整圈：alpha=128。若某个像素被 blend 两次，结果会明显比 128 深。
    {
        Buf b(96, 96);
        detail::raster_arc(b.cv, 48.0f, 48.0f, 30.0f, 7.0f, Color(0, 0, 0, 128), 0.0f, 0.0f, 1.0f);
        int lo = 999, hi = -1;
        int n = 0;
        for (int deg = 0; deg < 360; ++deg) {
            const double a = double(deg) * 3.14159265358979 / 180.0;
            const int x = int(48.0 + std::cos(a) * 30.0);
            const int y = int(48.0 + std::sin(a) * 30.0);
            const int v = b.lum(x, y);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            ++n;
        }
        char d[128];
        std::snprintf(d, sizeof(d), "(360 点，lum %d..%d)", lo, hi);
        check(n == 360 && hi - lo == 0, "半透明整圈无接缝（沿角度 360 点完全一致）", d);
        // 中心线必须是满覆盖：128 的 alpha 压在白底上 ≈ 127
        check(lo >= 120 && lo <= 134, "环中心线覆盖率=1（半透明压白底 ≈127）", "");
    }

    // 反面参照：同样的采样打在"拼 48 段线段"的旧实现上，必须能测出接缝。
    // 反例必须被判为有接缝，以验证检测方法有效。
    {
        Buf b(96, 96);
        legacy_ring(b.cv, 48.0f, 48.0f, 30.0f, 7.0f, Color(0, 0, 0, 128));
        int lo = 999, hi = -1;
        for (int deg = 0; deg < 360; ++deg) {
            const double a = double(deg) * 3.14159265358979 / 180.0;
            const int x = int(48.0 + std::cos(a) * 30.0);
            const int y = int(48.0 + std::sin(a) * 30.0);
            const int v = b.lum(x, y);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        char d[160];
        std::snprintf(d, sizeof(d), "(旧实现 lum %d..%d，落差 %d)", lo, hi, hi - lo);
        check(hi - lo > 12, "反面参照：拼线段确实会叠出接缝", d);
    }

    // ------------------------------------------------------------ 2) 包围盒不越界
    {
        Buf b(96, 96);
        detail::raster_arc(b.cv, 48.0f, 48.0f, 20.0f, 6.0f, Color(0, 0, 0, 200), 0.0f, 0.0f, 1.0f);
        // 环外半径 = 20+3 = 23，留 1px AA → 24。之外必须一点都没碰到。
        int touched = 0;
        for (int y = 0; y < 96; ++y)
            for (int x = 0; x < 96; ++x) {
                const double dx = (double(x) + 0.5) - 48.0, dy = (double(y) + 0.5) - 48.0;
                const double d = std::sqrt(dx * dx + dy * dy);
                if (d > 24.6 && b.lum(x, y) != 255) ++touched;
            }
        char d[128];
        std::snprintf(d, sizeof(d), "（环外 r>24.6 被碰到的像素 %d 个）", touched);
        check(touched == 0, "环外不落笔", d);
    }

    // -------------------------------------------------------------- 3) 角度边界
    // sweep = 90°，从 0° 起：45° 在有弧的一侧，315° 在空的一侧。
    {
        Buf b(96, 96);
        detail::raster_arc(b.cv, 48.0f, 48.0f, 30.0f, 6.0f, Color(0, 0, 0, 255), 0.0f, 1.5707963f, 1.0f);
        const int x45  = int(48.0 + std::cos(0.785398) * 30.0);
        const int y45  = int(48.0 + std::sin(0.785398) * 30.0);
        const int x315 = int(48.0 + std::cos(5.497787) * 30.0);
        const int y315 = int(48.0 + std::sin(5.497787) * 30.0);
        char d[160];
        std::snprintf(d, sizeof(d), "(弧内 %d，弧外 %d)", b.lum(x45, y45), b.lum(x315, y315));
        check(b.lum(x45, y45) < 40 && b.lum(x315, y315) == 255, "90° 弧：弧内有色、弧外干净", d);
    }

    // -------------------------------------------------------- 4) 渐变单调 + 端点
    // a_from = 0：尾端全透明、头端满色，中间单调递增（越黑 alpha 越大）。
    {
        Buf b(128, 128);
        detail::raster_arc(b.cv, 64.0f, 64.0f, 40.0f, 8.0f, Color(0, 0, 0, 255), 0.0f, 4.7123889f, 0.0f);
        int prev = 999;          // 亮度，应该单调不增
        bool monotonic = true;
        int tail = -1, head = -1;
        for (int deg = 2; deg <= 268; deg += 2) {
            const double a = double(deg) * 3.14159265358979 / 180.0;
            const int x = int(64.0 + std::cos(a) * 40.0);
            const int y = int(64.0 + std::sin(a) * 40.0);
            const int v = b.lum(x, y);
            if (v > prev + 12) monotonic = false;   // 允许 ±AA 抖动
            prev = v;
            if (deg == 2)   tail = v;
            if (deg == 268) head = v;
        }
        char d[160];
        std::snprintf(d, sizeof(d), "(尾 %d → 头 %d，亮度应递减)", tail, head);
        check(tail > 200, "拖尾端接近透明", d);
        check(head < 40, "头部满色", "");
        check(monotonic, "沿弧单调渐变（无反向跳变）", "");
    }

    // ------------------------------------------------------------ 5) 半径/宽度为零
    {
        Buf b(32, 32);
        detail::raster_arc(b.cv, 16.0f, 16.0f, 0.0f, 4.0f, Color(0, 0, 0, 255), 0.0f, 0.0f, 1.0f);
        int dark = 0;
        for (int i = 0; i < 32 * 32; ++i) if (b.px[size_t(i) * 4] != 255) ++dark;
        check(dark == 0, "radius=0 不画（不除零、不留脏点）", "");
    }

    // ------------------------------------------------------- 6) 端点圆帽（不硬切）
    // 在尾端外侧沿切线方向 1.5px 处应还有半覆盖，而不是立刻 255。
    {
        Buf b(128, 128);
        detail::raster_arc(b.cv, 64.0f, 64.0f, 40.0f, 10.0f, Color(0, 0, 0, 255), 0.0f, 1.5707963f, 1.0f);
        // 尾端在 (104,64)，切线方向是 -y
        const int x = 104;
        const int y0 = int(64.0 - 4.0);          // 落在半圆帽内（hw=5）
        int v0 = b.lum(x, y0);
        check(v0 < 200, "尾端有圆帽（切线方向 4px 处仍有覆盖）", "");
    }

    std::printf("\n通过 %d 项，失败 %d 项\n", g_pass, g_fail);

    // 顺手把"旧 vs 新"两个同样的半透明环写成 PPM，用来看接缝长什么样
    {
        const int W = 120;
        Buf a(W, W), b(W, W);
        legacy_ring(a.cv, 60.0f, 60.0f, 42.0f, 11.0f, Color(0, 0, 0, 128));
        detail::raster_arc(b.cv, 60.0f, 60.0f, 42.0f, 11.0f, Color(0, 0, 0, 128), 0.0f, 0.0f, 1.0f);
        const char* names[2] = { "arc_legacy.ppm", "arc_new.ppm" };
        Buf* bufs[2] = { &a, &b };
        for (int k = 0; k < 2; ++k) {
            std::FILE* f = std::fopen(names[k], "wb");
            if (!f) continue;
            std::fprintf(f, "P6\n%d %d\n255\n", W, W);
            for (int i = 0; i < W * W; ++i)
                std::fwrite(&bufs[k]->px[size_t(i) * 4], 1, 3, f);
            std::fclose(f);
        }
        std::printf("对照图 -> arc_legacy.ppm / arc_new.ppm\n");
    }
    return g_fail == 0 ? 0 : 1;
}
