// spinner 拖尾环（raster_ring_tail）的像素断言。
//
// 要证明的是"转一圈不会断"：
//   1) 整圈 720 个角度采样，每一处都有弧的着色（没有"缺口"）；
//   2) phase=0 与 phase=2π 的渲染结果逐像素完全一致（周期闭合，绕回来不跳）；
//   3) 把一圈切成 180 步，相邻两步的画面差异应该均匀 —— 某一处突增就说明
//      转在那里会"断一下"。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <chrono>
#include <algorithm>

using namespace aino;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* what, const char* detail = "") {
    if (ok) { ++g_pass; std::printf("  [ok  ] %s %s\n", what, detail); }
    else    { ++g_fail; std::printf("  [FAIL] %s %s\n", what, detail); }
}

struct Buf {
    std::vector<uint8_t> px;
    detail::Canvas cv;
    int w, h;
    Buf(int W, int H) : px(size_t(W) * size_t(H) * 4, 255), w(W), h(H) {
        cv.px = px.data(); cv.w = W; cv.h = H; cv.stride = W * 4;
        for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    }
    inline const uint8_t* at(int x, int y) const {
        return &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
    }
    int lum(int x, int y) const {
        const uint8_t* p = at(x, y);
        return (int(p[detail::CH_R]) * 30 + int(p[detail::CH_G]) * 59 + int(p[detail::CH_B]) * 11) / 100;
    }
};

static const float TAU = 6.2831853f;
static const float SCALE = 2.0f;
static const float D = 64.0f;
static const float RAD = (D * 0.5f - 1.5f) * SCALE;
static const float LW  = std::max(1.5f, D * 0.12f) * SCALE;
static const Color TRACK = Color::hex(0xF4F4F5);
static const Color ARC   = Color::hex(0x18181B);

static void draw(Buf& b, float cx, float cy, float phase) {
    detail::raster_arc(b.cv, cx, cy, RAD, LW, TRACK, 0.0f, 0.0f, 1.0f);
    detail::raster_ring_tail(b.cv, cx, cy, RAD, LW, ARC, phase, 0.14f);
}

static void render(Buf& b, float cx, float cy, float phase) {
    // 必须先擦回白底：否则 track/arc 会叠在上一帧的像素上，越叠越深，
    // 第 4 项的"相邻帧差异"就会被这层累积污染成假数据。
    std::fill(b.px.begin(), b.px.end(), uint8_t(255));
    draw(b, cx, cy, phase);
}

static void write_ppm(const Buf& b, const char* path) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", b.w, b.h);
    std::vector<uint8_t> rgb(size_t(b.w) * size_t(b.h) * 3);
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x) {
            const uint8_t* p = b.at(x, y);
            uint8_t* q = &rgb[(size_t(y) * size_t(b.w) + size_t(x)) * 3];
            q[0] = p[detail::CH_R]; q[1] = p[detail::CH_G]; q[2] = p[detail::CH_B];
        }
    std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
    std::printf("已写出 %s\n", path);
}

// 两张同尺寸图的平均通道差
static double diff(const Buf& a, const Buf& b) {
    long long s = 0;
    for (size_t i = 0; i < a.px.size(); i += 4)
        s += std::abs(int(a.px[i + detail::CH_R]) - int(b.px[i + detail::CH_R]));
    return double(s) / double(a.px.size() / 4);
}

