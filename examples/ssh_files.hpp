// SSH 示例的本地文件支持；不依赖网络库，允许离线验证持久化行为。
#pragma once

#include <cstdarg>
#include <cstdint>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace sshc {

// File picker paths are UTF-8; use the wide file API on Windows.
inline std::FILE* open_local_file(const std::string& path, const char* mode) {
#if defined(_WIN32)
    const auto wide = std::filesystem::u8path(path).wstring();
    std::wstring wide_mode;
    for (const char* p = mode; *p; ++p)
        wide_mode.push_back(wchar_t(*p));
    return _wfopen(wide.c_str(), wide_mode.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

inline std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return std::string(buf);
}

inline std::string home_dir() {
    if (const char* p = std::getenv("AINO_SSH_HOME")) {
        if (*p)
            return p;
    }
#if defined(_WIN32)
    const char* p = std::getenv("USERPROFILE");
    if (!p)
        p = std::getenv("HOMEDRIVE");
#else
    const char* p = std::getenv("HOME");
#endif
    return p ? std::string(p) : std::string(".");
}

// 同目录独占创建，避免传输失败或取消时先截断最终目标。
inline std::FILE* create_transfer_file(const std::string& target, std::string* temporary) {
    static std::atomic<unsigned long> next{0};
#if defined(_WIN32)
    const auto pid = GetCurrentProcessId();
#else
    const auto pid = getpid();
#endif
    temporary->clear();
    for (int attempt = 0; attempt < 32; ++attempt) {
        const std::string candidate = target + ".part." + std::to_string(pid) + "." + std::to_string(next++);
#if defined(_WIN32)
        const int fd = _wopen(std::filesystem::u8path(candidate).c_str(),
                              _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
        const int fd = ::open(candidate.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif
        if (fd < 0) {
            if (errno == EEXIST)
                continue;
            return nullptr;
        }
#if defined(_WIN32)
        std::FILE* file = _fdopen(fd, "wb");
        if (!file)
            _close(fd);
#else
        std::FILE* file = fdopen(fd, "wb");
        if (!file)
            ::close(fd);
#endif
        if (!file) {
            std::error_code ignored;
            std::filesystem::remove(std::filesystem::u8path(candidate), ignored);
        } else
            *temporary = candidate;
        return file;
    }
    return nullptr;
}

inline bool replace_transfer_file(const std::string& temporary, const std::string& target,
                                  bool replace_existing = true) {
#if defined(_WIN32)
    const auto from = std::filesystem::u8path(temporary), to = std::filesystem::u8path(target);
    if (!replace_existing)
        return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
    const DWORD attributes = GetFileAttributesW(to.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES)
        return ReplaceFileW(to.c_str(), from.c_str(), nullptr, 0, nullptr, nullptr) != 0;
    // 没有 REPLACE_EXISTING：检查后若出现同名文件，保留它并报告冲突。
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (!replace_existing) {
        if (::link(temporary.c_str(), target.c_str()) != 0)
            return false;
        ::unlink(temporary.c_str());
        return true;
    }
    struct stat original{};
    if (::stat(target.c_str(), &original) == 0) {
        struct stat staged{};
        if (::stat(temporary.c_str(), &staged) != 0)
            return false;
        if ((staged.st_uid != original.st_uid || staged.st_gid != original.st_gid) &&
            ::chown(temporary.c_str(), original.st_uid, original.st_gid) != 0)
            return false;
        if (::chmod(temporary.c_str(), original.st_mode & 07777) != 0)
            return false;
    } else if (errno != ENOENT)
        return false;
    return ::rename(temporary.c_str(), target.c_str()) == 0;
#endif
}

// 临时文件与目标同目录，独占创建；写完并同步后才替换，失败保留旧文件。
inline bool atomic_write_file(const std::string& path, const std::string& data, std::string* err) {
    if (err)
        err->clear();
    namespace fs = std::filesystem;
    auto fail = [&](const char* stage) {
        if (err)
            *err = std::string("保存失败（") + stage + "）：" + path;
        return false;
    };
    fs::path target = fs::u8path(path);
    std::error_code ec;
    const auto status = fs::symlink_status(target, ec);
    if (ec && ec != std::errc::no_such_file_or_directory)
        return fail("检查目标");
    ec.clear();
    if (fs::is_symlink(status)) {
        target = fs::canonical(target, ec); // 保留用户的配置符号链接。
        if (ec)
            return fail("解析符号链接");
    }
    const auto parent = target.parent_path();
    if (!parent.empty())
        fs::create_directories(parent, ec);
    if (ec)
        return fail("创建目录");
    static std::atomic<unsigned long> next{0};
#if defined(_WIN32)
    const auto pid = GetCurrentProcessId();
#else
    const auto pid = getpid();
#endif
    fs::path temp;
    int fd = -1;
    for (int attempt = 0; attempt < 16; ++attempt) {
        temp = target;
        temp += ".tmp." + std::to_string(pid) + "." + std::to_string(next++);
#if defined(_WIN32)
        fd = _wopen(temp.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
        fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif
        if (fd >= 0 || errno != EEXIST)
            break;
    }
    if (fd < 0)
        return fail("创建临时文件");
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code ignored;
            fs::remove(path, ignored);
        }
    } cleanup{temp};
#if defined(_WIN32)
    std::FILE* file = _fdopen(fd, "wb");
#else
    std::FILE* file = fdopen(fd, "wb");
#endif
    if (!file) {
#if defined(_WIN32)
        _close(fd);
#else
        ::close(fd);
#endif
        return fail("打开临时流");
    }
    bool ok = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    if (std::fflush(file) != 0)
        ok = false;
#if defined(_WIN32)
    if (_commit(_fileno(file)) != 0)
        ok = false;
#else
    if (fsync(fileno(file)) != 0)
        ok = false;
#endif
    if (std::fclose(file) != 0)
        ok = false;
    if (!ok)
        return fail("写入或同步");
#if defined(_WIN32)
    if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return fail("原子替换");
#else
    fs::rename(temp, target, ec);
    if (ec)
        return fail("原子替换");
#endif
    return true;
}

// 从已打开的文件取 64 位长度，不移动读取位置，也不把 ftell 的 -1 转成巨大无符号数。
inline bool local_file_size(std::FILE* file, uint64_t* size, std::string* err) {
    if (err)
        err->clear();
#if defined(_WIN32)
    struct _stat64 st{};
    const bool ok = file && _fstat64(_fileno(file), &st) == 0;
#else
    struct stat st{};
    const bool ok = file && fstat(fileno(file), &st) == 0;
#endif
    if (!ok || st.st_size < 0 || !size) {
        if (err)
            *err = "读取本地文件长度失败";
        return false;
    }
#if defined(_WIN32)
    const bool regular = (st.st_mode & _S_IFMT) == _S_IFREG;
#else
    const bool regular = S_ISREG(st.st_mode);
#endif
    if (!regular) {
        if (err)
            *err = "上传源必须是普通文件";
        return false;
    }
    *size = uint64_t(st.st_size);
    return true;
}

// FIFO 的普通 fopen 会在类型检查前等待写端；先非阻塞打开，再从描述符确认类型。
inline std::FILE* open_upload_file(const std::string& path) {
#if defined(_WIN32)
    std::FILE* file = open_local_file(path, "rb");
#else
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return nullptr;
    std::FILE* file = fdopen(fd, "rb");
    if (!file)
        ::close(fd);
#endif
    uint64_t size = 0;
    if (file && !local_file_size(file, &size, nullptr)) {
        std::fclose(file);
        return nullptr;
    }
    return file;
}

struct KnownHosts {
    std::string path = home_dir() + "/.aino_ssh/known_hosts";

    static std::string key_of(const std::string& host, int port) {
        return port == 22 ? host : fmt("[%s]:%d", host.c_str(), port);
    }

    // 0 = 已记录且一致；1 = 首次见到且写入成功；-1 = 不一致或文件读写失败。
    int verify(const std::string& host, int port, const std::string& fp, std::string* err) {
        static std::mutex mutex;
        std::lock_guard<std::mutex> guard(mutex);
        if (err)
            err->clear();
        const std::string key = key_of(host, port);
        const std::string prefix = key + " ";
        auto fail = [&](const std::string& message) {
            if (err)
                *err = message + "：" + path;
            return -1;
        };
        if (key.find_first_of("\r\n ") != std::string::npos || fp.empty() ||
            fp.find_first_of("\r\n ") != std::string::npos)
            return fail("主机密钥记录格式无效");
        std::error_code ec;
        const bool exists = std::filesystem::exists(path, ec);
        if (ec)
            return fail("无法检查主机密钥记录");
        std::string contents;
        if (exists) {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return fail("无法读取主机密钥记录");
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                contents += line + '\n';
                if (line.compare(0, prefix.size(), prefix) != 0)
                    continue;
                if (line.substr(prefix.size()) == fp)
                    return 0;
                return fail("主机密钥变了！确认服务器密钥后再手动更新记录");
            }
            if (!file.eof())
                return fail("读取主机密钥记录失败");
        }
        if (!atomic_write_file(path, contents + key + " " + fp + "\n", err))
            return -1;
        if (err)
            *err = "首次连接，已记录 " + key + " 的主机密钥";
        return 1;
    }
};

} // namespace sshc
