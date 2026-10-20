// 剪贴板链路 —— Ctrl+C / Ctrl+X / Ctrl+V 的六条纪律
//
// 这个探针不是来证明"能复制粘贴"的，它是来钉死一次真实闪退的根因：
//
//   Ui::Impl 上有 set_clipboard / get_clipboard 两个 std::function，
//   由 Window 在构造时绑定。但绑定代码**漏了**，而 input / textarea 里
//   Ctrl+C / Ctrl+X 又直接 `impl->set_clipboard(...)` 不判空 —— 调一个空的
//   std::function 会抛 std::bad_function_call，没人接、std::terminate，
//   用户看到的就是"选一段字按 Ctrl+C，程序自己关了"。
//   Ctrl+V 因为调用点顺手写了判空，所以不崩，只是没反应（更难发现）。
//
// 于是断言分三层：
//   A 没绑回调（Ui 脱离 Window 用：离屏、单测、嵌进别人窗口）→ 不许崩、不许丢数据
//   B 绑了回调 → 复制 / 剪切 / 粘贴的语义要正确（选区、caret、换行过滤、撤销）
//   C 真·系统剪贴板 → Window::set_clipboard / clipboard 的往返（含 CF_TEXT 回退）
//
// A 组在修复前会直接把进程打死（exit 132），所以它天然验红；B/C 是回归护栏。
#define AINO_IMPLEMENTATION
#include "../../aino.hpp"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

int g_pass = 0, g_fail = 0;

static void check(bool ok, const char* what, const std::string& detail = std::string()) {
    if (ok) { ++g_pass; }
    else    { ++g_fail; std::printf("  [FAIL] %s  %s\n", what, detail.c_str()); }
}

// ---------------------------------------------------------------- 离屏画布
struct Surface {
    HBITMAP bmp = nullptr;
    HDC dc = nullptr;
    void* bits = nullptr;
    aino::detail::Canvas cv;
    int w = 0, h = 0;

    Surface(int W, int H) : w(W), h(H) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        dc = CreateCompatibleDC(nullptr);
        SelectObject(dc, bmp);
        cv.px = (uint8_t*)bits;
        cv.w = W; cv.h = H; cv.stride = W * 4;
        cv.platform_dc = dc;
    }
    ~Surface() {
        if (dc) DeleteDC(dc);
        if (bmp) DeleteObject(bmp);
    }
};

struct Harness {
    aino::Ui ui;
    aino::detail::GdiTextRenderer tr;
    Surface* surf = nullptr;

    explicit Harness(Surface* s) : surf(s) {
        tr.init(1.0f, std::string(), std::string());
        ui.impl->text = &tr;
        ui.impl->canvas = &surf->cv;
        ui.set_theme(aino::Theme::light());
    }
    ~Harness() { tr.release(); }
};

// ---------------------------------------------------------------- 帧驱动
struct Drv {
    aino::Ui& ui;
    int w, h;
    Drv(aino::Ui& u, int W, int H) : ui(u), w(W), h(H) {}

    void raw(const std::function<void(aino::Input&)>& prep, const std::function<void()>& body) {
        ui.begin_frame(float(w), float(h), 1.0f / 60.0f, 1.0f);
        aino::Input& in = ui.impl->input;
        // end_frame 只清"一次性"事件；按住状态得自己复位，否则上一帧的
        // down[0]=true 会让编辑框以为鼠标一直按着，把 caret 拖回鼠标下。
        in.down[0] = in.down[1] = in.down[2] = false;
        in.key_down[aino::KEY_CTRL]  = false;
        in.key_down[aino::KEY_SHIFT] = false;
        in.key_down[aino::KEY_ALT]   = false;
        if (prep) prep(ui.impl->input);
        body();
        ui.end_frame();
    }
    void idle(const std::function<void()>& body) { raw(nullptr, body); }

    // 一次点击必须两帧：pressed 帧 + released 帧
    void click(float x, float y, const std::function<void()>& body) {
        raw([&](aino::Input& in) {
            in.mouse = aino::Vec2(x, y);
            in.down[0] = true; in.pressed[0] = true;
        }, body);
        raw([&](aino::Input& in) {
            in.mouse = aino::Vec2(x, y);
            in.down[0] = false; in.released[0] = true;
        }, body);
    }
    // 单个组合键。key_ctrl 是"按下那一刻"的修饰位，ctrl_of() 读的就是它。
    void key(int k, const std::function<void()>& body, bool ctrl = true, bool shift = false) {
        raw([&](aino::Input& in) {
            in.key_pressed[k] = true;
            if (ctrl)  { in.key_ctrl[k] = true;  in.key_down[aino::KEY_CTRL] = true; }
            if (shift) { in.key_shift[k] = true; in.key_down[aino::KEY_SHIFT] = true; }
        }, body);
    }
    // 光标定位：点框内某个横坐标（逻辑坐标）
    void caret_at(float x, const std::function<void()>& body) { click(x, 25.0f, body); }
};

