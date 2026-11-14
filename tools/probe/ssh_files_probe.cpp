// 无网络、无 libssh2 依赖的真实本地文件契约。
#include "../../examples/ssh_files.hpp"
#include <chrono>
#include <iterator>
#if defined(_WIN32)
#include <windows.h>
#include <winioctl.h>
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

    const auto config = root / "config.json";
    const std::string config_path = config.u8string();
    check(sshc::atomic_write_file(config_path, "old complete data", &err) && err.empty(), "atomic initial save");
    auto read = [&](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    check(sshc::atomic_write_file(config_path, "new complete data", &err) && read(config) == "new complete data",
          "atomic replacement saves complete new contents");
#if defined(_WIN32)
    const HANDLE held = CreateFileW(config.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    check(held != INVALID_HANDLE_VALUE, "hold target against replacement");
    check(!sshc::atomic_write_file(config_path, "partial", &err) && !err.empty(), "blocked replacement reports failure");
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    check(read(config) == "new complete data", "failed replacement preserves original file");
#endif
    check(!sshc::atomic_write_file(root.u8string(), "invalid", &err), "cannot replace directory with a file");
    bool no_temps = true;
    for (const auto& entry : std::filesystem::directory_iterator(root))
        if (entry.path().filename().string().find(".tmp.") != std::string::npos) no_temps = false;
    check(no_temps, "success and failure remove temporary files");
    const auto unicode = root / std::filesystem::u8path("配置.json");
    check(sshc::atomic_write_file(unicode.u8string(), "UTF-8 path", &err) && read(unicode) == "UTF-8 path",
          "atomic save supports Unicode filenames");
    std::FILE* unicode_input = sshc::open_local_file(unicode.u8string(), "rb");
    char unicode_buffer[32]{};
    const auto unicode_size = unicode_input ? std::fread(unicode_buffer, 1, sizeof(unicode_buffer), unicode_input) : 0;
    if (unicode_input) std::fclose(unicode_input);
    check(std::string(unicode_buffer, unicode_size) == "UTF-8 path", "upload opens Unicode local path");
    std::FILE* unicode_output = sshc::open_local_file(unicode.u8string(), "wb");
    check(unicode_output && std::fwrite("download", 1, 8, unicode_output) == 8, "download opens Unicode local path");
    if (unicode_output) std::fclose(unicode_output);
    check(read(unicode) == "download", "Unicode download path contains exact bytes");
#if !defined(_WIN32)
    const auto link = root / "config-link";
    std::filesystem::create_symlink(config.filename(), link);
    check(sshc::atomic_write_file(link.u8string(), "through link", &err) && std::filesystem::is_symlink(link) &&
          read(config) == "through link", "save follows and preserves configuration symlink");
#endif

    const auto large = root / "large.sparse";
    const uint64_t large_size = (uint64_t(3) << 30) + 17;
    bool sparse = false;
#if defined(_WIN32)
    HANDLE large_file = CreateFileW(large.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (large_file != INVALID_HANDLE_VALUE) {
        DWORD returned = 0;
        LARGE_INTEGER end{};
        end.QuadPart = LONGLONG(large_size);
        sparse = DeviceIoControl(large_file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr) &&
                 SetFilePointerEx(large_file, end, nullptr, FILE_BEGIN) && SetEndOfFile(large_file);
        CloseHandle(large_file);
    }
#else
    const int large_file = ::open(large.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (large_file >= 0) {
        sparse = ftruncate(large_file, off_t(large_size)) == 0;
        ::close(large_file);
    }
#endif
    check(sparse, "create sparse fixture larger than 2 GiB without allocating its contents");
    std::FILE* file = std::fopen(large.string().c_str(), "rb");
    uint64_t size = 0;
    check(sshc::local_file_size(file, &size, &err) && size == large_size, "64 bit file length is exact");
    check(file && std::ftell(file) == 0, "length query preserves read position");
    if (file) std::fclose(file);
    check(!sshc::local_file_size(nullptr, &size, &err) && !err.empty(), "invalid file length query reports error");
    std::printf("[ssh-files] %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
