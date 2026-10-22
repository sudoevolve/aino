#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cstdio>
#include <vector>
using namespace aino;
using namespace aino::detail;

// 参考实现（与优化前逐字一致）
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
            co *= 0.25f; ci *= 0.25f;
            float cov = clampf(co, 0.0f, 1.0f) - clampf(ci, 0.0f, 1.0f);
            if (cov > 0.0005f) cv.blend(x, py, c, cov);
        }
    }
}

struct Buf {
    std::vector<uint8_t> mem; Canvas cv{};
    Buf(int w, int h) : mem(size_t(w) * size_t(h) * 4u, 0) {
        cv.px = mem.data(); cv.w = w; cv.h = h; cv.stride = w * 4;
    }
    const uint8_t* at(int x, int y) const { return cv.px + size_t(y) * size_t(cv.stride) + size_t(x) * 4u; }
};

int main(int argc, char** argv) {
    int W = 140, H = 90;
    Rect r(43.92f, 1.97f, 10.13f, 52.02f);
    float rad = 0.3f, width = 2.0f;
    bool use_clip = argc > 1;
    if (argc > 1) { float a[4]; if (sscanf(argv[1], "%f,%f,%f,%f", &a[0],&a[1],&a[2],&a[3]) == 4) r = Rect(a[0],a[1],a[2],a[3]); }
    if (argc > 2) rad = float(atof(argv[2]));
    if (argc > 3) width = float(atof(argv[3]));

    Buf A(W, H), B(W, H);
    if (use_clip) { A.cv.set_clip(31, 17, 110, 71); B.cv.set_clip(31, 17, 110, 71); }
    const Color c{ 32, 96, 200, 255 };
    ref_stroke(A.cv, r, c, rad, width);
    raster_rrect_stroke(B.cv, r, c, rad, width);

    std::printf("r=(%.2f,%.2f,%.2f,%.2f) rad=%.2f w=%.2f  %s\n", r.x, r.y, r.w, r.h, rad, width,
                use_clip ? "[已设裁剪 31,17,110,71]" : "[无裁剪]");
    int y = int(std::floor(r.y)) + 50;
    std::printf("row y=%d :  x, ref.g, opt.g\n", y);
    for (int x = int(std::floor(r.x)) - 3; x <= int(std::floor(r.x)) + 6; ++x) {
        if (x < 0 || x >= W) continue;
        int a = A.at(x, y)[1], b = B.at(x, y)[1];
        std::printf("   x=%3d  ref=%3d  opt=%3d %s\n", x, a, b, a != b ? "  <== 不一致" : "");
    }
    // 统计不一致像素
    int bad = 0;
    for (int yy = 0; yy < H; ++yy) for (int xx = 0; xx < W; ++xx) {
        int a = A.at(xx, yy)[1], b = B.at(xx, yy)[1];
        if (std::abs(a - b) > 1) {
            ++bad;
            if (bad <= 12) std::printf("   差异 @(%d,%d)  ref=%d opt=%d\n", xx, yy, a, b);
        }
    }
    std::printf("不一致像素数 = %d\n", bad);
    return 0;
}
