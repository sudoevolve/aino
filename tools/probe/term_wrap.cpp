// 全宽行（整行正好 cols 个字符）的换行回归测试。
//
// 场景：`tmux capture-pane -p` / `htop` / 带背景色的整行输出，都会写"恰好 cols 宽"的行。
// 如果解析器在"自动换行挂起(wrap_pending) + CR"这一对组合上算错一格，
// 就会整行吃掉或者错位一行 —— 现象是"屏幕上少了一大片内容"。
#include <cstdio>
#include <string>
#include <vector>

#include "../../examples/ssh_core.hpp"

using namespace sshc;

static int fails = 0;

static void check(bool ok, const char* what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++fails;
}

int main() {
    const int C = 150, R = 45;

    // 用例 1：40 行"整好 C 宽"的行（模拟 tmux capture-pane 的输出）
    {
        Terminal t;
        t.resize(C, R);
        std::string s;
        for (int i = 0; i < 40; ++i) {
            char head[32];
            std::snprintf(head, sizeof head, "L%02d~", i);
            std::string line = head;
            line.resize(size_t(C), ' ');       // 正好 150 个字符
            s += line;
            s += "\r\n";
        }
        t.feed(s.data(), s.size());
        // 期望：每一行都出现在连续的 40 行里，且首字符是 L
        int found = 0;
        for (int y = 0; y < t.rows; ++y) {
            const std::string tx = t.line_text(t.history_size() + y);
            if (tx.rfind("L", 0) == 0) ++found;
        }
        std::printf("用例1 全宽行: 找到 %d 行（期望 40）\n", found);
        for (int y = 0; y < 6; ++y)
            std::printf("   row%02d=%s\n", y, t.line_text(t.history_size() + y).c_str());
        check(found == 40, "40 行整宽行一行不少");
    }

    // 用例 2：整宽行之后紧跟短行（混合：htop 那种带背景色的整行 + 普通行）
    {
        Terminal t;
        t.resize(20, 10);
        std::string s;
        std::string wide(20, 'W');
        s += wide; s += "\r\n";        // 整宽
        s += "short\r\n";              // 短行
        s += wide; s += "\r\n";        // 又整宽
        s += "end\r\n";
        t.feed(s.data(), s.size());
        const std::string r0 = t.line_text(t.history_size() + 0);
        const std::string r1 = t.line_text(t.history_size() + 1);
        const std::string r2 = t.line_text(t.history_size() + 2);
        const std::string r3 = t.line_text(t.history_size() + 3);
        std::printf("用例2: r0=%s r1=%s r2=%s r3=%s\n", r0.c_str(), r1.c_str(), r2.c_str(), r3.c_str());
        check(r0 == wide && r1 == "short" && r2 == wide && r3 == "end", "整宽行与短行交错不错位");
    }

    // 用例 3：整宽行 + 延迟到来的下一行（分两次 feed，模拟网络分包）
    {
        Terminal t;
        t.resize(20, 10);
        std::string wide(20, 'X');
        t.feed(wide.data(), wide.size());          // 第一次：刚好写满，wrap 挂起
        const std::string s2 = "\r\nsecond\r\n";
        t.feed(s2.data(), s2.size());              // 第二次：CRLF 才到
        const std::string r0 = t.line_text(t.history_size() + 0);
        const std::string r1 = t.line_text(t.history_size() + 1);
        std::printf("用例3: r0=[%s] r1=[%s]\n", r0.c_str(), r1.c_str());
        check(r0.size() == 20 && r1 == "second", "跨分包的全宽行不丢下一行");
    }

    // 用例 4：整宽行后直接跟下一行（没有 CRLF，靠自动换行）—— htop 常见
    {
        Terminal t;
        t.resize(10, 5);
        std::string s;
        s += std::string(10, 'A');     // 写满 → 挂起
        s += std::string(10, 'B');     // 第 11 个字符触发换行
        t.feed(s.data(), s.size());
        const std::string r0 = t.line_text(t.history_size() + 0);
        const std::string r1 = t.line_text(t.history_size() + 1);
        std::printf("用例4: r0=[%s] r1=[%s]\n", r0.c_str(), r1.c_str());
        check(r0 == std::string(10, 'A') && r1 == std::string(10, 'B'), "自动换行位置正确");
    }

    std::printf("\n%s (%d fails)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
