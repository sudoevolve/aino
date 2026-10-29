// =============================================================================
//  dc_repro.cpp —— "断开时偶发 0xC0000005" 的最小复现
//
//  为什么单独写一个：ssh_probe 一跑就是全部用例，崩了不知道是 SFTP、终端还是
//  断开本身的问题。这里把变量拆开，一次只做一件事，并且在同一进程里反复做：
//
//    dc_repro <mode> <n>
//      mode = shell    只开 shell + 读线程，然后断开
//      mode = sftp     只开 SFTP（列一次目录），然后断开
//      mode = both     两样都开
//      mode = noshell  不读写线程，纯连接后立刻断开
//
//  用法（先起本地服务器）：
//    python tools/dev_ssh_server.py --port 2222 --root build/sshtest &
//    python tools/build_ssh.py dc     # 见 build_ssh.py 里的 dc 目标
//    ./tools/probe/dc_repro.exe both 20
//
//  输出每轮一行；崩在第几轮、哪个阶段都有据可查。
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>

#include "../../examples/ssh_core.hpp"

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string mode = argc > 1 ? argv[1] : "both";
    const int n = argc > 2 ? std::atoi(argv[2]) : 10;

    const char* host = std::getenv("SSH_PROBE_HOST") ? std::getenv("SSH_PROBE_HOST") : "127.0.0.1";
    const int port = std::getenv("SSH_PROBE_PORT") ? std::atoi(std::getenv("SSH_PROBE_PORT")) : 2222;

    int crashes = 0;
    for (int i = 0; i < n; ++i) {
        std::printf("[%02d] connect... ", i);
        sshc::Engine e;
        std::string err;
        if (!e.connect(host, port, "demo", "demo", "", &err)) {
            std::printf("connect FAIL: %s\n", err.c_str());
            return 2;
        }
        if (mode == "shell" || mode == "both") {
            if (!e.open_shell(80, 24, &err)) std::printf("open_shell FAIL: %s ", err.c_str());
            e.start_reader();
        }
        if (mode == "sftp" || mode == "both") {
            if (!e.open_sftp(&err)) std::printf("sftp FAIL: %s ", err.c_str());
            std::vector<sshc::Entry> v;
            e.list("/", &v, &err);
        }
        // 有读线程时给它一点时间真的跑起来（读线程和断开的竞态只在"跑起来"时才有）
        std::this_thread::sleep_for(std::chrono::milliseconds(60));

        std::printf("disconnect... ");
        const auto t0 = std::chrono::steady_clock::now();
        e.disconnect();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();
        std::printf("ok (%lld ms)\n", static_cast<long long>(ms));
    }
    std::printf("done, %d rounds, %d crashes\n", n, crashes);
    return 0;
}
