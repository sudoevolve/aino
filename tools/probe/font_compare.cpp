// 字体对照台架：同一段文字，用不同「字体族 + 渲染质量 + 字号」各画一行，
// 用来判断 aino 现在的字为什么看起来和系统 UI 不一样。
//
// 直接走 GDI（不经 aino），这样 QUALITY 标志是唯一的变量。
// 编译：g++ -std=c++17 -O2 -mwindows .workbuddy/probe/font_compare.cpp -o .workbuddy/probe/font_compare.exe -lgdi32 -luser32

#define _WIN32_WINNT 0x0A00
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <cstdio>
#include <string>

namespace {

struct Row {
    const wchar_t* face;
    BYTE           quality;
    int            px;
    const wchar_t* tag;
};

// 21px = aino 的 14 逻辑像素 × DPI 1.5（当前行为）
// 18px = 系统 lfMessageFont 的 -12px × 1.5（Windows UI 的实际字号）
// 本轮只比两件事：字体族（雅黑 vs Segoe）与 AA 质量。字号/质量保持常量。
const Row kRows[] = {
    { L"Microsoft YaHei UI",     ANTIALIASED_QUALITY, 21, L"A  YaHeiUI / GRAY      / 21px" },
    { L"Segoe UI Variable Text", ANTIALIASED_QUALITY, 21, L"B  SegoeUI-Variable-Text / GRAY / 21px" },
    { L"Segoe UI",               ANTIALIASED_QUALITY, 21, L"C  Segoe UI / GRAY / 21px" },
    { L"Microsoft YaHei UI",     CLEARTYPE_QUALITY,   21, L"D  YaHeiUI / CLEARTYPE / 21px" },
    { L"Segoe UI Variable Text", CLEARTYPE_QUALITY,   21, L"E  SegoeUI-Variable-Text / CLEARTYPE / 21px" },
};
const int kCount = int(sizeof(kRows) / sizeof(kRows[0]));

const wchar_t* kSample = L"组件画廊 Gallery 0123456789 aA";
const wchar_t* kSample2 = L"单头文件 · 零第三方依赖 · 空闲 CPU 0.00%";

void paint(HDC dc, int w, int h) {
    RECT all = { 0, 0, w, h };
    HBRUSH bg = CreateSolidBrush(RGB(250, 250, 250));
    FillRect(dc, &all, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);

    int y = 16;
    for (int i = 0; i < kCount; ++i) {
        const Row& r = kRows[i];

        // 小号灰字标签：固定用 12px YaHei，避免标签本身干扰对比
        HFONT lf = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                               ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                               L"Microsoft YaHei UI");
        HGDIOBJ old = SelectObject(dc, lf);
        SetTextColor(dc, RGB(120, 120, 128));
        TextOutW(dc, 16, y, r.tag, int(wcslen(r.tag)));
        y += 20;

        HFONT f = CreateFontW(-r.px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                              r.quality, DEFAULT_PITCH | FF_DONTCARE, r.face);
        SelectObject(dc, f);
        // 若字体族不存在，GDI 会静默替换 —— 这里把真实选中的字面名读回来标注
        wchar_t actual[64] = { 0 };
        GetTextFaceW(dc, 64, actual);
        SetTextColor(dc, RGB(24, 24, 27));
        TextOutW(dc, 16, y, kSample, int(wcslen(kSample)));
        y += r.px + 4;
        SetTextColor(dc, RGB(70, 70, 76));
        TextOutW(dc, 16, y, kSample2, int(wcslen(kSample2)));
        y += r.px + 2;

        if (wcscmp(actual, r.face) != 0) {
            SetTextColor(dc, RGB(190, 40, 40));
            wchar_t msg[128];
            swprintf(msg, 128, L"    ^ 实际落到: %s", actual);
            TextOutW(dc, 16, y, msg, int(wcslen(msg)));
            y += 16;
        }

        SelectObject(dc, old);
        DeleteObject(f);
        DeleteObject(lf);
        y += 14;
    }
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);
        paint(dc, cr.right, cr.bottom);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

} // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"AinoFontCompare";
    RegisterClassW(&wc);

    RECT rc = { 0, 0, 560, 470 };
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);
    HWND h = CreateWindowExW(0, wc.lpszClassName, L"font compare", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             nullptr, nullptr, inst, nullptr);
    if (!h) return 1;
    ShowWindow(h, SW_SHOW);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
