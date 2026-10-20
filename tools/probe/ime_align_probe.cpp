// 诊断：候选窗/组合窗到底被摆在哪儿，跟我们报的位置差多少。
//
// 用户截图：输入框里草稿正常，可候选框（1 啊啊啊 2 嗷嗷嗷…）既偏左又偏低，
// 压到下面那个输入框上。库里报的是 (caret_x, box.bottom() + 4) 物理客户区像素。
// 这个探针边打字边采样，把本进程所有 输入法相关窗口 的矩形按变化打出来
// （MSCTFIME Composition / MSCTFIME UI / CiceroUIWndFrame），再和输入框的
// 屏幕矩形算差值：候选窗左边 - 输入框左边、候选窗顶边 - 输入框底边。
//
// 用法（手动跑，不进 regress：抢键盘、切中文输入法）：
//   python tools/cl_one.py tools/probe/ime_align_probe.cpp build/ime_align_probe.exe \
//          user32.lib gdi32.lib msimg32.lib imm32.lib
//   ./build/ime_align_probe.exe
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"
#include <cstdio>
#include <atomic>
#include <thread>
#include <string>
#include <vector>

static std::atomic<HWND> g_hwnd{nullptr};
static const wchar_t* kTitle = L"ime align";

struct Box { int x = 0, y = 0, w = 0, h = 0; };      // 输入框（客户区物理像素）
static Box g_box;

struct WinInfo {
    HWND hwnd = nullptr;
    wchar_t cls[64] = {0};
    int vis = 0, x = 0, y = 0, w = 0, h = 0;
};

struct Ctx { std::vector<WinInfo>* out; };

static BOOL CALLBACK enum_cb(HWND w, LPARAM lp) {
    auto* v = reinterpret_cast<Ctx*>(lp)->out;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    wchar_t cls[64] = {0};
    if (!GetClassNameW(w, cls, 64)) return TRUE;
    if (lstrcmpW(cls, L"AinoWindowClass") == 0) return TRUE;
    RECT r{};
    GetWindowRect(w, &r);
    WinInfo i;
    i.hwnd = w;
    lstrcpynW(i.cls, cls, 64);
    i.vis = IsWindowVisible(w) ? 1 : 0;
    i.x = r.left; i.y = r.top; i.w = r.right - r.left; i.h = r.bottom - r.top;
    v->push_back(i);
    return TRUE;
}

static std::vector<WinInfo> snapshot() {
    std::vector<WinInfo> v;
    Ctx c{&v};
    EnumWindows(enum_cb, reinterpret_cast<LPARAM>(&c));
    return v;
}

static bool ime_class(const wchar_t* c) {
    return lstrcmpW(c, L"MSCTFIME Composition") == 0
        || lstrcmpW(c, L"MSCTFIME UI") == 0
        || lstrcmpW(c, L"CiceroUIWndFrame") == 0;
}

static void banner(const char* tag) {
    HWND h = g_hwnd.load();
    if (!h) return;
    POINT org{0, 0};
    ClientToScreen(h, &org);
    std::printf("== %s ==\n", tag);
    std::printf("   输入框（屏幕）= %d,%d .. %d,%d\n", org.x + g_box.x, org.y + g_box.y,
                org.x + g_box.x + g_box.w, org.y + g_box.y + g_box.h);
    std::fflush(stdout);
}

// 打印当前输入法相关窗口，并与输入框算差值
static void report(const std::vector<WinInfo>& v, const char* tag) {
    HWND h = g_hwnd.load();
    POINT org{0, 0};
    if (h) ClientToScreen(h, &org);
    const int bx0 = org.x + g_box.x, by1 = org.y + g_box.y + g_box.h;
    std::printf("  [%s]", tag);
    bool any = false;
    for (const auto& i : v) {
        if (!ime_class(i.cls) || i.w == 0 || i.h == 0) continue;
        any = true;
        std::printf("\n    %-20ls vis=%d rect=%d,%d %dx%d", i.cls, i.vis, i.x, i.y, i.w, i.h);
        if (i.vis) std::printf("  (左边差 %+d, 顶边比框底低 %+d)", i.x - bx0, i.y - by1);
    }
    if (!any) std::printf(" （没有输入法窗口）");
    std::printf("\n");
    std::fflush(stdout);
}

static HKL find_zh() {
    HKL list[128];
    int n = GetKeyboardLayoutList(128, list);
    for (int i = 0; i < n; ++i) {
        DWORD lang = DWORD(0xFFFF & (DWORD_PTR)list[i]);
        if ((lang & 0x3FF) == 0x04) return list[i];
    }
    return nullptr;
}

static void type_key(WORD vk) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    SendInput(1, &in, sizeof in);
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof in);
}

int main() {
    aino::Window win(560, 420, "ime align");
    if (!win.ok()) { std::printf("[fatal] window\n"); return 1; }

    std::string email, search, note;

    std::thread([&] {
        Sleep(1500);
        HWND h = g_hwnd.load();
        SetForegroundWindow(h);
        SetFocus(h);
        Sleep(300);
        if (HKL zh = find_zh()) SendMessageW(h, WM_INPUTLANGCHANGEREQUEST, 0, LPARAM(zh));
        Sleep(800);
        RECT cr{};
        GetClientRect(h, &cr);
        const float sc = float(cr.right) / 560.0f;
        LPARAM pt = MAKELPARAM(int(120 * sc), int(40 * sc));
        PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, pt);
        PostMessageW(h, WM_LBUTTONUP, 0, pt);
        Sleep(400);
        banner("起始");

        for (int k = 0; k < 5; ++k) {
            type_key('S');
            Sleep(180);
            // 每次按键后连采 6 次，抓候选项刷新那一刻的几何
            for (int s = 0; s < 6; ++s) {
                report(snapshot(), ("键 " + std::to_string(k) + " 采样 " + std::to_string(s)).c_str());
                Sleep(20);
            }
        }
        Sleep(400);
        report(snapshot(), "静止");
        std::printf("   email=\"%s\"  search=\"%s\"\n", email.c_str(), search.c_str());
        std::fflush(stdout);
        Sleep(200);
        PostMessageW(h, WM_CLOSE, 0, 0);
    }).detach();

    win.run([&](aino::Ui& ui) {
        g_hwnd.store(FindWindowW(nullptr, kTitle));
        ui.set_theme(aino::Theme::light());
        ui.clear(ui.theme().background);
        aino::Stack c = ui.vbox(aino::Rect(20, 16, 500, 380), 8.0f, 0.0f);
        ui.label("第一个输入框：", c.next(20.0f), aino::Style().left());
        const aino::Rect box = c.next(36.0f);
        ui.input(&email, box, aino::Style().hint("you@example.com"));
        ui.input(&search, c.next(36.0f), aino::Style().hint("输入关键词…"));
        c.skip(2.0f);
        ui.textarea(&note, c.next(92.0f), aino::Style().font(13.0f));

        RECT cr{};
        GetClientRect(g_hwnd.load(), &cr);
        const float sc = cr.right / 560.0f;
        g_box = Box{int(box.x * sc), int(box.y * sc), int(box.w * sc), int(box.h * sc)};
    });
    return 0;
}
