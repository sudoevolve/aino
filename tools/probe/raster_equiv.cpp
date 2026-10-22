// 光栅化等价性测试：把优化后的 raster_rrect / raster_rrect_stroke / clear
// 与"朴素参考实现"（逐像素 4 倍子采样，就是优化前的算法）逐像素比对。
//
// 这是优化这类代码唯一靠谱的验证方式 —— 不看图，直接把新旧算法的输出
// 拉到同一块内存里对差；只要有任何一个像素差的绝对值 > 1，就算失败。
//
// 覆盖：r=0 的普通矩形、各种圆角半径、亚像素坐标、随机尺寸、
//       1px / 2px / 0.5px 描边、描边宽度 >= 半径的退化情形。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace aino;
using namespace aino::detail;

static int g_pass = 0, g_fail = 0, g_worst = 0;

// ---------------------------------------------------------------------------
// 朴素参考实现：算法与"优化前"逐字一致
// ---------------------------------------------------------------------------
static void ref_rrect(Canvas& cv, const Rect& r, const Color& c, float rad) {
    int y0 = (int)std::floor(r.y), y1 = (int)std::ceil(r.bottom());
    for (int py = y0; py < y1; ++py) {
        float xls[AINO_SS], xrs[AINO_SS];
        float mn = 1e9f, mx = -1e9f;
        for (int s = 0; s < AINO_SS; ++s) {
            float sy = float(py) + (float(s) + 0.5f) / float(AINO_SS);
            Span sp = rrect_span(sy, r, rad);
            xls[s] = sp.xl; xrs[s] = sp.xr;
            mn = std::min(mn, sp.xl); mx = std::max(mx, sp.xr);
        }
        if (mx <= mn) continue;
        int px0 = std::max(int(std::floor(mn)), 0);
        int px1 = std::min(int(std::ceil(mx)), cv.w);
        for (int x = px0; x < px1; ++x) {
            float cov = 0.0f;
            for (int s = 0; s < AINO_SS; ++s) {
                float a = float(x), b = float(x + 1);
                float ov = std::min(b, xrs[s]) - std::max(a, xls[s]);
                if (ov > 0.0f) cov += ov;
            }
            cov *= (1.0f / float(AINO_SS));
            if (cov > 0.0005f) cv.blend(x, py, c, clampf(cov, 0.0f, 1.0f));
        }
    }
}

static void ref_stroke(Canvas& cv, const Rect& r, const Color& c, float rad, float width) {
    float w = std::max(width, 0.5f);
    Rect inner = r.inset(w);
    float inner_rad = std::max(0.0f, rad - w);
    int y0 = (int)std::floor(r.y), y1 = (int)std::ceil(r.bottom());
    for (int py = y0; py < y1; ++py) {
        float ol[AINO_SS], orr[AINO_SS], il[AINO_SS], irr[AINO_SS];
        float mn = 1e9f, mx = -1e9f;
        for (int s = 0; s < AINO_SS; ++s) {
            float sy = float(py) + (float(s) + 0.5f) / float(AINO_SS);
            Span so = rrect_span(sy, r, rad);
            Span si = inner.w > 0 ? rrect_span(sy, inner, inner_rad) : Span{1.0f, 0.0f};
            ol[s] = so.xl; orr[s] = so.xr; il[s] = si.xl; irr[s] = si.xr;
            mn = std::min(mn, so.xl); mx = std::max(mx, so.xr);
        }
        if (mx <= mn) continue;
        int px0 = std::max(int(std::floor(mn)), 0);
        int px1 = std::min(int(std::ceil(mx)), cv.w);
        for (int x = px0; x < px1; ++x) {
            float co = 0.0f, ci = 0.0f;
            for (int s = 0; s < AINO_SS; ++s) {
                float a = float(x), b = float(x + 1);
                float ovo = std::min(b, orr[s]) - std::max(a, ol[s]);
                if (ovo > 0.0f) co += ovo;
                float ovi = std::min(b, irr[s]) - std::max(a, il[s]);
                if (ovi > 0.0f) ci += ovi;
            }
            co *= (1.0f / float(AINO_SS));
            ci *= (1.0f / float(AINO_SS));
            float cov = clampf(co, 0.0f, 1.0f) - clampf(ci, 0.0f, 1.0f);
            if (cov > 0.0005f) cv.blend(x, py, c, cov);
        }
    }
}

