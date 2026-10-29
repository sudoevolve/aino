// hx 编辑器兼容性探针（无 GUI，纯 headless）。
//
// 为什么要有它：hx / vim 这类全屏程序对三件事特别敏感 ——
//   1. pty 尺寸（远端以为屏幕多大就只画多大）；
//   2. 颜色模式（TERM/COLORTERM 决定它发 16 色、256 色还是真彩）；
//   3. 备用屏 + 光标定位（底部状态行能不能出来全靠它）。
// 用眼睛盯 GUI 截图判断这三件事效率极低，所以这里把远端的**原始字节流**存下来，
// 再用 pyte（参考实现）和自家 Terminal 解析器逐行对比 —— 差异就是 bug 所在。
//
// 用法：
//   python tools/build_ssh.py hxprobe && tools/probe/term_hx.exe [host] [user] [pass]
// 产物：
//   build/hx_raw.bin   远端原始字节流
//   build/hx_ours.txt  自家解析器的屏幕快照
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../examples/ssh_core.hpp"

using namespace sshc;

static std::string g_raw;          // 累积所有远端字节，最后落盘

static void pump(Engine& en, Terminal& t, int ms) {
    const int step = 20;
    for (int i = 0; i < ms; i += step) {
        if (std::string out = en.take_output(); !out.empty()) {
            g_raw += out;
            t.feed(out.data(), out.size());
            if (std::string rep = t.take_reply(); !rep.empty()) en.send(rep);
        }
        Sleep(step);
    }
    if (std::string out = en.take_output(); !out.empty()) {
        g_raw += out;
        t.feed(out.data(), out.size());
    }
    if (std::string rep = t.take_reply(); !rep.empty()) en.send(rep);
}

// 只打印"有内容的行 + 底部 3 行"，否则 45 行空屏看不出重点
static void dump(const Terminal& t, const char* tag, FILE* f) {
    std::fprintf(f, "---- %s  (%dx%d, alt=%d, cur=%d,%d, hist=%d)\n", tag, t.cols, t.rows,
                 int(t.alt_screen()), t.cursor_col(), t.cursor_row(), t.history_size());
    int last_text = -1;
    for (int y = 0; y < t.rows; ++y)
        for (const Cell& c : t.screen_row(y))
            if (c.ch != ' ' && c.ch != 0) { last_text = y; break; }
    for (int y = 0; y < t.rows; ++y) {
        const std::string s = t.line_text(t.history_size() + y);
        const bool text = !s.empty();
        if (!text && y > last_text && y < t.rows - 3) continue;
        std::fprintf(f, "[%02d] |%s|\n", y, s.c_str());
    }
}

// 打印"末尾 n 行有内容的行"：45 行屏幕上 total-4 一定是空白，会误判成"没有输出"
static void tail(FILE* f, const Terminal& t, int n) {
    int last = 0;
    for (int i = 0; i < t.total_lines(); ++i)
        if (!t.line_text(i).empty()) last = i;
    for (int i = std::max(0, last - n + 1); i <= last; ++i)
        std::fprintf(f, "     %s\n", t.line_text(i).c_str());
}

static void color_hist(const Terminal& t, FILE* f) {
    int def = 0, idx16 = 0, idx256 = 0, rgb = 0, bad = 0;
    for (int y = 0; y < t.rows; ++y) {
        for (const Cell& c : t.screen_row(y)) {
            if (c.ch == ' ' || c.ch == 0) continue;
            if (c.fg == kDefaultFg) ++def;
            else if (c.fg & kRgbFlag) ++rgb;
            else if (c.fg < 16) ++idx16;
            else if (c.fg < 256) ++idx256;
            else ++bad;
        }
    }
    std::fprintf(f, "colors: default=%d idx16=%d idx256=%d rgb=%d bad=%d\n", def, idx16, idx256, rgb, bad);
}

