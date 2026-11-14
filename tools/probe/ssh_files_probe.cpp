// 无网络、无 libssh2 依赖的真实本地文件契约。
#include "../../examples/ssh_files.hpp"
#include <chrono>
#if defined(_WIN32)
#include <windows.h>
#endif

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("aino-ssh-files-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); }
    } cleanup{root};
    sshc::KnownHosts hosts;
    hosts.path = (root / "nested" / "known_hosts").string();
    int failed = 0, passed = 0;
    auto check = [&](bool ok, const char* what) {
        if (ok) ++passed;
        else { ++failed; std::printf("FAIL: %s\n", what); }
    };
    std::string err;
    check(hosts.verify("localhost", 22, "sha256:test", &err) == 1, "first trust persists");
    check(!err.empty(), "first trust reports persistence");
#if defined(_WIN32)
    DWORD before = 0, after = 0;
    GetProcessHandleCount(GetCurrentProcess(), &before);
#endif
    for (int i = 0; i < 200; ++i) {
        check(hosts.verify("localhost", 22, "sha256:test", &err) == 0 && err.empty(), "matching host");
        check(hosts.verify("localhost", 22, "sha256:changed", &err) == -1 && !err.empty(), "changed key rejected");
    }
#if defined(_WIN32)
    GetProcessHandleCount(GetCurrentProcess(), &after);
    check(after <= before, "matching and mismatching paths do not leak handles");
#endif
    check(hosts.verify("localhost", 2222, "sha256:other", &err) == 1, "port isolates host record");
    check(hosts.verify("localhost", 2222, "sha256:other", &err) == 0, "custom port record reloads");
    check(hosts.verify("host\ninjected", 22, "sha256:test", &err) == -1, "record injection rejected");
    hosts.path = root.string();
    check(hosts.verify("localhost", 22, "sha256:test", &err) == -1 && !err.empty(), "unreadable records refuse trust");
    hosts.path = (root / "blocker").string();
    { std::ofstream file(hosts.path); file << "not a directory"; }
    hosts.path += "/known_hosts";
    check(hosts.verify("localhost", 22, "sha256:test", &err) == -1 && !err.empty(), "write failure refuses trust");
    std::printf("[ssh-files] %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
