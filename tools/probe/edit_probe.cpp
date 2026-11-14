// 真实窗口下的编辑器链路：按键消息 → input/textarea → 撤销/重做/选区。
//
// 为什么要开窗口：bigfeat_probe 只测了 detail::edit_undo 这些函数，测不到
// 「WM_CHAR 进来之后控件到底有没有调它、有没有被 !undone 之类的守卫挡掉」。
// 打开一个 520×300 的窗口，用 PostMessage 投递真实按键，逐项回读字符串。
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

using namespace aino;

static const wchar_t* kTitleW = L"aino-edit-probe";
static const float kLogicalW = 520.0f, kLogicalH = 300.0f;
static const Rect kSingleBox(20, 40, 300, 24);
static const Rect kMultiBox(20, 126, 300, 120);

struct State {
    std::string single = "abc";
    std::string multi  = "line1\nline2";
};
static State g;
static std::mutex g_mu;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* what, const std::string& detail = std::string()) {
    if (ok) { ++g_pass; std::printf("  [OK  ] %s %s\n", what, detail.c_str()); }
    else    { ++g_fail; std::printf("  [FAIL] %s %s\n", what, detail.c_str()); }
}

static std::string snapshot_single() { std::lock_guard<std::mutex> l(g_mu); return g.single; }
static std::string snapshot_multi()  { std::lock_guard<std::mutex> l(g_mu); return g.multi; }

// 把 char 投递进窗口：WM_CHAR（走库的 text_input）
static void type_text(HWND h, const char* s) {
    for (const char* p = s; *p; ++p) {
        PostMessageW(h, WM_CHAR, WPARAM((unsigned char)*p), 1);
        Sleep(40);
    }
}

static void key(HWND h, int vk, bool ctrl = false, bool shift = false) {
    if (ctrl)  PostMessageW(h, WM_KEYDOWN, VK_CONTROL, 0);
    if (shift) PostMessageW(h, WM_KEYDOWN, VK_SHIFT, 0);
    PostMessageW(h, WM_KEYDOWN, WPARAM(vk), 1);
    Sleep(60);
    PostMessageW(h, WM_KEYUP, WPARAM(vk), 1);
    if (shift) PostMessageW(h, WM_KEYUP, VK_SHIFT, 1);
    if (ctrl)  PostMessageW(h, WM_KEYUP, VK_CONTROL, 1);
    Sleep(60);
}

int main() {
    Window win(int(kLogicalW), int(kLogicalH), "aino-edit-probe");
    if (!win.ok()) { std::printf("window failed\n"); return 1; }

    std::thread driver([&] {
        Sleep(900);
        HWND h = FindWindowW(nullptr, kTitleW);
        if (!h) { std::printf("no hwnd\n"); win.close(); return; }
        RECT cr{};
        GetClientRect(h, &cr);
        POINT org{0, 0};
        ClientToScreen(h, &org);
        const float sc = float(cr.right) / kLogicalW;
        auto click = [&](float lx, float ly) {
            PostMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(int(lx * sc), int(ly * sc)));
            Sleep(50);
            PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(int(lx * sc), int(ly * sc)));
            Sleep(40);
            PostMessageW(h, WM_LBUTTONUP, 0, MAKELPARAM(int(lx * sc), int(ly * sc)));
            Sleep(120);
        };
        (void)org;

        std::printf("== 单行 input ==\n");
        click(kSingleBox.cx(), kSingleBox.cy());                 // input 区域
        key(h, VK_END);
        type_text(h, "def");
        std::string v = snapshot_single();
        check(v == "abcdef", "输入追加到末尾", "(\"" + v + "\")");

        // 连续 3 个字符应该合成一步撤销
        key(h, 'Z', true);
        v = snapshot_single();
        check(v == "abc", "Ctrl+Z 一次退掉整组输入", "(\"" + v + "\")");

        key(h, 'Z', true);
        v = snapshot_single();
        check(v == "abc", "退到底之后再按也不变（不崩、不乱）", "(\"" + v + "\")");

        key(h, 'Y', true);
        v = snapshot_single();
        check(v == "abcdef", "Ctrl+Y 重做", "(\"" + v + "\")");

        // Shift+Home 选中整行，再输入 → 替换
        key(h, VK_HOME, false, true);
        type_text(h, "X");
        v = snapshot_single();
        check(v == "X", "Shift+Home 扩选后输入替换整行", "(\"" + v + "\")");

        key(h, 'Z', true);
        v = snapshot_single();
        check(v == "abcdef", "撤销回到替换前", "(\"" + v + "\")");

        type_text(h, "\t");
        check(snapshot_single() == "abcdef", "真实 WM_CHAR Tab 不进入单行输入");
        key(h, 'A', true);
        type_text(h, "\t");
        check(snapshot_single() == "abcdef", "真实 Tab 不删除单行选区");
        type_text(h, "name");
        check(snapshot_single() == "name", "忽略真实 Tab 后仍保留原选区");

        std::printf("== 多行 textarea ==\n");
        click(kMultiBox.cx(), kMultiBox.y + 40);                 // textarea 区域
        key(h, VK_END, true);                  // Ctrl+End 到文档末尾
        key(h, VK_RETURN);
        type_text(h, "tail");
        std::string m = snapshot_multi();
        check(m == "line1\nline2\ntail", "Enter + 输入追加到文档末尾", "(\"" + m + "\")");

        key(h, 'Z', true);
        m = snapshot_multi();
        check(m == "line1\nline2", "Ctrl+Z 退掉这一组输入", "(\"" + m + "\")");

        key(h, 'Y', true);
        m = snapshot_multi();
        check(m == "line1\nline2\ntail", "Ctrl+Y 重做", "(\"" + m + "\")");

        // 全选 + 删除
        key(h, 'A', true);
        key(h, VK_DELETE);
        m = snapshot_multi();
        check(m.empty(), "Ctrl+A / Delete 清空", "(\"" + m + "\")");

        key(h, 'Z', true);
        m = snapshot_multi();
        check(m == "line1\nline2\ntail", "撤销恢复整篇", "(\"" + m + "\")");

        // 小键盘 / 方向键不崩：连发 40 次
        for (int i = 0; i < 40; ++i) key(h, i % 2 ? VK_LEFT : VK_RIGHT, false, i % 3 == 0);
        m = snapshot_multi();
        check(m == "line1\nline2\ntail", "方向键乱按不改变文本", "(\"" + m + "\")");

        std::printf("\n通过 %d 项，失败 %d 项\n", g_pass, g_fail);
        win.close();
    });

    win.run([](Ui& ui) {
        std::lock_guard<std::mutex> l(g_mu);
        ui.clear(ui.theme().background);
        ui.label("单行 input", Rect(20, 14, 200, 20), Style().muted_text());
        ui.input(&g.single, kSingleBox);
        ui.label("多行 textarea", Rect(20, 100, 200, 20), Style().muted_text());
        ui.textarea(&g.multi, kMultiBox);
    });

    driver.join();
    std::printf("main loop exited\n");
    return g_fail == 0 ? 0 : 1;
}