int main(int argc, char** argv) {
    // 无缓冲：探针一旦崩，缓冲里的输出就全丢了，看不到它死在哪一步
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* host = argc > 1 ? argv[1] : "192.168.12.131";
    const char* user = argc > 2 ? argv[2] : "aino";
    const char* pass = argc > 3 ? argv[3] : "123456";

    // HX_DIRECT=WxH：一开始就按这个尺寸申请 pty，不做中途 resize。
    // 用来 A/B 区分"pty 尺寸从来没对过"和"中途 resize 把远端搞乱了"。
    int direct_c = 0, direct_r = 0;
    if (const char* d = std::getenv("HX_DIRECT")) std::sscanf(d, "%dx%d", &direct_c, &direct_r);
    const bool direct = direct_c > 0 && direct_r > 0;
    const char* raw_path = direct ? "build/hx_raw_direct.bin" : "build/hx_raw.bin";
    const char* ours_path = direct ? "build/hx_ours_direct.txt" : "build/hx_ours.txt";

    Engine en;
    std::string err;
    std::printf("connect %s@%s ...\n", user, host);
    if (!en.connect(host, 22, user, pass, "", &err)) {
        std::printf("FAIL stage=%d err=%s\n", int(en.fail_stage()), err.c_str());
        return 1;
    }
    std::printf("ok %s\n", en.who().c_str());

    FILE* out = std::fopen(ours_path, "wb");
    if (!out) out = stdout;

    // ---- 用例 1：pty 尺寸真的生效吗 ----
    const int open_c = direct ? direct_c : 80;
    const int open_r = direct ? direct_r : 24;
    if (!en.open_shell(open_c, open_r, &err)) { std::printf("open_shell FAIL: %s\n", err.c_str()); return 1; }
    en.start_reader();
    Terminal t;
    t.resize(open_c, open_r);
    pump(en, t, 800);
    en.send("echo \"T=$TERM CT=$COLORTERM\"; stty size\n");
    pump(en, t, 900);
    std::fprintf(out, "[S1] after open_shell(%dx%d):\n", open_c, open_r);
    tail(out, t, 4);

    if (!direct) {
        en.resize_pty(150, 45);
        t.resize(150, 45);
        pump(en, t, 500);
        en.send("stty size\n");
        pump(en, t, 900);
        std::fprintf(out, "[S1] after resize_pty(150,45):\n");
        tail(out, t, 4);
    }

    // ---- 用例 1.5：整屏填充对照（验证"不是我们解析器画不满"）----
    // 用 printf 直接铺满 45 行：如果这个能铺满而 hx 不能，问题就在 hx 那边。
    en.send("printf '\\033[2J'; for i in $(seq 1 45); do printf '\\033[%d;1HL%02d fill-fill-fill-fill-fill-fill-fill-fill\\n' $i $i; done\n");
    pump(en, t, 1200);
    dump(t, "full-screen fill test (seq 45 rows)", out);

    // ---- 用例 2：hx 在 150x45 下画了什么 ----
    const size_t raw_mark = g_raw.size();
    en.send("hx /tmp/aino_hx_probe.txt\n");
    pump(en, t, 3000);
    dump(t, direct ? "hx after open (direct pty)" : "hx after resize", out);
    color_hist(t, out);

    // ---- 用例 3：键位是否被 hx 收到 ----
    en.send("hello");
    pump(en, t, 400);
    en.send("\x1b[D\x1b[D");
    pump(en, t, 300);
    en.send("\x7f");
    pump(en, t, 300);
    dump(t, "after typing + 2x left + bs", out);

    // ---- 用例 4：退出 hx，看备用屏是否恢复 ----
    en.send("\x1b:q!\r");
    pump(en, t, 700);
    if (t.alt_screen()) { en.send("\x1b:q!\r"); pump(en, t, 700); }
    std::fprintf(out, "[S4] alt=%d hist=%d\n", int(t.alt_screen()), t.history_size());

    // hx 那一段单独存一份，pyte 对比只回放这段
    {
        FILE* rf = std::fopen(raw_path, "wb");
        if (rf) { std::fwrite(g_raw.data() + raw_mark, 1, g_raw.size() - raw_mark, rf); std::fclose(rf); }
        FILE* ra = std::fopen("build/hx_raw_all.bin", "wb");
        if (ra) { std::fwrite(g_raw.data(), 1, g_raw.size(), ra); std::fclose(ra); }
    }

    en.send("exit\n");
    pump(en, t, 500);
    if (out != stdout) std::fclose(out);
    std::printf("done raw=%zu bytes\n", g_raw.size());
    return 0;
}
