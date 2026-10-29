// SFTP 卡死/超时的定位实验。
//
//   症状：连上之后在文件面板点目录 → 整个界面卡住；探针里 sftp_init 报
//         "Timed out waiting on socket (-9)"（20s 超时）。
//
// 三种顺序各跑一遍，看是"读线程把会话状态搞乱了"还是 SFTP 本身的问题：
//   A 连上就开 SFTP（没起读线程）
//   B 连上 → 开 shell → 起读线程 → 开 SFTP      （真实客户端的顺序）
//   C 连上 → 开 shell → 起读线程 → 停读线程 → 开 SFTP
//
//   python tools/build_ssh.py doctor && ./tools/probe/sftp_doctor.exe
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "../../examples/ssh_core.hpp"

static long long ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0).count();
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string mode = argc > 1 ? argv[1] : "B";
    std::printf("[mode %s]\n", mode.c_str());

    sshc::Engine e;
    std::string err;
    auto t0 = std::chrono::steady_clock::now();
    if (!e.connect("127.0.0.1", 2222, "demo", "demo", "", &err)) {
        std::printf("连接失败：%s\n", err.c_str());
        return 1;
    }
    std::printf("connect            %4lld ms\n", ms_since(t0));

    if (mode == "A") {
        t0 = std::chrono::steady_clock::now();
        const bool ok = e.open_sftp(&err);
        std::printf("open_sftp          %4lld ms  %s %s\n", ms_since(t0),
                    ok ? "ok" : "FAIL", err.c_str());
        if (ok) {
            std::vector<sshc::Entry> v;
            t0 = std::chrono::steady_clock::now();
            const bool l = e.list("/", &v, &err);
            std::printf("list /             %4lld ms  %s n=%zu %s\n", ms_since(t0),
                        l ? "ok" : "FAIL", v.size(), err.c_str());
        }
        return 0;
    }

    t0 = std::chrono::steady_clock::now();
    if (!e.open_shell(100, 30, &err)) { std::printf("shell 失败：%s\n", err.c_str()); return 1; }
    std::printf("open_shell         %4lld ms\n", ms_since(t0));
    e.start_reader();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    if (mode == "C") {
        t0 = std::chrono::steady_clock::now();
        e.stop_reader();
        std::printf("stop_reader        %4lld ms\n", ms_since(t0));
    }

    t0 = std::chrono::steady_clock::now();
    const bool ok = e.open_sftp(&err);
    std::printf("open_sftp          %4lld ms  %s %s\n", ms_since(t0),
                ok ? "ok" : "FAIL", err.c_str());
    if (ok) {
        std::vector<sshc::Entry> v;
        t0 = std::chrono::steady_clock::now();
        const bool l = e.list("/", &v, &err);
        std::printf("list /             %4lld ms  %s n=%zu %s\n", ms_since(t0),
                    l ? "ok" : "FAIL", v.size(), err.c_str());
    }
    return 0;
}