// 内存版剪贴板：模拟 Window 绑定的那两个回调
static std::string g_clip;
static void bind_mem_clipboard(aino::Ui& ui) {
    ui.impl->set_clipboard = [](const std::string& s) { g_clip = s; };
    ui.impl->get_clipboard = []() { return g_clip; };
}

int main() {
    Surface surf(420, 220);

    // =========================================================== A 组：没绑回调
    // 这是崩溃现场。每组打完就 printf 一行，进程被打死时能看出死在哪。
    std::printf("A. 未绑定回调（Ui 脱离 Window）\n");
    {
        Harness h(&surf);
        Drv d(h.ui, 420, 220);
        std::string v = "abcdef";
        auto draw = [&] { h.ui.input(&v, aino::Rect(10, 10, 260, 30), aino::Style()); };

        d.click(60, 25, draw);                 // 聚焦
        d.key('A', draw);                      // 全选
        std::printf("  .. Ctrl+C (no callback)\n"); std::fflush(stdout);
        d.key('C', draw);
        check(true, "A1 未绑 set_clipboard：Ctrl+C 不崩（跑到这行即通过）");
        check(v == "abcdef", "A2 Ctrl+C 不改文本");

        std::printf("  .. Ctrl+X (no callback)\n"); std::fflush(stdout);
        d.key('X', draw);
        check(v == "abcdef", "A3 未绑回调时 Ctrl+X 不删内容（剪了却没地方放 = 丢数据）");

        std::printf("  .. Ctrl+V (no callback)\n"); std::fflush(stdout);
        d.key('V', draw);
        check(v == "abcdef", "A4 未绑 get_clipboard：Ctrl+V 不崩、不改文本");
    }
    {
        // textarea 走的是另一份代码（split_lines 那套），单独验一遍
        Harness h(&surf);
        Drv d(h.ui, 420, 220);
        std::string v = "l1\nl2";
        auto draw = [&] { h.ui.textarea(&v, aino::Rect(10, 10, 300, 120), aino::Style()); };
        d.click(60, 25, draw);
        d.key('A', draw);
        std::printf("  .. textarea Ctrl+C/X/V (no callback)\n"); std::fflush(stdout);
        d.key('C', draw);
        d.key('X', draw);
        check(v == "l1\nl2", "A5 textarea 未绑回调：C/X 都不崩、内容不动");
        d.key('V', draw);
        check(v == "l1\nl2", "A6 textarea 未绑回调：V 不崩、内容不动");
    }

    // =========================================================== B 组：绑了回调
    std::printf("B. 绑定回调后的语义\n");
    {
        Harness h(&surf);
        Drv d(h.ui, 420, 220);
        std::string v = "abcdef";
        auto draw = [&] { h.ui.input(&v, aino::Rect(10, 10, 260, 30), aino::Style()); };
        bind_mem_clipboard(h.ui);

        d.click(60, 25, draw);                 // 聚焦、caret 落在字中间
        d.key('A', draw);                      // 全选
        g_clip = "PRE";
        // 无选区（caret 在末尾、没有 anchor）时按 Ctrl+C：不许动剪贴板，
        // 否则用户刚复制的东西被一句空串清掉。
        h.ui.impl->caret = 6; h.ui.impl->sel_anchor = -1;
        d.key('C', draw);
        check(g_clip == "PRE", "B1 无选区时 Ctrl+C 不碰系统剪贴板");

        d.key('A', draw);
        d.key('C', draw);
        check(g_clip == "abcdef", "B2 全选后 Ctrl+C 复制到剪贴板");
        check(v == "abcdef", "B3 复制不改原文");
        check(h.ui.impl->caret == 6, "B4 复制后 caret 停在原文末尾（没被 Ctrl+A 的位置带走）");

        // 剪切：剪贴板拿得到，原文被删，caret 归位
        d.key('A', draw);
        d.key('X', draw);
        check(g_clip == "abcdef", "B5 Ctrl+X 把内容放进剪贴板");
        check(v.empty(), "B6 Ctrl+X 后原文被删除");
        check(h.ui.impl->caret == 0, "B7 Ctrl+X 后 caret 归 0");

        d.key('V', draw);
        check(v == "abcdef", "B8 Ctrl+V 粘回原文");
        check(h.ui.impl->caret == 6, "B9 粘贴后 caret 落在插入内容之后");

        // 单行：换行必须被过滤掉，否则单行框里会混进 \n
        v.clear();
        h.ui.impl->caret = 0; h.ui.impl->sel_anchor = -1;
        g_clip = "a\r\nb\nc";
        d.key('V', draw);
        check(v == "abc", "B10 单行 input 粘贴时过滤 \\r\\n");

        // 中文 / emoji：按码点插入，caret 按码点推进
        v.clear();
        h.ui.impl->caret = 0; h.ui.impl->sel_anchor = -1;
        g_clip = "中文\u00e9";                 // 中文 + é
        d.key('V', draw);
        check(v == g_clip, "B11 中文/非 ASCII 粘贴原样保留");

        // 空剪贴板：不许先删选区再插空
        v = "keep me";
        h.ui.impl->caret = 8; h.ui.impl->sel_anchor = -1;
        d.key('A', draw);                      // 全选
        g_clip.clear();
        d.key('V', draw);
        check(v == "keep me", "B12 空剪贴板 Ctrl+V 不抹掉选区");

        // 有选区粘贴 = 替换
        v = "hello";
        h.ui.impl->caret = 5; h.ui.impl->sel_anchor = -1;
        d.key('A', draw);
        g_clip = "X";
        d.key('V', draw);
        check(v == "X", "B13 有选区时粘贴替换选区");

        // 粘贴可以撤销（走 edit_snapshot 的 0.7s 合并窗口之外）
        const std::string before_undo = v;
        d.key('Z', draw);
        check(v == "hello", "B14 粘贴可被 Ctrl+Z 撤销", before_undo + " -> hello");
    }
    {
        // textarea：换行必须**保留**
        Harness h(&surf);
        Drv d(h.ui, 420, 220);
        std::string v = "";
        auto draw = [&] { h.ui.textarea(&v, aino::Rect(10, 10, 320, 140), aino::Style()); };
        bind_mem_clipboard(h.ui);

        d.click(30, 25, draw);
        g_clip = "l1\nl2\nl3";
        d.key('V', draw);
        check(v == "l1\nl2\nl3", "B15 textarea 粘贴保留换行");
        check(h.ui.impl->caret == 8, "B16 textarea 粘贴后 caret 在末尾（按码点计）");

        // 复制回来要一致
        d.key('A', draw);
        d.key('C', draw);
        check(g_clip == "l1\nl2\nl3", "B17 textarea 全选复制内容一致");

        d.key('X', draw);
        check(v.empty() && g_clip == "l1\nl2\nl3", "B18 textarea 剪切：内容进剪贴板且原文清空");
    }

    // =========================================================== C 组：真系统剪贴板
    // 只在剪贴板里**本来就是文本**时跑：否则覆盖掉用户复制的图片/文件不礼貌。
    // 跑完把原文本写回。
    std::printf("C. 系统剪贴板往返\n");
    {
        bool has_text = false;
        std::string saved;
        if (OpenClipboard(nullptr)) {
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
                if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) {
                    saved = aino::detail::from_wide(std::wstring(p));
                    GlobalUnlock(h);
                    has_text = true;
                }
            }
            CloseClipboard();
        }
        if (!has_text) {
            std::printf("  [SKIP] 当前剪贴板不是文本（可能是图片/文件），跳过以免覆盖\n");
        } else {
            aino::Window win(160, 100, "aino-clip-probe");
            if (!win.ok()) {
                check(false, "C0 探针窗口创建失败");
            } else {
                win.set_clipboard("你好 aino 123");
                check(win.clipboard() == "你好 aino 123", "C1 UTF-16 剪贴板往返（含中文）");

                // 只放 CF_TEXT 的源（不少老工具如此）：GetClipboardData 不会替你
                // 转格式，必须自己回退去取 ANSI，否则读到空串 —— 表现为"从某软件
                // 复制过来粘不进去"。
                bool put = false;
                if (OpenClipboard(nullptr)) {
                    EmptyClipboard();
                    const char* s = "ansi plain 42";
                    const size_t n = std::strlen(s) + 1;
                    if (HGLOBAL m = GlobalAlloc(GMEM_MOVEABLE, n)) {
                        std::memcpy(GlobalLock(m), s, n);
                        GlobalUnlock(m);
                        put = SetClipboardData(CF_TEXT, m) != nullptr;
                        if (!put) GlobalFree(m);
                    }
                    CloseClipboard();
                }
                check(put, "C2 写入 CF_TEXT 测试数据");
                check(win.clipboard() == "ansi plain 42", "C3 只有 CF_TEXT 时也能读出来（ANSI 回退）");

                win.set_clipboard(saved);   // 还原用户原来的剪贴板文本
                check(win.clipboard() == saved, "C4 恢复原剪贴板");
            }
        }
    }

    // 固定尾标：C 组会因为"剪贴板里不是文本"而跳过，总数会变（24 / 28），
    // 所以给 regress 一个不随用例数变化的判据。
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    std::printf("[clip] verdict: %s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
