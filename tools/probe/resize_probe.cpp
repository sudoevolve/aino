// 缩放（WM_SIZE）后的"缓冲一致性"断言。
//
// 要抓的 bug：拖边框缩放时窗口整片变黑。
// 成因是**后备缓冲被重建了，但没有任何人渲染它**：
//   拖边框走的是 DefWindowProc 的非客户区逻辑，user32 会进一个模态循环
//   （WM_ENTERSIZEMOVE … WM_EXITSIZEMOVE），我们自己的 run() 循环这期间一行都跑不到；
//   而 WM_SIZE 新建的 DIB 是**全零**的，紧接着的 WM_PAINT 把它整块 BitBlt 上屏 →
//   屏幕上就是纯黑，一直黑到松手。
//
// 这个探针不去复现"屏幕上看到黑"（模态循环没法可靠脚本化），而是直接钉住底层不变量：
//   **每次尺寸变化之后，后备缓冲里必须已经是"按新尺寸渲染过的画面"**。
// 这条成立，就不可能贴出黑块；不成立，就一定会贴。
//
// 判据两条，缺一不可：
//   ① 画面不是黑的（黑像素占比 < 2%）—— 排除"贴了没画过的缓冲"；
//   ② 右边缘那条竖条出现在**新的**宽度上 —— 排除"只是把旧画面拉伸"。
//
// 最后还有一个反面参照：手工把缓冲清零，同一套判据必须报错 ——
// 否则说明断言本身是瞎的，测不出东西。
#include <windows.h>
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace aino;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* what, const char* detail = "") {
    if (ok) { ++g_pass; std::printf("  [ok  ] %s %s\n", what, detail); }
    else    { ++g_fail; std::printf("  [FAIL] %s %s\n", what, detail); }
}

static const wchar_t* kTitle = L"aino-resize";

struct Frame {
    std::vector<uint8_t> px;   // BGRA
    int w = 0, h = 0;
};

// 直接读**后备缓冲本身**，不走 PrintWindow。
//
// 这条很关键：一开始我用 PrintWindow 抓，结果修复前后都"通过" ——
// 因为 PrintWindow 内部会先把窗口刷一遍，顺带把 WM_PAINT 也处理了，
// 于是读到的永远是"已经补过帧"的画面，正好把要抓的 bug 盖掉了。
// 读 DIB 的 bits 才是"下一帧上屏时会贴出去的那块内存"。
static bool grab(detail::Win32State* st, Frame& f) {
    if (!st || !st->has_backbuffer || !st->bits) return false;
    if (st->pw <= 0 || st->ph <= 0) return false;
    f.w = st->pw;
    f.h = st->ph;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(st->bits);
    f.px.assign(p, p + size_t(f.w) * size_t(f.h) * 4u);
    return true;
}

static const uint8_t* at(const Frame& f, int x, int y) {
    return &f.px[(size_t(y) * size_t(f.w) + size_t(x)) * 4];
}

// 近黑像素占比（三通道都 < 16 才算黑）
static double black_ratio(const Frame& f) {
    size_t n = 0;
    for (size_t i = 0; i < size_t(f.w) * size_t(f.h); ++i) {
        const uint8_t* p = &f.px[i * 4];
        if (p[0] < 16 && p[1] < 16 && p[2] < 16) ++n;
    }
    return double(n) / double(size_t(f.w) * size_t(f.h));
}

// 画面里是否有一条"主色竖条"落在右边缘 w-40..w 内（回调里画的那条）
static bool right_stripe_ok(const Frame& f) {
    if (f.w < 60 || f.h < 40) return false;
    const uint8_t* p = at(f, f.w - 20, f.h / 2);
    // 主色 #2563EB：蓝 235、红 37
    return p[detail::CH_B] > 180 && p[detail::CH_R] < 90;
}

