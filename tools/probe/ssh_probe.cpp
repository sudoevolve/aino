// =============================================================================
//  ssh_probe.cpp —— ssh_client 的端到端自检（无窗口）
//
//  连本地 tools/dev_ssh_server.py 起的测试服务器，验证四件事：
//    1. 握手 + 密码认证 + PTY shell
//    2. 终端解析器：中文回显、SGR 配色、光标绝对定位、滚动缓冲
//    3. SFTP：列表 / 新建目录 / 上传 / 下载 / 改名 / 删除（含内容逐字节比对）
//    4. 断线后状态清理
//
//  跑法：
//    python tools/dev_ssh_server.py --port 2222 &
//    python tools/cl_one.py tools/probe/ssh_probe.cpp tools/probe/ssh_probe.exe ...
//    ./tools/probe/ssh_probe.exe
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "../../examples/ssh_core.hpp"

static int g_fail = 0;
static int g_pass = 0;

// 控制台编码不可靠（GBK/UTF-8 会把中文糊掉），需要看原文时把屏幕内容按 UTF-8 落盘：
//   SSH_PROBE_DUMP=build/probe_screen.txt ./tools/probe/ssh_probe.exe
static void dump_screen(const sshc::Terminal& term, const char* path) {
    if (!path) return;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    for (int i = 0; i < term.total_lines(); ++i) {
        f << '[' << i << "] " << term.line_text(i) << '\n';
    }
}

