// 字体 A/B 台架：把同一段文字用不同「字体族 / AA 质量」画进内存 DIB，
// 逐像素比对。用来回答两个问题：
//   1. 换成 Segoe UI 之后，**中文**字形有没有变（GDI 字体链接是否落到同一个 CJK 字体）
//   2. 与雅黑相比，**拉丁**字形差多少（这是"看起来不是系统字体"的来源）
//
// 编译：g++ -std=c++17 -O2 .workbuddy/probe/font_ab.cpp -o .workbuddy/probe/font_ab.exe -lgdi32 -luser32

#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

const int kW = 560, kH = 44;
const int kPx = 21;                 // 14 逻辑 × DPI 1.5

struct Img {
    std::vector<unsigned char> px;  // BGRA，白底
    int w = kW, h = kH;
};

Img render(const wchar_t* face, BYTE quality, const wchar_t* text) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kW;
    bi.bmiHeader.biHeight = -kH;    // 自顶向下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC dc = CreateCompatibleDC(screen);
    HGDIOBJ oldb = SelectObject(dc, bmp);

    // 白底
    RECT r{0, 0, kW, kH};
    HBRUSH br = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &r, br);
    DeleteObject(br);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    HFONT f = CreateFontW(-kPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                          quality, DEFAULT_PITCH | FF_DONTCARE, face);
    HGDIOBJ oldf = SelectObject(dc, f);
    TextOutW(dc, 8, 8, text, int(wcslen(text)));

    Img img;
    img.px.assign(static_cast<unsigned char*>(bits),
                  static_cast<unsigned char*>(bits) + size_t(kW) * kH * 4);

    SelectObject(dc, oldf);
    DeleteObject(f);
    SelectObject(dc, oldb);
    DeleteDC(dc);
    DeleteObject(bmp);
    ReleaseDC(nullptr, screen);
    return img;
}

void diff(const char* tag, const Img& a, const Img& b) {    int mx = 0;
    double sum = 0;
    int n = 0, moved = 0;
    for (size_t i = 0; i < a.px.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            const int d = std::abs(int(a.px[i + c]) - int(b.px[i + c]));
            if (d > mx) mx = d;
            sum += d;
            ++n;
            if (d > 24) ++moved;
        }
    }
    std::printf("  %-34s 最大差 %3d  平均差 %6.2f  明显不同的通道 %5.2f%%\n",
                tag, mx, sum / n, 100.0 * moved / n);
}

// 存成 PPM（无依赖），方便人眼核对——像素比对再准，也得看一眼确认不是换了个更差的字体。
void save_ppm(const char* path, const Img& img) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", img.w, img.h);
    for (size_t i = 0; i < img.px.size(); i += 4) {
        const unsigned char rgb[3] = { img.px[i + 2], img.px[i + 1], img.px[i] };
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

} // namespace

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const wchar_t* cjk   = L"组件画廊单头文件零第三方依赖";
    const wchar_t* latin = L"Gallery Collection 0123456789 aAgG";

    struct Cfg { const wchar_t* face; BYTE q; const char* name; };
    const Cfg cfgs[] = {
        { L"Microsoft YaHei UI",     ANTIALIASED_QUALITY, "YaHeiUI/gray" },
        { L"Segoe UI Variable Text", ANTIALIASED_QUALITY, "SegoeVar/gray" },
        { L"Segoe UI",               ANTIALIASED_QUALITY, "SegoeUI/gray"  },
        { L"Microsoft YaHei UI",     CLEARTYPE_QUALITY,   "YaHeiUI/CT"    },
        { L"Segoe UI Variable Text", CLEARTYPE_QUALITY,   "SegoeVar/CT"   },
    };
    const int n = int(sizeof(cfgs) / sizeof(cfgs[0]));

    std::printf("== 中文串：和「雅黑/灰度」逐像素比 ==\n");
    Img base_cjk = render(cfgs[0].face, cfgs[0].q, cjk);
    for (int i = 1; i < n; ++i)
        diff(cfgs[i].name, base_cjk, render(cfgs[i].face, cfgs[i].q, cjk));

    std::printf("\n== 拉丁串：和「雅黑/灰度」逐像素比 ==\n");
    Img base_lat = render(cfgs[0].face, cfgs[0].q, latin);
    for (int i = 1; i < n; ++i)
        diff(cfgs[i].name, base_lat, render(cfgs[i].face, cfgs[i].q, latin));

    // 落盘供人眼核对
    save_ppm(".workbuddy/probe/font_a_yahai.ppm", base_cjk);
    save_ppm(".workbuddy/probe/font_b_segoe.ppm",
             render(L"Segoe UI Variable Text", ANTIALIASED_QUALITY, cjk));

    // 宽度差：字体族换了以后排版一定会挪，这是必须知道的
    std::printf("\n== 同字号下的文本宽度（px @21）==\n");
    HDC dc = CreateCompatibleDC(nullptr);
    for (int i = 0; i < n; ++i) {
        HFONT f = CreateFontW(-kPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                              cfgs[i].q, DEFAULT_PITCH | FF_DONTCARE, cfgs[i].face);
        HGDIOBJ o = SelectObject(dc, f);
        SIZE sc{}, sl{};
        GetTextExtentPoint32W(dc, cjk, int(wcslen(cjk)), &sc);
        GetTextExtentPoint32W(dc, latin, int(wcslen(latin)), &sl);
        std::printf("  %-14s 中文 %4d  拉丁 %4d\n", cfgs[i].name, int(sc.cx), int(sl.cx));
        SelectObject(dc, o);
        DeleteObject(f);
    }
    DeleteDC(dc);
    return 0;
}