// 多边形填充的朴素参考实现（与优化前逐字一致）
static void ref_poly(Canvas& cv, const std::vector<Vec2>& pts, const Color& c) {
    const size_t n = pts.size();
    if (n < 3 || c.a == 0) return;
    float ymin = pts[0].y, ymax = pts[0].y;
    for (size_t i = 1; i < n; ++i) { ymin = std::min(ymin, pts[i].y); ymax = std::max(ymax, pts[i].y); }
    const int y0 = std::max(0, int(std::floor(ymin)));
    const int y1 = std::min(cv.h, int(std::ceil(ymax)) + 1);
    if (y1 <= y0) return;
    std::vector<float> xs;
    std::vector<Vec2>  spans;
    std::vector<float> cov;
    for (int py = y0; py < y1; ++py) {
        spans.clear();
        float mn = 1e9f, mx = -1e9f;
        for (int s = 0; s < AINO_SS; ++s) {
            const float sy = float(py) + (float(s) + 0.5f) / float(AINO_SS);
            xs.clear();
            for (size_t i = 0; i < n; ++i) {
                const Vec2& a = pts[i];
                const Vec2& b = pts[(i + 1) % n];
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
                mn = std::min(mn, xs[k]); mx = std::max(mx, xs[k + 1]);
            }
        }
        if (mx <= mn) continue;
        const int px0 = std::max(0, int(std::floor(mn)));
        const int px1 = std::min(cv.w, int(std::ceil(mx)));
        if (px1 <= px0) continue;
        cov.assign(size_t(px1 - px0), 0.0f);
        for (const Vec2& sp : spans) {
            for (int x = px0; x < px1; ++x) {
                const float ov = std::min(float(x + 1), sp.y) - std::max(float(x), sp.x);
                if (ov > 0.0f) cov[size_t(x - px0)] += ov;
            }
        }
        const float inv = 1.0f / float(AINO_SS);
        for (int x = px0; x < px1; ++x) {
            const float v = cov[size_t(x - px0)] * inv;
            if (v > 0.0005f) cv.blend(x, py, c, clampf(v, 0.0f, 1.0f));
        }
    }
}

// ---------------------------------------------------------------------------
static int compare(const char* what, const Canvas& a, const Canvas& b, int W, int H) {
    int worst = 0, bad = 0;
    int fx = -1, fy = -1, fr = 0, fg2 = 0, fb = 0;
    for (int y = 0; y < H; ++y) {
        const uint8_t* pa = a.px + size_t(y) * size_t(a.stride);
        const uint8_t* pb = b.px + size_t(y) * size_t(b.stride);
        for (int x = 0; x < W * 4; ++x) {
            int d = std::abs(int(pa[x]) - int(pb[x]));
            if (d > worst) worst = d;
            if (d > 1) {
                ++bad;
                if (fx < 0) {
                    fx = (x / 4); fy = y; fr = pa[(x / 4) * 4 + 1];
                    fg2 = pb[(x / 4) * 4 + 1]; fb = pa[(x / 4) * 4 + 0];
                }
            }
        }
    }
    if (worst > g_worst) g_worst = worst;
    if (bad) {
        ++g_fail;
        std::printf("  [FAIL] %-34s 最大差 %3d 像素@(%d,%d) ref.g=%d opt.g=%d\n",
                    what, worst, fx, fy, fr, fg2);
    } else { ++g_pass; }
    return worst;
}

struct Buf {
    std::vector<uint8_t> mem;
    Canvas cv{};
    Buf(int w, int h) : mem(size_t(w) * size_t(h) * 4u, 0) {
        cv.px = mem.data(); cv.w = w; cv.h = h; cv.stride = w * 4;
    }
    void reset() { std::fill(mem.begin(), mem.end(), uint8_t(0)); }
};

