// SSH 示例的本地文件支持；不依赖网络库，允许离线验证持久化行为。
#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace sshc {

inline std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return std::string(buf);
}

inline std::string home_dir() {
#if defined(_WIN32)
    const char* p = std::getenv("USERPROFILE");
    if (!p) p = std::getenv("HOMEDRIVE");
#else
    const char* p = std::getenv("HOME");
#endif
    return p ? std::string(p) : std::string(".");
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
        if (err) err->clear();
        const std::string key = key_of(host, port);
        const std::string prefix = key + " ";
        auto fail = [&](const std::string& message) {
            if (err) *err = message + "：" + path;
            return -1;
        };
        if (key.find_first_of("\r\n ") != std::string::npos || fp.empty() ||
            fp.find_first_of("\r\n ") != std::string::npos)
            return fail("主机密钥记录格式无效");
        std::error_code ec;
        const bool exists = std::filesystem::exists(path, ec);
        if (ec) return fail("无法检查主机密钥记录");
        if (exists) {
            std::ifstream file(path, std::ios::binary);
            if (!file) return fail("无法读取主机密钥记录");
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.compare(0, prefix.size(), prefix) != 0) continue;
                if (line.substr(prefix.size()) == fp) return 0;
                return fail("主机密钥变了！确认服务器密钥后再手动更新记录");
            }
            if (!file.eof()) return fail("读取主机密钥记录失败");
        }
        const auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
        if (ec) return fail("无法创建主机密钥目录");
        std::ofstream file(path, std::ios::binary | std::ios::app);
        // 前置换行也支持手工编辑后末行没有换行符的记录。
        file << '\n' << key << ' ' << fp << '\n';
        file.close();
        if (!file) return fail("保存主机密钥失败，拒绝仅本次信任");
        if (err) *err = "首次连接，已记录 " + key + " 的主机密钥";
        return 1;
    }
};

} // namespace sshc
