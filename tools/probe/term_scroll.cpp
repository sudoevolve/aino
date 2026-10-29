// 纯解析器压力测试：不碰网络，直接喂字节，用来把"终端模拟器的内存错误"
// 和"libssh2/引擎的问题"分开。堆损坏（0xC0000374）时这个探针能立刻复现。
//
//   python tools/build_ssh.py term && ./tools/probe/term_scroll.exe
#include <cstdio>
#include <string>

#include "../../examples/ssh_core.hpp"

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    sshc::Terminal t;
    t.resize(100, 30);

    std::printf("1) 200 行普通文本\n");
    for (int i = 0; i < 200; ++i) {
        char line[128];
        std::snprintf(line, sizeof line, "\x1b[0m第 %03d 行：滚动缓冲测试 the quick brown fox\r\n", i + 1);
        t.feed(line, std::strlen(line));
    }
    std::printf("   history=%d total=%d\n", t.history_size(), t.total_lines());

    std::printf("2) 塞超长行（超过 100 列，强制软换行）\n");
    std::string longline(400, 'x');
    longline += "\r\n";
    t.feed(longline.data(), longline.size());
    std::printf("   total=%d\n", t.total_lines());

    std::printf("3) 超过 scrollback_max 的行数（5000 行）\n");
    for (int i = 0; i < 5000; ++i) {
        char line[64];
        std::snprintf(line, sizeof line, "line %d\r\n", i);
        t.feed(line, std::strlen(line));
    }
    std::printf("   history=%d total=%d\n", t.history_size(), t.total_lines());

    std::printf("4) 全屏滚动区域 + CSI r / S / T\n");
    t.feed("\x1b[5;20r\x1b[10S\x1b[3T\x1b[r", 22);
    std::printf("   ok\n");

    std::printf("5) 备用屏进出\n");
    t.feed("\x1b[?1049h\x1b[2J\x1b[H", 15);
    t.feed("alt screen\r\n", 12);
    t.feed("\x1b[?1049l", 8);
    std::printf("   ok\n");

    std::printf("6) 拆开的 UTF-8（逐字节喂）\n");
    const char* jp = "中文 こんにちは世界 🚀\r\n";
    for (const char* p = jp; *p; ++p) t.feed(p, 1);
    std::printf("   line=%s\n", t.line_text(t.total_lines() - 2).c_str());

    std::printf("7) 选区复制（跨行，含全角）\n");
    t.sel_begin(t.total_lines() - 3, 0);
    t.sel_extend(t.total_lines() - 2, 20);
    std::printf("   sel=[%s]\n", t.selected_text().c_str());

    std::printf("全部跑完，没有堆损坏。\n");
    return 0;
}