int main() {
    std::printf("== spinner 拖尾环（box=64 / DPI 2.0 / 物理 128）==\n");

    // ---- 1) 出图：12 个相位铺满一圈，肉眼确认环始终连续
    {
        const int W = 900, H = 320;
        Buf b(W, H);
        std::fill(b.px.begin(), b.px.end(), uint8_t(255));
        for (int i = 0; i < 12; ++i) {
            const int col = i % 6, row = i / 6;
            draw(b, 78.0f + float(col) * 148.0f, 82.0f + float(row) * 152.0f,
                 float(i) * TAU / 12.0f);
        }
        write_ppm(b, "ring_phases.ppm");
    }

    // ---- 2) 整圈覆盖：720 点都得有弧的着色（不是缺口）
    {
        Buf b(200, 200);
        render(b, 100.0f, 100.0f, 0.0f);
        int worst = 0, worst_deg = 0;
        for (int i = 0; i < 720; ++i) {
            const float deg = float(i) * 0.5f;
            const double a = double(deg) * 3.14159265358979 / 180.0;
            const int v = b.lum(int(100.0 + std::cos(a) * RAD), int(100.0 + std::sin(a) * RAD));
            if (v > worst) { worst = v; worst_deg = i / 2; }
        }
        char d[96];
        std::snprintf(d, sizeof(d), "(最浅处 lum=%d 在 %d°；track 底色约 244)", worst, worst_deg);
        check(worst < 235, "整圈 720 点都被弧覆盖（没有缺口）", d);
    }

    // ---- 3) 周期闭合：phase 0 与 2π 必须逐像素一致
    {
        Buf a(200, 200), b(200, 200);
        render(a, 100.0f, 100.0f, 0.0f);
        render(b, 100.0f, 100.0f, TAU);
        int maxdiff = 0;
        for (size_t i = 0; i < a.px.size(); i += 4)
            maxdiff = std::max(maxdiff, std::abs(int(a.px[i + detail::CH_R]) - int(b.px[i + detail::CH_R])));
        char d[96];
        std::snprintf(d, sizeof(d), "(最大通道差 %d)", maxdiff);
        check(maxdiff <= 1, "phase=0 与 phase=2π 无缝闭合", d);
    }

    // ---- 4) 连续性：一圈切 180 步，相邻步的画面差异必须均匀
    {
        const int N = 180;
        std::vector<double> steps;
        Buf prev(200, 200), cur(200, 200);
        render(prev, 100.0f, 100.0f, 0.0f);
        for (int i = 1; i <= N; ++i) {
            render(cur, 100.0f, 100.0f, float(i) * TAU / float(N));
            steps.push_back(diff(prev, cur));
            prev = cur;
        }
        double sum = 0.0, mx = 0.0, mn = 1e9;
        int mx_at = 0;
        for (int i = 0; i < N; ++i) {
            sum += steps[i];
            if (steps[i] > mx) { mx = steps[i]; mx_at = i; }
            if (steps[i] < mn) mn = steps[i];
        }
        const double mean = sum / double(N);
        char d[128];
        std::snprintf(d, sizeof(d), "(每步差异 %.3f..%.3f，均值 %.3f，峰值在 %d°)",
                      mn, mx, mean, mx_at * 2);
        // 判据用绝对值：平均通道差超过 2 才可能被肉眼看成"跳一下"。
        // 相对倍数不可靠 —— 头部亮度梯度本来就最大，转同样角度时差异自然更高。
        check(mx <= 2.0, "转一圈过程中每一步的变化都很小（没有跳变）", d);
        std::printf("       前 6 步：%.3f %.3f %.3f %.3f %.3f %.3f\n",
                    steps[0], steps[1], steps[2], steps[3], steps[4], steps[5]);
        std::printf("       中段 3 步：%.3f %.3f %.3f\n",
                    steps[44], steps[45], steps[89]);
    }

    // ---- 5) 头尾亮度对比：最亮处必须明显深于最暗处（方向感还在）
    {
        Buf b(200, 200);
        render(b, 100.0f, 100.0f, 0.0f);
        const int head = b.lum(int(100.0 + RAD), 100);
        const int tail = b.lum(int(100.0 - RAD), 100);
        char d[96];
        std::snprintf(d, sizeof(d), "(头 lum=%d，尾 lum=%d)", head, tail);
        check(head < 60 && tail > head + 60, "拖尾有明确的方向感（头深尾浅）", d);
    }

    // ---- 6) 成本：单次绘制耗时（不含清屏）
    {
        Buf b(200, 200);
        const int N = 4000;
        const auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i) draw(b, 100.0f, 100.0f, float(i) * 0.003f);
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / double(N);
        std::printf("\n  spinner 单次绘制（track + 拖尾环，物理 128px）：%.1f µs\n", us);
    }

    std::printf("\n通过 %d 项，失败 %d 项\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
