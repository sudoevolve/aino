// 量 Sleep 的实际周期，用来解释 aino 动画节流「按 anim_fps 算该 60fps，实测只有 30~36fps」。
//
// aino 的节流是 Sleep(anim_budget_ms - 本帧渲染耗时)。如果 Sleep(9) 实际睡 ~15.6ms，
// 那就是进程的**定时器粒度**问题（没调 timeBeginPeriod）—— 顺带把本进程的
// NtQueryTimerResolution 打出来，看它和从 Python 里查到的 1.000ms 是否一致。
#include <windows.h>
#include <cstdio>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ntdll.lib")

extern "C" __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int);
extern "C" __declspec(dllimport) long __stdcall NtQueryTimerResolution(
    unsigned long*, unsigned long*, unsigned long*);

static void timer_resolution(const char* who) {
    unsigned long mn = 0, mx = 0, cur = 0;
    if (NtQueryTimerResolution(&mn, &mx, &cur) == 0) {
        std::printf("%s NtQueryTimerResolution: 最小 %.3f ms  最大 %.3f ms  当前 %.3f ms\n",
                    who, mn / 10000.0, mx / 10000.0, cur / 10000.0);
    }
}

static double period_ms(int ms, int n) {
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        QueryPerformanceCounter(&t0);
        Sleep(DWORD(ms));
        QueryPerformanceCounter(&t1);
        sum += double(t1.QuadPart - t0.QuadPart) / double(f.QuadPart) * 1000.0;
    }
    return sum / n;
}

int main() {
    timer_resolution("默认");

    // 模拟 aino 的节流：预算 16ms，本帧渲染 6.4ms → Sleep(9)
    const int ms[] = {5, 9, 10, 16, 17, 33};
    for (int i = 0; i < 6; ++i)
        std::printf("Sleep(%2d ms) -> 实际平均 %.2f ms（24 次）\n", ms[i], period_ms(ms[i], 24));

    // 再试一次：本进程把定时器精度提到 1ms 之后，结果是否变好？
    if (timeBeginPeriod(1) == 0) {
        timer_resolution("timeBeginPeriod(1) 之后");
        std::printf("Sleep( 9 ms) -> 实际平均 %.2f ms\n", period_ms(9, 24));
        std::printf("Sleep(16 ms) -> 实际平均 %.2f ms\n", period_ms(16, 24));
    }
    return 0;
}