int main() {
    std::printf("== 缩放后备缓冲一致性断言 ==\n");

    Window win(700, 480, "aino-resize");
    if (!win.ok()) { std::printf("window failed\n"); return 1; }
    HWND h = FindWindowW(nullptr, kTitle);
    auto* st = reinterpret_cast<detail::Win32State*>(win.impl->native);
    if (!h || !st) { std::printf("hwnd/state failed\n"); return 1; }

    // 假装自己已经在 run() 里 —— run() 就是绑这两个回调：一个出帧、一个被消息路径回调。
    win.impl->render_fn = [&win]() { win.render(); };

    // 帧回调：整窗铺底色 + 右边缘一条主色竖条。
    // 竖条的存在与否直接反映"有没有按当前宽度重排" —— 只拉伸旧画面的话它还在老位置上。
    win.impl->frame_cb_ptr = [&win]() {
        Ui& ui = win.impl->ui;
        ui.clear(ui.theme().background);
        ui.fill_rect(Rect(ui.width() - 40.0f, 0.0f, 40.0f, ui.height()),
                     Color::hex(0x2563EB), 0.0f);
    };

    // 首帧
    win.render();

    {
        Frame f;
        if (!grab(st, f)) { std::printf("首帧抓取失败\n"); return 1; }
        char d[128];
        std::snprintf(d, sizeof(d), "(客户区 %dx%d，黑像素 %.1f%%)", f.w, f.h, black_ratio(f) * 100.0);
        check(black_ratio(f) < 0.02, "首帧不是黑的", d);
    }

    // ---- 连续改尺寸：每次改完立刻读缓冲，必须是"按新尺寸画过的" ----
    const int sizes[][2] = {
        { 860, 600 }, { 640, 420 }, { 1000, 700 }, { 560, 380 }, { 780, 520 },
    };
    int black_frames = 0, stripe_bad = 0, size_bad = 0;
    for (const auto& s : sizes) {
        SetWindowPos(h, nullptr, 0, 0, s[0], s[1],
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        RECT cr;
        GetClientRect(h, &cr);
        Frame f;
        if (!grab(st, f)) { ++black_frames; continue; }
        const double br = black_ratio(f);
        const bool size_ok = (f.w == cr.right - cr.left && f.h == cr.bottom - cr.top);
        if (br >= 0.02) ++black_frames;
        if (!right_stripe_ok(f)) ++stripe_bad;
        if (!size_ok) ++size_bad;
        char d[160];
        std::snprintf(d, sizeof(d), "(-> %dx%d，缓冲 %dx%d，黑像素 %.1f%%)",
                      s[0], s[1], f.w, f.h, br * 100.0);
        check(br < 0.02, "改尺寸后不是黑帧", d);
        char d2[128];
        std::snprintf(d2, sizeof(d2), "(右边缘竖条 %s)", right_stripe_ok(f) ? "在" : "不在");
        check(right_stripe_ok(f), "按新尺寸重新排过（不是拉伸旧画面）", d2);
        char d3[160];
        std::snprintf(d3, sizeof(d3), "(缓冲 %dx%d，客户区 %ldx%ld)", f.w, f.h,
                      long(cr.right - cr.left), long(cr.bottom - cr.top));
        check(size_ok, "缓冲尺寸 = 客户区尺寸", d3);
    }
    {
        char d[128];
        std::snprintf(d, sizeof(d), "(%d 次改尺寸，黑帧 %d，竖条缺失 %d，尺寸不符 %d)",
                      int(sizeof(sizes) / sizeof(sizes[0])), black_frames, stripe_bad, size_bad);
        check(black_frames == 0 && stripe_bad == 0 && size_bad == 0,
              "连续缩放全程无黑帧、无拉伸残留", d);
    }

    // ---- 反面参照：把缓冲清零，同一套判据必须报"黑" ----
    {
        if (st && st->bits) {
            std::memset(st->bits, 0, size_t(st->pw) * size_t(st->ph) * 4u);
            Frame f;
            if (grab(st, f)) {
                char d[128];
                std::snprintf(d, sizeof(d), "(清零后黑像素 %.1f%%)", black_ratio(f) * 100.0);
                check(black_ratio(f) > 0.9, "反面参照：清零的缓冲确实被判为黑帧", d);
            } else {
                check(false, "反面参照：清零后抓帧失败", "");
            }
            win.render();   // 恢复
        } else {
            check(false, "反面参照：拿不到后备缓冲", "");
        }
    }

    // ---- 逻辑尺寸同步 ----
    {
        char d[128];
        std::snprintf(d, sizeof(d), "(window %dx%d, dpi %.2f)", win.width(), win.height(),
                      double(win.dpi_scale()));
        check(win.width() > 0 && win.height() > 0, "缩放后 width()/height() 同步更新", d);
    }

    std::printf("\n通过 %d 项，失败 %d 项\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
