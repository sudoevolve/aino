// 布局器数值验证 —— 不开窗口，纯算术，逐一比对期望矩形
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cmath>
#include <cstdio>

using aino::Rect;
using aino::Stack;

static int g_pass = 0, g_fail = 0;

static bool approx(float a, float b) { return std::fabs(a - b) < 0.01f; }

static void chk(const char* name, const Rect& r, float x, float y, float w, float h) {
    if (approx(r.x, x) && approx(r.y, y) && approx(r.w, w) && approx(r.h, h)) { ++g_pass; return; }
    ++g_fail;
    std::printf("[FAIL] %-38s got (%.2f,%.2f,%.2f,%.2f)  want (%.2f,%.2f,%.2f,%.2f)\n",
                name, r.x, r.y, r.w, r.h, x, y, w, h);
}

int main() {
    // ---- T1 纵向顺序 + 元素间 gap ----
    {
        Stack s(nullptr, Rect(0, 0, 100, 200), true, 8.0f);
        chk("T1 next(20) 第一块",   s.next(20.0f), 0, 0, 100, 20);
        chk("T1 next(20) 第二块",   s.next(20.0f), 0, 28, 100, 20);
        chk("T1 fill 吃掉剩余",     s.fill(),      0, 56, 100, 144);
    }

    // ---- T2 横向等分（part 不改游标）----
    {
        Stack s(nullptr, Rect(0, 0, 220, 36), false, 12.0f);
        chk("T2 part(0,2) 左半",  s.part(0, 2), 0, 0, 104, 36);
        chk("T2 part(1,2) 右半",  s.part(1, 2), 116, 0, 104, 36);
        chk("T2 part 不动游标",   s.rest(),     0, 0, 220, 36);
        chk("T2 part(0,3) 三等分", s.part(0, 3), 0, 0, 65.333f, 36);
        chk("T2 part(2,3) 三等分", s.part(2, 3), 154.667f, 0, 65.333f, 36);
    }

    // ---- T3 复现 demo 的三栏（对照组：手算 cx0/cx1/cx2 = 32/336/640）----
    {
        Stack s(nullptr, Rect(32, 0, 908, 100), false, 24.0f);
        chk("T3 第一栏 == 手算 cx0=32", s.next(280.0f), 32, 0, 280, 100);
        chk("T3 第二栏 == 手算 cx1=336", s.next(280.0f), 336, 0, 280, 100);
        chk("T3 第三栏 == 手算 cx2=640", s.next(280.0f), 640, 0, 280, 100);
        // 宽度不够时必须收窄而不是越界
        Stack t(nullptr, Rect(32, 0, 876, 100), false, 24.0f);
        t.next(280.0f); t.next(280.0f);
        chk("T3 容器不足时第三栏收窄", t.next(280.0f), 640, 0, 268, 100);
    }

    // ---- T4 fill 不会白吞一个 gap（只有两个元素，中间才有一个间距）----
    {
        Stack s(nullptr, Rect(0, 0, 100, 100), true, 10.0f);
        chk("T4 next(30)", s.next(30.0f), 0, 0, 100, 30);
        chk("T4 fill 起始应在 40 而非 50", s.fill(), 0, 40, 100, 60);
    }

    // ---- T5 溢出保护：超出剩余则收窄，不越界 ----
    {
        Stack s(nullptr, Rect(0, 0, 100, 100), true, 0.0f);
        chk("T5 next(200) 收窄到可用高", s.next(200.0f), 0, 0, 100, 100);
        chk("T5 之后剩余为 0",           s.next(50.0f),  0, 100, 100, 0);
    }

    // ---- T6 嵌套：切一条横栏再三分 ----
    {
        Stack col(nullptr, Rect(0, 0, 300, 200), true, 10.0f);
        Stack hb = col.hbox(40.0f, 8.0f);
        chk("T6 子布局 part(0,3)", hb.part(0, 3), 0, 0, 94.667f, 40);
        chk("T6 子布局 part(1,3)", hb.part(1, 3), 102.667f, 0, 94.667f, 40);
        chk("T6 子布局 part(2,3)", hb.part(2, 3), 205.333f, 0, 94.667f, 40);
        chk("T6 父布局剩余",       col.rest(),    0, 50, 300, 150);
    }

    // ---- T7 skip 只推进游标 ----
    {
        Stack s(nullptr, Rect(0, 0, 100, 100), true, 0.0f);
        chk("T7 next(20)",  s.next(20.0f), 0, 0, 100, 20);
        s.skip(30.0f);
        chk("T7 skip(30) 后", s.next(10.0f), 0, 50, 100, 10);
    }

    // ---- T8 非法输入安全 ----
    {
        Stack s(nullptr, Rect(0, 0, 100, 100), true, 5.0f);
        chk("T8 next(-5) 归零", s.next(-5.0f), 0, 0, 100, 0);
        chk("T8 part(5,3) 越界返回空", s.part(5, 3), 0, 0, 0, 0);
        chk("T8 part(0,0) 返回空",     s.part(0, 0), 0, 0, 0, 0);
    }

    // ---- T9 next_frac ----
    {
        Stack s(nullptr, Rect(0, 0, 400, 100), false, 0.0f);
        chk("T9 next_frac(0.25)", s.next_frac(0.25f), 0, 0, 100, 100);
        chk("T9 next_frac(1/3)",  s.next_frac(1.0f / 3.0f), 100, 0, 100, 100);
    }

    std::printf("\n通过 %d 项，失败 %d 项\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
