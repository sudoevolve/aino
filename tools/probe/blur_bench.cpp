// 背景模糊的纯计算耗时（不开窗口，直接对一块后备缓冲反复调用）。
// 目的：给"液态玻璃用模糊替身"这条取舍一个可复现的数字，而不是"感觉挺快"。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cstdio>
#include <chrono>

static double now_ms() {
    using namespace std::chrono;
    return double(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count()) / 1000.0;
}

int main() {
    const int W = 940, H = 760;
    std::vector<uint8_t> px(size_t(W) * H * 4, 0);
    // 填一张"有内容"的底：渐变 + 几条硬边（纯色底测不出降采样/放大的成本差异）
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            uint8_t* p = &px[(size_t(y) * W + x) * 4];
            p[0] = uint8_t((x * 255) / W); p[1] = uint8_t((y * 255) / H);
            p[2] = uint8_t(((x / 40 + y / 40) & 1) ? 200 : 60); p[3] = 255;
        }
    aino::Canvas cv; cv.px = px.data(); cv.w = W; cv.h = H; cv.stride = W * 4;

    struct Case { const char* name; float blur; float w, h; };
    const Case cases[] = {{"170x148 单块 frost 16", 16.0f, 170, 148},
                          {"170x148 单块 frost 6", 6.0f, 170, 148},
                          {"940x300 整条 frost 16", 16.0f, 900, 300},
                          {"940x300 整条 frost 24", 24.0f, 900, 300},
                          {"940x740 满窗 frost 16", 16.0f, 940, 740}};
    for (const Case& c : cases) {
        const aino::Rect r(20, 20, c.w, c.h);
        const int N = 60;
        // 预热一帧（thread_local 缓冲首次分配）
        aino::detail::blur_backdrop(cv, r, 12.0f, c.blur);
        const double t0 = now_ms();
        for (int i = 0; i < N; ++i) aino::detail::blur_backdrop(cv, r, 12.0f, c.blur);
        const double dt = (now_ms() - t0) / double(N);
        std::printf("%-24s %.3f ms/次  (%.0f x %.0f)\n", c.name, dt, r.w, r.h);
    }
    return 0;
}
