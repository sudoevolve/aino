// 把一段真实抓下来的字节流回放进自家解析器，并在每个检查点打印整屏快照。
//
// 为什么需要它：线上出问题（hx 只画两行）时，"我们的屏幕"和"参考实现(pyte)的屏幕"
// 不一致，但肉眼看 dump 只能看出"少了几行"，看不出是**哪一条转义序列**处理错了。
// 有了定点快照，就能二分到具体 byte 偏移，直接对上那条 CSI。
//
// 用法：
//   term_replay.exe <bin> <cols> <rows> [step] [limit]
//   step  = 每喂多少字节打一次快照（默认 64）
//   limit = 总共只喂这么多字节（默认 全部）—— 二分"喂到第 n 字节时屏幕是否已经错了"
//           就靠它；没有它，step 一改就等于把整段都喂完，永远只看得到最终画面。
// 输出：每个检查点
//   ### <offset>
//   c <cy> <cx> <alt>
//   R<..> <fg> <bg> <attr> <text>
// 空行也打印（rstrip 后为空），因为"该有内容却是空"正是要找的差异。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../examples/ssh_core.hpp"

using namespace sshc;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: term_replay <bin> <cols> <rows> [step] [limit]\n");
        return 2;
    }
    const char* path = argv[1];
    const int cols = std::atoi(argv[2]);
    const int rows = std::atoi(argv[3]);
    const int step = argc > 4 ? std::max(1, std::atoi(argv[4])) : 64;
    const size_t limit = argc > 5 ? size_t(std::max(0, std::atoi(argv[5]))) : size_t(-1);
    std::FILE* f = std::fopen(path, "rb");
    if (!f) { std::printf("open %s failed\n", path); return 1; }
    std::string d;
    {
        char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.append(buf, n);
    }
    std::fclose(f);

    Terminal t;
    t.resize(cols, rows);
    t.reset();

    // 逐 step 喂，喂完一个 step 打一个快照
    const size_t total = std::min(d.size(), limit);
    size_t pos = 0;
    for (;;) {
        const size_t end = std::min(total, pos + size_t(step));
        if (end > pos) t.feed(d.data() + pos, end - pos);
        pos = end;
        std::printf("### %zu\n", pos);
        std::printf("c %d %d %d\n", t.cursor_row(), t.cursor_col(), int(t.alt_screen()));
        for (int y = 0; y < rows; ++y) {
            const std::vector<Cell>& r = t.screen_row(y);
            // 行文本：只取字符，行尾空格去掉
            std::string s;
            for (const Cell& c : r) {
                if (c.ch == 0) continue;                  // 全角右半格
                uint32_t cp = c.ch;
                if (cp < 0x80) s.push_back(char(cp));
                else if (cp < 0x800) { s.push_back(char(0xC0 | (cp >> 6))); s.push_back(char(0x80 | (cp & 0x3F))); }
                else if (cp < 0x10000) { s.push_back(char(0xE0 | (cp >> 12))); s.push_back(char(0x80 | ((cp >> 6) & 0x3F))); s.push_back(char(0x80 | (cp & 0x3F))); }
                else { s.push_back(char(0xF0 | (cp >> 18))); s.push_back(char(0x80 | ((cp >> 12) & 0x3F))); s.push_back(char(0x80 | ((cp >> 6) & 0x3F))); s.push_back(char(0x80 | (cp & 0x3F))); }
            }
            while (!s.empty() && s.back() == ' ') s.pop_back();
            if (s.empty()) { std::printf("R%02d - - - |\n", y); continue; }
            // 取第一个非空格子的颜色，够用来发现"整行颜色错"
            uint32_t fg = kDefaultFg, bg = kDefaultBg; uint8_t at = 0;
            for (const Cell& c : r) {
                if (c.ch != ' ' && c.ch != 0) { fg = c.fg; bg = c.bg; at = c.attr; break; }
            }
            std::printf("R%02d %08X %08X %u |%s|\n", y, fg, bg, unsigned(at), s.c_str());
        }
        if (pos >= total) break;
    }
    return 0;
}
