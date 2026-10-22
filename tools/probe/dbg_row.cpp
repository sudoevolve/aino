#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cstdio>
using namespace aino;
using namespace aino::detail;

int main() {
    const Rect r(20.19f, 42.35f, 2.14f, 57.53f);
    const Color c{ 32, 96, 200, 255 };
    const float rad = 8.0f;
    const float rad_eff = (rad > 0.0f) ? std::min(rad, std::min(r.w, r.h) * 0.5f) : 0.0f;
    std::printf("r=(%.2f,%.2f,%.2f,%.2f) bottom=%.2f rad_eff=%.3f\n",
                r.x, r.y, r.w, r.h, r.bottom(), rad_eff);

    const int py = (int)std::floor(r.y);
    float xls[AINO_SS], xrs[AINO_SS];
    float mn = 1e9f, mx = -1e9f, inl = -1e9f, inr = 1e9f;
    for (int s = 0; s < AINO_SS; ++s) {
        float sy = float(py) + (float(s) + 0.5f) / float(AINO_SS);
        Span sp = rrect_span(sy, r, rad_eff);
        xls[s] = sp.xl; xrs[s] = sp.xr;
        if (sp.xl < mn) mn = sp.xl;
        if (sp.xr > mx) mx = sp.xr;
        if (sp.xl > inl) inl = sp.xl;
        if (sp.xr < inr) inr = sp.xr;
        std::printf("  s=%d sy=%.3f span=[%.3f, %.3f]%s\n", s, sy, sp.xl, sp.xr,
                    (sp.xl == 1.0f && sp.xr == 0.0f) ? "  <= 空哨兵" : "");
    }
    std::printf("mn=%.3f mx=%.3f inl=%.3f inr=%.3f\n", mn, mx, inl, inr);
    const int px0 = std::max(int(std::floor(mn)), 0);
    const int px1 = std::min(int(std::ceil(mx)), 140);
    const int ix0 = int(std::ceil(inl));
    const int ix1 = int(std::floor(inr));
    std::printf("px0=%d px1=%d | ix0=%d ix1=%d | 快填带有效=%d\n",
                px0, px1, ix0, ix1, (ix1 > ix0));
    // 逐个像素对比参考/优化在该行的覆盖率
    for (int x = px0; x < px1; ++x) {
        float ref = 0.0f;
        for (int s = 0; s < AINO_SS; ++s) {
            float a = float(x), b = float(x + 1);
            float ov = std::min(b, xrs[s]) - std::max(a, xls[s]);
            if (ov > 0.0f) ref += ov;
        }
        ref *= 0.25f;
        bool in_band = (ix1 > ix0) && (x >= std::max(ix0, px0)) && (x < std::min(ix1, px1));
        float opt = in_band ? 1.0f : ref;
        if (std::fabs(ref - opt) > 0.004f)
            std::printf("  !! x=%d ref_cov=%.4f opt_cov=%.4f%s\n", x, ref, opt, in_band ? " (走了快填带)" : "");
    }
    return 0;
}
