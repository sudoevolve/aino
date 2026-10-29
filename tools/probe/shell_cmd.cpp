// 通用"在远端 pty 里跑一条命令并回显屏幕"的探针。
//
// 用途：排查终端类问题（配色、尺寸、按键）时，经常需要看"远端某条命令到底输出什么"。
// 每次改 term_hx.cpp 太笨，这里做成通用的：
//
//   shell_cmd.exe "cat ~/.config/helix/config.toml; hx --version"
//   环境变量：
//     SHELL_CMD_COLS / SHELL_CMD_ROWS    pty 尺寸（默认 150x45）
//     SHELL_CMD_RAW=path                 原始字节流落盘（默认 build/shell_cmd_raw.bin）
//
// 输出：build/shell_cmd.txt（屏幕快照，只打印有内容的行）
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../../examples/ssh_core.hpp"

using namespace sshc;

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) { std::printf("usage: shell_cmd.exe \"cmd\" [host] [user] [pass]\n"); return 2; }
    const char* cmd = argv[1];
    const char* host = argc > 2 ? argv[2] : "192.168.12.131";
    const char* user = argc > 3 ? argv[3] : "aino";
    const char* pass = argc > 4 ? argv[4] : "123456";
    const int cols = std::getenv("SHELL_CMD_COLS") ? std::atoi(std::getenv("SHELL_CMD_COLS")) : 150;
    const int rows = std::getenv("SHELL_CMD_ROWS") ? std::atoi(std::getenv("SHELL_CMD_ROWS")) : 45;

    Engine en;
    std::string err;
    if (!en.connect(host, 22, user, pass, "", &err)) {
        std::printf("connect FAIL stage=%d %s\n", int(en.fail_stage()), err.c_str());
        return 1;
    }
    if (!en.open_shell(cols, rows, &err) || (en.start_reader(), false)) {
        std::printf("open_shell FAIL %s\n", err.c_str());
        return 1;
    }
    Terminal t;
    t.resize(cols, rows);
    std::string raw;
    auto pump = [&](int ms) {
        for (int i = 0; i < ms; i += 20) {
            if (std::string o = en.take_output(); !o.empty()) {
                raw += o;
                t.feed(o.data(), o.size());
                if (std::string r = t.take_reply(); !r.empty()) en.send(r);
            }
            Sleep(20);
        }
    };
    auto drain = [&]() {
        if (std::string o = en.take_output(); !o.empty()) { raw += o; t.feed(o.data(), o.size()); }
        if (std::string r = t.take_reply(); !r.empty()) en.send(r);
    };
    pump(700);
    en.send(std::string(cmd) + "\n");
    pump(2500);
    drain();

    FILE* f = std::fopen("build/shell_cmd.txt", "wb");
    if (!f) f = stdout;
    for (int y = 0; y < t.total_lines(); ++y) {
        const std::string s = t.line_text(y);
        if (!s.empty()) std::fprintf(f, "[%02d] %s\n", y, s.c_str());
    }
    if (f != stdout) std::fclose(f);
    if (const char* rp = std::getenv("SHELL_CMD_RAW")) {
        if (FILE* rf = std::fopen(rp, "wb")) { std::fwrite(raw.data(), 1, raw.size(), rf); std::fclose(rf); }
    }
    std::printf("ok raw=%zu -> build/shell_cmd.txt\n", raw.size());
    return 0;
}
