// 图标掩膜的行合成与逐像素 blend 的等价性检查。
//
// `system_icon` 由"每个图标 push_clip + 逐像素 blend"改成"把裁剪算成一段区间后
// 整行合成"。算式与 `Canvas::blend` 逐项同序，因此要求**逐字节相同**（不是 ≤1）。
// 覆盖：正常位置、左上越界、右下越界、零偏移，并带一层裁剪区。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cstdio>
#include <chrono>
#include <vector>

int main() {
    const int W = 200, H = 120;
    std::vector<uint8_t> a(size_t(W) * H * 4, 240), b(size_t(W) * H * 4, 240);
    aino::Canvas ca; ca.px = a.data(); ca.w = W; ca.h = H; ca.stride = W * 4;
    aino::Canvas cb; cb.px = b.data(); cb.w = W; cb.h = H; cb.stride = W * 4;
    ca.set_clip(30, 20, 150, 100);
    cb.set_clip(30, 20, 150, 100);

    const int S = 24;
    std::vector<uint8_t> mask(size_t(S) * S);
    for (int i = 0; i < S * S; ++i) mask[size_t(i)] = uint8_t((i * 37) & 0xFF);

    const aino::Color color(20, 40, 60, 217);
    const int pos[][2] = {{40, 30}, {-8, -6}, {140, 90}, {0, 0}};
    for (const auto& p : pos) {
        const int left = p[0], top = p[1];
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x)
                ca.blend(left + x, top + y, color, mask[size_t(y) * S + x] / 255.f);

        int x0 = left, x1 = left + S, y0 = top, y1 = top + S;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > cb.w) x1 = cb.w;
        if (y1 > cb.h) y1 = cb.h;
        if (x0 < cb.clip_x0) x0 = cb.clip_x0;
        if (y0 < cb.clip_y0) y0 = cb.clip_y0;
        if (x1 > cb.clip_x1) x1 = cb.clip_x1;
        if (y1 > cb.clip_y1) y1 = cb.clip_y1;
        if (x1 > x0 && y1 > y0)
            for (int y = y0; y < y1; ++y)
                cb.blend_mask_row8(y, x0, x1, color, mask.data() + size_t(y - top) * S + (x0 - left));
    }

    int maxd = 0;
    size_t ndiff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const int d = std::abs(int(a[i]) - int(b[i]));
        if (d) ++ndiff;
        if (d > maxd) maxd = d;
    }
    std::printf("[%s] 逐字节比较：最大差 %d，差异字节 %zu / %zu\n",
                (maxd == 0 ? " OK " : "FAIL"), maxd, ndiff, a.size());

    // 单图标合成本身（24×24）
    auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < 2000; ++k)
        for (int y = 40; y < 64; ++y)
            cb.blend_mask_row8(y, 40, 64, color, mask.data() + size_t(y - 40) * S);
    auto t1 = std::chrono::steady_clock::now();
    auto o0 = std::chrono::steady_clock::now();
    for (int k = 0; k < 2000; ++k)
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x)
                ca.blend(40 + x, 40 + y, color, mask[size_t(y) * S + x] / 255.f);
    auto o1 = std::chrono::steady_clock::now();
    const double neu = double(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()) / 2000;
    const double old = double(std::chrono::duration_cast<std::chrono::microseconds>(o1 - o0).count()) / 2000;
    std::printf("[INFO] 单图标 24x24：逐像素 %.2f µs → 整行 %.2f µs（%.1f×）\n", old, neu, old / neu);

    if (maxd) { std::printf("失败\n"); return 1; }
    std::printf("全部通过\n");
    return 0;
}