static void check(bool ok, const char* what, const std::string& detail = "") {
    if (ok) { ++g_pass; std::printf("  ok    %s\n", what); }
    else {
        ++g_fail;
        std::printf("  FAIL  %s%s%s\n", what, detail.empty() ? "" : "  --> ", detail.c_str());
    }
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* host = std::getenv("SSH_PROBE_HOST") ? std::getenv("SSH_PROBE_HOST") : "127.0.0.1";
    const int port = std::getenv("SSH_PROBE_PORT") ? std::atoi(std::getenv("SSH_PROBE_PORT")) : 2222;

    sshc::Engine e;
    std::string err;
    sshc::Terminal term;
    term.resize(100, 30);

    // 收字节 + 喂给解析器。真实客户端里这是 UI 每帧干的事，这里用固定时长轮询代替。
    auto pump = [&](int ms) {
        const auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - t0).count() < ms) {
            std::string s = e.take_output();
            if (!s.empty()) term.feed(s.data(), s.size());
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    auto screen_text = [&] {
        std::string all;
        for (int i = 0; i < term.total_lines(); ++i) { all += term.line_text(i); all += "\n"; }
        return all;
    };

    std::printf("=== 1. 连接与认证 ===\n");
    {
        const bool ok = e.connect(host, port, "demo", "demo", "", &err);
        check(ok, "TCP + 握手 + 密码认证", err);
        if (!ok) { std::printf("\n连接不上，先起测试服务器：python tools/dev_ssh_server.py\n"); return 1; }
        check(!e.fingerprint().empty(), "拿到主机密钥指纹", e.fingerprint());
        check(e.open_shell(100, 30, &err), "申请 PTY + 启动 shell", err);
        e.start_reader();
    }

    std::printf("=== 2. 终端解析器 ===\n");
    pump(500);                                   // 横幅
    check(screen_text().find("aino SSH") != std::string::npos, "收到欢迎横幅");

    e.send("unicode\r");
    pump(700);
    {
        dump_screen(term, std::getenv("SSH_PROBE_DUMP"));
        const std::string s = screen_text();
        check(s.find("你好，世界！") != std::string::npos, "中文回显（UTF-8 解码）");
        check(s.find("こんにちは世界") != std::string::npos, "日文回显（全角）");
        if (s.find("こんにちは世界") == std::string::npos) {
            for (int i = 0; i < term.total_lines(); ++i) {
                const std::string ln = term.line_text(i);
                if (ln.find("日文") != std::string::npos || ln.find("こん") != std::string::npos)
                    std::printf("      [行 %d] %s\n", i, ln.c_str());
            }
        }
        // 全角字符占两格：'你' 在网格里应当是 ch=你 + 右半格 0
        bool wide_ok = false;
        for (int y = 0; y < term.rows && !wide_ok; ++y) {
            const std::vector<sshc::Cell>& r = term.screen_row(y);
            for (size_t x = 0; x + 1 < r.size(); ++x) {
                if (r[x].ch == 0x4F60u /* 你 */ && r[x + 1].ch == 0) { wide_ok = true; break; }
            }
        }
        check(wide_ok, "全角字符占两格（CJK 对齐的前提）");
    }

    e.send("colors\r");
    pump(700);
    {
        bool colored_bg = false, colored_fg = false;
        for (int y = 0; y < term.rows; ++y) {
            for (const sshc::Cell& c : term.screen_row(y)) {
                if (c.bg != sshc::kDefaultBg) colored_bg = true;
                if (c.fg != sshc::kDefaultFg) colored_fg = true;
            }
        }
        check(colored_fg, "SGR 前景色生效");
        check(colored_bg, "SGR 背景色 / 256 色生效");
    }

    e.send("clear\r");
    pump(300);
    e.send("cursor\r");
    pump(700);
    {
        // 服务端在 (5,10) 和 (10,20) 写了 A、B（1 基）
        const std::vector<sshc::Cell>& r4 = term.screen_row(4);
        const std::vector<sshc::Cell>& r9 = term.screen_row(9);
        check(4 < term.rows && 9 < term.cols && r4[9].ch == 'A', "CSI H 光标绝对定位（A）");
        check(9 < term.rows && 19 < term.cols && r9[19].ch == 'B', "CSI H 光标绝对定位（B）");
    }

    e.send("big\r");
    pump(900);
    check(term.history_size() > 100, "长输出进滚动缓冲",
          "history=" + std::to_string(term.history_size()));

    std::printf("=== 3. SFTP ===\n");
    {
        check(e.open_sftp(&err) || e.sftp_ready(), "打开 SFTP 子系统", err);
        std::vector<sshc::Entry> list;
        check(e.list("/", &list, &err), "列目录", err);
        const std::string dir = "/probe_dir_" + std::to_string(std::time(nullptr) % 100000);
        check(e.mkdir(dir, &err), "新建目录", err);
        check(e.list(dir, &list, &err), "进入新目录", err);
        check(list.empty(), "新目录是空的", "size=" + std::to_string(list.size()));

        // 上传
        const std::string local = "build/sshtest_probe.txt";
        const std::string body = "hello sftp 中文内容 0123456789\n第二行\n";
        std::FILE* f = std::fopen(local.c_str(), "wb");
        if (f) { std::fwrite(body.data(), 1, body.size(), f); std::fclose(f); }
        const std::string remote = dir + "/probe.txt";
        e.start_upload(local, remote);
        for (int i = 0; i < 400 && e.transfer_busy(); ++i) e.xfer_step();
        check(!e.transfer_busy(), "上传完成");
        check(e.transfer().error.empty(), "上传无错误", e.transfer().error);
        e.clear_transfer();

        std::vector<sshc::Entry> lst;
        e.list(dir, &lst, &err);
        bool found = false;
        for (const auto& x : lst) if (x.name == "probe.txt" && x.size == body.size()) found = true;
        check(found, "远端列表里有该文件且大小一致",
              lst.empty() ? "列表空" : (lst[0].name + " size=" + std::to_string(lst[0].size)));

        // 下载到另一个名字，逐字节比对
        const std::string back = "build/sshtest_probe_back.txt";
        e.start_download(remote, back, false);
        for (int i = 0; i < 400 && e.transfer_busy(); ++i) e.xfer_step();
        check(!e.transfer_busy(), "下载完成");
        e.clear_transfer();
        std::string got;
        if (std::FILE* g = std::fopen(back.c_str(), "rb")) {
            char buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, g)) > 0) got.append(buf, n);
            std::fclose(g);
        }
        check(got == body, "下载内容与上传字节一致",
              "got=" + std::to_string(got.size()) + " want=" + std::to_string(body.size()));

        check(e.rename(remote, dir + "/probe2.txt", &err), "重命名", err);
        check(e.unlink(dir + "/probe2.txt", &err), "删除文件", err);
        check(e.rmdir(dir, &err), "删除目录", err);
        check(!e.rmdir(dir, &err), "删除不存在的目录要报错（错误信息可用）", err);
    }

    std::printf("=== 4. 断开 ===\n");
    e.disconnect();
    check(!e.connected(), "断开后状态清零");
    check(!e.shell_open(), "通道已释放");

    std::printf("\n%s（通过 %d 项，失败 %d 项）\n", g_fail == 0 ? "PASS" : "FAIL", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