int main() {
    const int W = 140, H = 90;
    Buf A(W, H), B(W, H);
    std::mt19937 rng(20261016u);
    std::uniform_real_distribution<float> U(-4.0f, 120.0f);
    std::uniform_real_distribution<float> SZ(1.0f, 70.0f);
    const Color fg{ 32, 96, 200, 255 };      // 不透明
    const Color fg_aa{ 200, 40, 40, 128 };   // 半透明，走 blend 的混合分支

    std::printf("== raster_rrect 等价性 ==\n");
    const float rads[] = { 0.0f, 0.3f, 1.0f, 2.5f, 8.0f, 20.0f, 999.0f };
    int cases = 0;
    for (int i = 0; i < 400; ++i) {
        Rect r(U(rng), U(rng), SZ(rng), SZ(rng));
        float rad = rads[rng() % 7];
        const Color& c = (rng() % 4 == 0) ? fg_aa : fg;
        A.reset(); B.reset();
        ref_rrect(A.cv, r, c, rad);
        raster_rrect(B.cv, r, c, rad);
        char tag[96];
        std::snprintf(tag, sizeof(tag), "fill r=(%.2f,%.2f,%.2f,%.2f) rad=%.1f a=%d",
                      r.x, r.y, r.w, r.h, rad, c.a);
        compare(tag, A.cv, B.cv, W, H);
        ++cases;
    }
    std::printf("  填充用例 %d 个\n", cases);

    std::printf("== raster_rrect_stroke 等价性 ==\n");
    const float widths[] = { 0.0f, 0.5f, 0.8f, 1.0f, 1.5f, 2.0f, 3.0f, 8.0f, 40.0f };
    cases = 0;
    for (int i = 0; i < 500; ++i) {
        Rect r(U(rng), U(rng), SZ(rng), SZ(rng));
        float rad = rads[rng() % 7];
        float w = widths[rng() % 9];
        const Color& c = (rng() % 4 == 0) ? fg_aa : fg;
        A.reset(); B.reset();
        ref_stroke(A.cv, r, c, rad, w);
        raster_rrect_stroke(B.cv, r, c, rad, w);
        char tag[110];
        std::snprintf(tag, sizeof(tag), "stroke r=(%.2f,%.2f,%.2f,%.2f) rad=%.1f w=%.1f a=%d",
                      r.x, r.y, r.w, r.h, rad, w, c.a);
        compare(tag, A.cv, B.cv, W, H);
        ++cases;
    }
    std::printf("  描边用例 %d 个\n", cases);

    std::printf("== 裁剪区内的一致性（clipped） ==\n");
    cases = 0;
    for (int i = 0; i < 200; ++i) {
        Rect r(U(rng), U(rng), SZ(rng), SZ(rng));
        float rad = rads[rng() % 7];
        float w = widths[rng() % 9];
        A.reset(); B.reset();
        // 两边设置同样的裁剪区，验证快路径尊重裁剪
        A.cv.set_clip(31, 17, 110, 71);
        B.cv.set_clip(31, 17, 110, 71);
        ref_rrect(A.cv, r, fg, rad);
        raster_rrect(B.cv, r, fg, rad);
        A.cv.clear_clip(); B.cv.clear_clip();
        char t1[128];
        std::snprintf(t1, sizeof(t1), "clipped fill   r=(%.2f,%.2f,%.2f,%.2f) rad=%.1f", r.x, r.y, r.w, r.h, rad);
        compare(t1, A.cv, B.cv, W, H);
        A.reset(); B.reset();
        A.cv.set_clip(31, 17, 110, 71);
        B.cv.set_clip(31, 17, 110, 71);
        ref_stroke(A.cv, r, fg, rad, w);
        raster_rrect_stroke(B.cv, r, fg, rad, w);
        A.cv.clear_clip(); B.cv.clear_clip();
        char t2[128];
        std::snprintf(t2, sizeof(t2), "clipped stroke r=(%.2f,%.2f,%.2f,%.2f) rad=%.1f w=%.2f wd=%.0f",
                      r.x, r.y, r.w, r.h, rad, w, float(W));
        compare(t2, A.cv, B.cv, W, H);
        ++cases;
    }
    std::printf("  裁剪用例 %d 组\n", cases);

    std::printf("== raster_poly 等价性 ==\n");
    {
        std::mt19937 rp(777u);
        std::uniform_real_distribution<float> U2(-6.0f, 130.0f);
        std::uniform_int_distribution<int> NZ(3, 14);      // 顶点数
        std::uniform_int_distribution<int> SP(3, 7);       // 星形尖数
        cases = 0;
        for (int i = 0; i < 300; ++i) {
            std::vector<Vec2> pts;
            const int kind = int(rp() % 4);
            if (kind == 0) {                               // 随机凸包近似：极角排序
                const float cx = U2(rp), cy = U2(rp);
                const int m = NZ(rp);
                for (int k = 0; k < m; ++k) {
                    const float ang = 2.0f * 3.14159265f * float(k) / float(m);
                    const float rr = 4.0f + 30.0f * float(rp() % 100) / 100.0f;
                    pts.push_back(Vec2(cx + std::cos(ang) * rr, cy + std::sin(ang) * rr));
                }
            } else if (kind == 1) {                        // 星形（凹）
                pts = star_points(U2(rp), U2(rp), SP(rp), 34.0f, 14.0f);
            } else if (kind == 2) {                        // 面积填充（折线 + 下边界，条形/面积图那种）
                const float x0 = U2(rp), yb = U2(rp) + 40.0f;
                pts.push_back(Vec2(x0, yb));
                for (int k = 0; k < 6; ++k)
                    pts.push_back(Vec2(x0 + float(k) * 14.0f, yb - 5.0f - 25.0f * float(int(rp() % 100)) / 100.0f));
                pts.push_back(Vec2(x0 + 5.0f * 14.0f, yb));
            } else {                                       // 细长条（退化成近似矩形）
                const float x0 = U2(rp), y0 = U2(rp);
                const float w = 2.0f + 20.0f * float(rp() % 100) / 100.0f;
                const float h = 2.0f + 40.0f * float(rp() % 100) / 100.0f;
                pts = { Vec2(x0, y0), Vec2(x0 + w, y0), Vec2(x0 + w, y0 + h), Vec2(x0, y0 + h) };
            }
            const Color& c = (rp() % 4 == 0) ? fg_aa : fg;
            A.reset(); B.reset();
            ref_poly(A.cv, pts, c);
            raster_poly(B.cv, pts, c);
            compare("poly", A.cv, B.cv, W, H);
            ++cases;
        }
        std::printf("  多边形用例 %d 个\n", cases);
    }

    std::printf("== clear / fill_row_fast ==\n");
    {
        Buf C(W, H);
        const Color bg{ 250, 250, 251, 255 };
        C.cv.clear(bg);
        int bad = 0;
        for (int y = 0; y < H; ++y) {
            const uint8_t* p = C.cv.px + size_t(y) * size_t(C.cv.stride);
            for (int x = 0; x < W; ++x) {
                if (p[x * 4 + CH_R] != bg.r || p[x * 4 + CH_G] != bg.g ||
                    p[x * 4 + CH_B] != bg.b || p[x * 4 + CH_A] != 255) ++bad;
            }
        }
        if (bad) { ++g_fail; std::printf("  [FAIL] clear 有 %d 个像素不对\n", bad); }
        else { ++g_pass; std::printf("  clear 整块正确（%dx%d）\n", W, H); }

        // fill_row_fast 越界与裁剪
        Buf D(W, H);
        D.cv.fill_row_fast(10, -50, W + 50, bg);       // 越界应被夹住
        D.cv.fill_row_fast(-5, 0, 10, bg);             // 越界行应无视
        D.cv.fill_row_fast(H + 5, 0, 10, bg);
        D.cv.set_clip(20, 0, 40, H);
        D.cv.fill_row_fast(11, 0, W, bg);              // 应只填 [20,40)
        D.cv.clear_clip();
        int bad2 = 0;
        for (int x = 0; x < W; ++x) {
            const uint8_t* p10 = D.cv.px + size_t(10) * size_t(D.cv.stride) + size_t(x) * 4u;
            if (p10[CH_R] != bg.r) ++bad2;
        }
        for (int x = 0; x < W; ++x) {
            const uint8_t* p11 = D.cv.px + size_t(11) * size_t(D.cv.stride) + size_t(x) * 4u;
            bool want = (x >= 20 && x < 40);
            if ((p11[CH_R] == bg.r) != want) ++bad2;
        }
        if (bad2) { ++g_fail; std::printf("  [FAIL] fill_row_fast 边界/裁剪有 %d 处不对\n", bad2); }
        else { ++g_pass; std::printf("  fill_row_fast 越界夹取 + 裁剪正确\n"); }
    }

    std::printf("\n通过 %d 项，失败 %d 项（全局最大通道差 %d/255）\n", g_pass, g_fail, g_worst);
    return g_fail ? 1 : 0;
}
