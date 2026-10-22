// WindowConfig —— 配置项是不是真的落到了系统上
//
// 这些没法靠像素断言看出来（图标、最小尺寸、居中都是**窗口管理器**的行为），
// 所以直接问系统：SetWindowPos 拉小之后客户区被夹到多少、窗口矩形中心在哪、
// 样式位里还有没有 WS_THICKFRAME。
//
// 字体那条是纯解析：WindowConfig::font_path 只给文件路径，库自己从 sfnt 的
// `name` 表里读家族名（GetFontResourceInfoW 只有 MSVC 头文件里有，mingw 编不过）。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <cstdio>
#include <string>

int g_pass = 0, g_fail = 0;

static void check(bool ok, const char* what, const std::string& detail = std::string()) {
    if (ok) { ++g_pass; }
    else    { ++g_fail; std::printf("  [FAIL] %s  %s\n", what, detail.c_str()); }
}

// ---------------------------------------------------------------- 1. 字体家族名
static void test_font_name() {
    std::printf("[1] 从字体文件读家族名\n");
    char windir[MAX_PATH] = {};
    GetWindowsDirectoryA(windir, MAX_PATH);

    struct Case { const char* file; const char* want; };
    const Case cases[] = {
        {"\\Fonts\\consola.ttf", "Consolas"},
        {"\\Fonts\\segoeui.ttf", "Segoe UI"},
        {"\\Fonts\\arial.ttf",  "Arial"},
    };
    for (const Case& c : cases) {
        const std::string p = std::string(windir) + c.file;
        if (GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::printf("  (跳过，文件不存在) %s\n", p.c_str());
            continue;
        }
        const std::string got = aino::detail::font_family_from_file(p);
        check(got == c.want, "家族名解析", p + " -> \"" + got + "\" 期望 \"" + c.want + "\"");
    }

    // 不是字体文件 → 空（不能崩，也不能返回垃圾）
    const std::string bogus = std::string(windir) + "\\notepad.exe";
    const std::string r = aino::detail::font_family_from_file(bogus);
    check(r.empty(), "非字体文件返回空", "-> \"" + r + "\"");
    check(aino::detail::font_family_from_file("").empty(), "空路径返回空");
    check(aino::detail::font_family_from_file("C:\\没有这个文件.ttf").empty(), "不存在的路径返回空");
}

// ---------------------------------------------------------------- 2. 尺寸约束
static void test_min_max() {
    std::printf("[2] 最小 / 最大尺寸\n");
    aino::WindowConfig cfg;
    cfg.width = 420; cfg.height = 300;
    cfg.min_width = 400; cfg.min_height = 280;
    cfg.max_width = 900; cfg.max_height = 700;
    cfg.center = false;
    aino::Window win(cfg);
    check(win.ok(), "窗口创建成功");
    if (!win.ok()) return;

    // native 指向后端的 Win32State，hwnd 在它里面（不是 native 本身）
    HWND hwnd = reinterpret_cast<aino::detail::Win32State*>(win.impl->native)->hwnd;
    RECT cr{};
    GetClientRect(hwnd, &cr);
    const float sc = win.dpi_scale();
    check(std::abs(cr.right - int(420 * sc)) <= 2, "初始尺寸 = 配置",
          std::to_string(cr.right) + " vs " + std::to_string(int(420 * sc)));

    // 拉小到 100×100：系统必须按 WM_GETMINMAXINFO 夹回 400×280（客户区）
    SetWindowPos(hwnd, nullptr, 0, 0, 100, 100, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    GetClientRect(hwnd, &cr);
    check(cr.right >= int(400 * sc) - 2 && cr.bottom >= int(280 * sc) - 2,
          "拖小于 min 被夹回", std::to_string(cr.right) + "x" + std::to_string(cr.bottom));

    // 拉大到 2000×1500：必须夹到 900×700
    SetWindowPos(hwnd, nullptr, 0, 0, 2000, 1500, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    GetClientRect(hwnd, &cr);
    check(cr.right <= int(900 * sc) + 2 && cr.bottom <= int(700 * sc) + 2,
          "拖大于 max 被夹回", std::to_string(cr.right) + "x" + std::to_string(cr.bottom));
    win.close();
}

// ---------------------------------------------------------------- 3. 居中
static void test_center() {
    std::printf("[3] 首次显示居中\n");
    aino::WindowConfig cfg;
    cfg.width = 400; cfg.height = 300;
    cfg.center = true;
    aino::Window win(cfg);
    check(win.ok(), "窗口创建成功");
    if (!win.ok()) return;

    HWND hwnd = reinterpret_cast<aino::detail::Win32State*>(win.impl->native)->hwnd;
    RECT wr{}, wa{};
    GetWindowRect(hwnd, &wr);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int dx = std::abs((wr.left + wr.right) / 2 - (wa.left + wa.right) / 2);
    const int dy = std::abs((wr.top + wr.bottom) / 2 - (wa.top + wa.bottom) / 2);
    check(dx <= 4 && dy <= 4, "窗口中心 = 工作区中心",
          "偏移 " + std::to_string(dx) + "," + std::to_string(dy));
    win.close();
}

// ---------------------------------------------------------------- 4. 不可缩放
static void test_not_resizable() {
    std::printf("[4] resizable=false 去掉厚边框\n");
    aino::WindowConfig cfg;
    cfg.width = 400; cfg.height = 300;
    cfg.resizable = false;
    aino::Window win(cfg);
    check(win.ok(), "窗口创建成功");
    if (!win.ok()) return;

    HWND hwnd = reinterpret_cast<aino::detail::Win32State*>(win.impl->native)->hwnd;
    const LONG st = GetWindowLongW(hwnd, GWL_STYLE);
    check((st & WS_THICKFRAME) == 0, "样式里没有 WS_THICKFRAME",
          "style=0x" + std::to_string(st));
    check((st & WS_MAXIMIZEBOX) == 0, "样式里没有 WS_MAXIMIZEBOX");
    check((st & WS_CAPTION) != 0, "标题栏还在");
    win.close();
}

int main() {
    test_font_name();
    test_min_max();
    test_center();
    test_not_resizable();
    std::printf("=== 通过 %d 项，失败 %d 项 ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
