// 投影掩膜缓存与逐像素直算的等价性检查。
//
// 为什么要这个探针：`raster_shadow` 从"每帧逐像素求距离场"改成"缓存一张覆盖率
// 掩膜再整行合成"，收益是 3 倍，代价是掩膜按 16 位存 —— 理论上会有 1 LSB 的
// 量化误差。这个探针把旧实现原样抄成参考实现，对六组参数（含小数相位、贴边 +
// 裁剪）逐像素比对，把"看不见的差异"变成可断言的数字。
//
// 期望：最大通道差 ≤ 1；差异通道占比 < 0.01%。超过即失败。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cstdio>
#include <chrono>
#include <vector>

namespace {

int failures = 0;
void check(bool ok, const char* what, const char* detail = "") {
    if (!ok) { ++failures; std::printf("[FAIL] %s %s\n", what, detail); }
    else std::printf("[ OK ] %s\n", what);
}

// 改动前的实现（逐像素求距离场）。作为参考实现保留在这里。
void shadow_reference(aino::Canvas& cv, const aino::Rect& r, float rad, float blur,
                      const aino::Color& c, float dy) {
    if (blur <= 0.5f || c.a == 0) return;
    const aino::Rect s = r.offset(0.0f, dy);
    const float inv_blur = 1.0f / blur;
    const float ext = blur * 1.35f;
    const int x0 = std::max(0, int(std::floor(s.x - ext)));
    const int x1 = std::min(cv.w, int(std::ceil(s.right() + ext)));
    const int y0 = std::max(0, int(std::floor(s.y - ext)));
    const int y1 = std::min(cv.h, int(std::ceil(s.bottom() + ext)));
    if (x0 >= x1 || y0 >= y1) return;
    for (int y = y0; y < y1; ++y) {
        const float pyc = float(y) + 0.5f;
        for (int x = x0; x < x1; ++x) {
            const float d = aino::detail::rrect_sdf(float(x) + 0.5f, pyc, s, rad);
            const float cov = aino::detail::shadow_cov(d * inv_blur);
            if (cov < 0.004f) continue;
            cv.blend(x, y, c, cov);
        }
    }
}

double now_ms() {
    using namespace std::chrono;
    return double(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count()) / 1000.0;
}

} // namespace

int main() {
    const int W = 1180, H = 760;
    struct Case { const char* name; float x, y, w, h, rad, blur, dy; int alpha; bool clip; };
    const Case cases[] = {
        {"模态 480x300 blur90",   350.0f, 230.0f,  480, 300, 12.0f, 90.0f, 30.0f,  82, false},
        {"模态 小数相位",          350.5f, 230.25f, 480, 300, 12.0f, 90.0f, 30.0f,  82, false},
        {"浮卡 420x300 blur44",   380.0f, 230.0f,  420, 300, 12.0f, 44.0f, 10.0f,  56, false},
        {"菜单 240x200 blur30",   470.0f, 280.0f,  240, 200, 12.0f, 30.0f,  5.0f,  52, false},
        {"滑块 32x20 blur7",      574.0f, 370.0f,   32,  20, 10.0f,  7.0f,  1.5f, 26, false},
        {"贴边 + 裁剪",           -20.0f, -10.0f,  480, 300, 12.0f, 90.0f, 30.0f,  82, true},
    };

    for (const Case& k : cases) {
        std::vector<uint8_t> a(size_t(W) * H * 4, 250), b(size_t(W) * H * 4, 250);
        aino::Canvas ca; ca.px = a.data(); ca.w = W; ca.h = H; ca.stride = W * 4;
        aino::Canvas cb; cb.px = b.data(); cb.w = W; cb.h = H; cb.stride = W * 4;
        if (k.clip) { ca.set_clip(40, 30, 900, 600); cb.set_clip(40, 30, 900, 600); }
        const aino::Rect r(k.x, k.y, k.w, k.h);
        const aino::Color c(0, 0, 0, uint8_t(k.alpha));

        shadow_reference(ca, r, k.rad, k.blur, c, k.dy);
        aino::detail::raster_shadow(cb, r, k.rad, k.blur, c, k.dy);

        int maxd = 0;
        size_t ndiff = 0, total = 0;
        for (size_t i = 0; i < a.size(); i += 4)
            for (int ch = 0; ch < 3; ++ch) {
                ++total;
                const int d = std::abs(int(a[i + ch]) - int(b[i + ch]));
                if (d) ++ndiff;
                if (d > maxd) maxd = d;
            }
        const double ratio = double(ndiff) / double(total);
        char detail[192];
        std::snprintf(detail, sizeof detail, "(%s: 最大差 %d，差异通道 %zu/%zu = %.4f%%)",
                      k.name, maxd, ndiff, total, ratio * 100.0);
        check(maxd <= 1 && ratio < 0.0001, "掩膜路径与逐像素直算等价", detail);
        std::printf("       %s\n", detail);
    }

    // 同一块掩膜第二次起应当只剩合成成本。
    {
        std::vector<uint8_t> px(size_t(W) * H * 4, 250);
        aino::Canvas cv; cv.px = px.data(); cv.w = W; cv.h = H; cv.stride = W * 4;
        const aino::Rect r((W - 480) * 0.5f, (H - 300) * 0.5f, 480, 300);
        const aino::Color c(0, 0, 0, 82);
        aino::detail::raster_shadow(cv, r, 12.0f, 90.0f, c, 30.0f);   // 建掩膜
        const double t0 = now_ms();
        for (int i = 0; i < 50; ++i) aino::detail::raster_shadow(cv, r, 12.0f, 90.0f, c, 30.0f);
        const double cached = (now_ms() - t0) / 50.0;
        const double t1 = now_ms();
        for (int i = 0; i < 10; ++i) shadow_reference(cv, r, 12.0f, 90.0f, c, 30.0f);
        const double direct = (now_ms() - t1) / 10.0;
        std::printf("[INFO] 模态 480x300 blur90：直算 %.3f ms → 掩膜合成 %.3f ms（%.1f×）\n",
                    direct, cached, direct / cached);
        check(cached < direct, "掩膜合成比逐像素直算快");
    }

    if (failures) { std::printf("%d 项失败\n", failures); return 1; }
    std::printf("全部通过\n");
    return 0;
}
