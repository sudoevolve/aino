// =============================================================================
//  aino 示例 —— SSH 客户端
//
//  启动页按来源分组列出连接；支持多标签同时开多个会话。
//
//    · 连接：列表点选 / 新建连接；密码默认掩码，右侧眼睛可临时看明文
//    · 配置：本机保存（~/.aino_ssh/profiles.json）；
//            自动导入 ~/.ssh/config（VSCode Remote-SSH 用的就是它），
//            以及 VSCode 设置里 remote.SSH.configFile 指定的那份
//    · 终端：自带 VT100/xterm 解析器（vim、top、less 都能跑），256 色 + 真彩色、
//            备用屏、区域滚动、CJK 全角对齐、制表符按线绘制、回滚 4000 行
//    · 终端右键菜单：复制、粘贴、全选；快捷键 Ctrl+Shift+C / V / A
//    · 远程文件：搜索、右键操作、上传队列、下载和目录管理
//    · 多会话：每个标签独立连接、独立读线程、独立文件面板
//    · 跨平台：Windows / Linux / macOS 同一份源码。SSH 用 libssh2
//      （MSVC 默认依赖目录使用 libssh2/mbedTLS；Linux 用发行版包）
//
//  编译（Windows）：
//    cmake --build build --config Release --target aino_ssh
//    # 或绕过 CMake 直接调 cl：python tools/build_ssh.py app          # 见该脚本里的 libssh2 说明
//  编译（Linux）：
//    g++ -std=c++17 -O2 ssh_client.cpp -o ssh_client -lssh2 -lX11 -lpthread -ldl
//
//  作者：sudoevolve <sudoevolve@gmail.com>
// =============================================================================
#define AINO_IMPLEMENTATION
#include "../aino.hpp"

// X11 在后端实现中定义 None 宏；应用的同名枚举不应被它展开。
#if defined(AINO_PLATFORM_X11) && defined(None)
#undef None
#endif

#include "ssh_core.hpp"
#include "ssh_credentials.hpp"

#include <chrono>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#include <ole2.h>
#endif

using aino::Color;
using aino::Rect;
using aino::Style;
using sshc::Cell;
using sshc::Engine;
using sshc::Entry;
using sshc::FailStage;
using sshc::Terminal;

namespace {

// =============================================================================
//  一、剪贴板
//
//  系统剪贴板由 Window 后端实现；shell_capture 只用于打开外部编辑器。
#if !defined(_WIN32)
std::string shell_capture(const std::string& cmd) {
    std::string out;
    std::FILE* p = ::popen(cmd.c_str(), "r");
    if (!p)
        return out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0)
        out.append(buf, n);
    ::pclose(p);
    return out;
}
#endif

struct Clipboard {
    aino::Window* win = nullptr;
    bool set(const std::string& text) {
        return win && win->try_set_clipboard(text);
    }
    void request(std::function<void(std::string)> complete) {
        if (win) win->request_clipboard(std::move(complete));
        else complete({});
    }
};

// 强调色用于目录、选择状态、连接成功和主要操作。
const Color kTermBg = Color::hex(0x0B0C0F);
const Color kTermFg = Color::hex(0xD6D8E0);
const Color kAccent = Color::hex(0x6EA8FE);
const Color kOk = Color::hex(0x4ADE80);
const Color kBad = Color::hex(0xF87171);
const Color kWarn = Color::hex(0xFBBF24);

// 配置只保存 DPAPI 密文或系统凭据引用；旧格式仅用于迁移读取。
struct Profile {
    std::string name;
    std::string host;
    int port = 22;
    std::string user;
    std::string pass;
    std::string stored_pass;
    std::string saved_pass; // 上次成功恢复或提交的内容，用于复用未修改的系统凭据
    bool secret_unavailable = false;
    std::string key;
    bool remember = false; // 是否把密码写进文件
    int src = 0;           // 0 = 本机配置，1 = ~/.ssh/config，2 = VSCode 指定
};

const char* src_badge(int src) {
    switch (src) {
    case 1:
        return "ssh config";
    case 2:
        return "VSCode";
    default:
        return "本机";
    }
}

// 显式独立目录优先；否则通过系统账户 API 查询，避免继承的 USERPROFILE 指向其他账户。
std::string home_dir() {
    if (const char* p = std::getenv("AINO_SSH_HOME")) {
        if (*p)
            return p;
    }
#if defined(_WIN32)
    PWSTR pw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_DEFAULT, nullptr, &pw)) && pw) {
        std::wstring w(pw);
        CoTaskMemFree(pw);
        const int need =
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
        if (need > 0) {
            std::string s(size_t(need), '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), &s[0], need, nullptr, nullptr);
            if (!s.empty())
                return s;
        }
    }
    if (const char* p = std::getenv("USERPROFILE"))
        return p;
    const char* d = std::getenv("HOMEDRIVE");
    const char* q = std::getenv("HOMEPATH");
    if (d && q)
        return std::string(d) + q;
    return "C:";
#else
    if (const char* p = std::getenv("HOME"))
        return p;
    return "/tmp";
#endif
}

// 机器级（非当前用户）的 ssh 配置路径判定。
//
// OpenSSH 在两处放 ssh_config：每用户一份（~/.ssh/config，写"我连哪儿"），
// 每台机器一份（Windows 的 %ProgramData%\ssh\ssh_config、Linux 的
// /etc/ssh/ssh_config，那是**服务端/全局默认**，跟"这个人连过哪些机器"无关）。
// VSCode 的 Remote-SSH 两个都列出来让你选，扫进客户端只会多出一堆别人的东西，
// 所以这里按系统目录前缀直接挡掉，只留当前用户自己的那份。
bool is_machine_wide_path(const std::string& path) {
    auto lower = [](std::string s) {
        for (char& c : s)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string p = lower(path);
#if defined(_WIN32)
    // 反斜杠统一成正斜杠再比，避免 C:\ProgramData\... 与 C:/ProgramData/... 漏网
    std::string norm;
    norm.reserve(p.size());
    for (char c : p)
        norm.push_back(c == '\\' ? '/' : c);
    static const char* kSys[] = {"PROGRAMDATA",  "ALLUSERSPROFILE", "PROGRAMFILES", "PROGRAMFILES(X86)",
                                 "PROGRAMW6432", "WINDIR",          "SYSTEMROOT"};
    for (const char* e : kSys) {
        const char* v = std::getenv(e);
        if (!v || !*v)
            continue;
        std::string pre = lower(v);
        for (char& c : pre)
            if (c == '\\')
                c = '/';
        if (pre.empty())
            continue;
        if (norm.rfind(pre + "/", 0) == 0)
            return true;
    }
#else
    if (p.rfind("/etc/ssh/", 0) == 0 || p.compare(0, 5, "/etc/") == 0)
        return true;
#endif
    return false;
}

std::string cfg_file() {
    return home_dir() + "/.aino_ssh/profiles.json";
}
std::string download_file() {
    return home_dir() + "/.aino_ssh/download_dir.txt";
}

std::string json_esc(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':
            o += "\\\"";
            break;
        case '\\':
            o += "\\\\";
            break;
        case '\n':
            o += "\\n";
            break;
        case '\r':
            o += "\\r";
            break;
        case '\t':
            o += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", static_cast<unsigned char>(c));
                o += b;
            } else {
                o.push_back(c);
            }
        }
    }
    return o;
}

using sshc::append_utf8;

// 极简 JSON 扫描器：只认本文件写出来的那种结构（数组 + 扁平对象 + 字符串/数字/布尔）
struct JsonScan {
    const std::string& s;
    size_t i = 0;
    explicit JsonScan(const std::string& t) : s(t) {}

    void ws() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
    }
    bool eat(char c) {
        ws();
        if (i < s.size() && s[i] == c) {
            ++i;
            return true;
        }
        return false;
    }
    bool peek(char c) {
        ws();
        return i < s.size() && s[i] == c;
    }

    bool str(std::string* out) {
        ws();
        if (i >= s.size() || s[i] != '"')
            return false;
        ++i;
        out->clear();
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 0x20)
                return false;
            if (c != '\\') {
                out->push_back(c);
                continue;
            }
            if (i >= s.size())
                break;
            const char e = s[i++];
            switch (e) {
            case 'n':
                out->push_back('\n');
                break;
            case 'r':
                out->push_back('\r');
                break;
            case 't':
                out->push_back('\t');
                break;
            case 'b':
                out->push_back('\b');
                break;
            case 'f':
                out->push_back('\f');
                break;
            case '"':
            case '\\':
            case '/':
                out->push_back(e);
                break;
            case 'u': {
                auto hex4 = [&](unsigned& u) {
                    if (i + 4 > s.size())
                        return false;
                    u = 0;
                    for (int k = 0; k < 4; ++k) {
                        const unsigned char h = s[i++];
                        int digit = h >= '0' && h <= '9'   ? h - '0'
                                    : h >= 'a' && h <= 'f' ? h - 'a' + 10
                                    : h >= 'A' && h <= 'F' ? h - 'A' + 10
                                                           : -1;
                        if (digit < 0)
                            return false;
                        u = u * 16 + unsigned(digit);
                    }
                    return true;
                };
                unsigned u = 0;
                if (!hex4(u))
                    return false;
                if (u >= 0xD800 && u <= 0xDBFF) {
                    if (i + 2 > s.size() || s[i] != '\\' || s[i + 1] != 'u')
                        return false;
                    i += 2;
                    unsigned low = 0;
                    if (!hex4(low) || low < 0xDC00 || low > 0xDFFF)
                        return false;
                    u = 0x10000 + (u - 0xD800) * 0x400 + low - 0xDC00;
                } else if (u >= 0xDC00 && u <= 0xDFFF)
                    return false;
                append_utf8(*out, u);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }
    bool num(long long* out) {
        ws();
        const size_t st = i;
        if (i < s.size() && s[i] == '-')
            ++i;
        const size_t digits = i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
            ++i;
        if (i == digits || (s[digits] == '0' && i > digits + 1))
            return false;
        errno = 0;
        *out = std::strtoll(s.substr(st, i - st).c_str(), nullptr, 10);
        return errno != ERANGE;
    }
    bool skip(unsigned depth = 0) {
        if (depth >= 128) return false;
        ws();
        if (i >= s.size()) return false;
        if (s[i] == '"') {
            std::string t;
            return str(&t);
        }
        if (s[i] == '{' || s[i] == '[') {
            const bool object = s[i++] == '{';
            const char close = object ? '}' : ']';
            if (eat(close)) return true;
            do {
                if (object) {
                    std::string key;
                    if (!str(&key) || !eat(':')) return false;
                }
                if (!skip(depth + 1)) return false;
                if (eat(close)) return true;
            } while (eat(','));
            return false;
        }
        for (const char* literal : {"true", "false", "null"}) {
            const size_t length = std::strlen(literal);
            if (s.compare(i, length, literal) == 0) { i += length; return true; }
        }
        if (s[i] == '-') ++i;
        if (i >= s.size()) return false;
        auto digit = [&] { return i < s.size() && s[i] >= '0' && s[i] <= '9'; };
        if (s[i] == '0') ++i;
        else {
            if (!digit()) return false;
            while (digit()) ++i;
        }
        if (i < s.size() && s[i] == '.') {
            ++i;
            if (!digit()) return false;
            while (digit()) ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            if (!digit()) return false;
            while (digit()) ++i;
        }
        return true;
    }
};

// 配置状态只由配置任务访问；UI 在完成后接收整个快照。
struct ProfileStore {
    std::string warning;
    bool writable = true;
    bool loaded = false;
    std::string original;
    std::vector<Profile> load();
    bool save(std::vector<Profile>& all, std::string* err = nullptr);
};

std::vector<Profile> ProfileStore::load() {
    std::vector<Profile> out;
    warning.clear();
    writable = true;
    loaded = true;
    original.clear();
    std::FILE* f = sshc::open_local_file(cfg_file(), "rb");
    if (!f) {
        if (errno != ENOENT) {
            writable = false;
            warning = "连接配置无法读取，已禁止覆盖保存";
        }
        return out;
    }
    std::string txt;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        txt.append(buf, n);
    const bool read_failed = std::ferror(f) != 0;
    std::fclose(f);
    original = txt;
    if (read_failed) {
        writable = false;
        warning = "连接配置读取失败，已禁止覆盖保存";
        return out;
    }

    bool parsed = true;
    bool known_schema = true;
    warning.clear();
    JsonScan document(txt);
    const bool valid = document.peek('[') && document.skip();
    document.ws();
    if (!valid || document.i != txt.size()) {
        writable = false;
        warning = "连接配置格式不完整，已保留原文并禁止覆盖保存";
        return out;
    }
    JsonScan js(txt);
    if (!js.eat('[')) {
        writable = false;
        warning = "连接配置不是完整数组，已禁止覆盖保存";
        return out;
    }
    while (!js.peek(']') && js.i < txt.size()) {
        if (!js.eat('{')) {
            parsed = false;
            break;
        }
        Profile p;
        while (!js.peek('}') && js.i < txt.size()) {
            std::string k;
            if (!js.str(&k) || !js.eat(':')) {
                parsed = false;
                break;
            }
            if (k == "name")
                parsed = js.str(&p.name);
            else if (k == "host")
                parsed = js.str(&p.host);
            else if (k == "user")
                parsed = js.str(&p.user);
            else if (k == "key")
                parsed = js.str(&p.key);
            else if (k == "pass")
                parsed = js.str(&p.stored_pass);
            else if (k == "port") {
                long long v = 22;
                parsed = js.num(&v) && v >= 1 && v <= 65535;
                if (parsed)
                    p.port = int(v);
            } else if (k == "remember") {
                long long v = 0;
                parsed = js.num(&v);
                if (parsed)
                    p.remember = v != 0;
            } else {
                known_schema = false;
                parsed = js.skip();
            }
            if (!parsed || (!js.peek('}') && !js.eat(','))) {
                parsed = false;
                break;
            }
        }
        if (!parsed || !js.eat('}')) {
            parsed = false;
            break;
        }
        if (!js.peek(']') && !js.eat(',')) {
            parsed = false;
            break;
        }
        if (!p.stored_pass.empty()) {
            if (p.stored_pass.rfind("dpapi:", 0) == 0 || p.stored_pass.rfind("vault:", 0) == 0) {
                p.secret_unavailable = !sshc::restore_password(p.stored_pass, &p.pass);
                if (p.secret_unavailable)
                    warning = "部分系统凭据无法读取，请解锁系统凭据服务或重新输入密码";
            } else {
                p.secret_unavailable = true;
                warning = "密码记录格式不受支持，已保留原记录；请重新输入密码";
            }
        }
        p.saved_pass = p.pass;
        if (!p.host.empty() || !p.name.empty())
            out.push_back(p);
    }
    // 不完整或未识别的配置只能读取，不能用部分已知字段覆盖原文。
    const bool complete = js.eat(']');
    js.ws();
    if (!parsed || !complete || js.i != txt.size()) {
        writable = false;
        warning = "连接配置不完整，已禁止覆盖保存；请先修复配置文件";
    } else if (!known_schema) {
        writable = false;
        warning = "配置含未知字段，已保留原文并禁止覆盖；请先备份并确认配置格式";
    }
    return out;
}

bool ProfileStore::save(std::vector<Profile>& all, std::string* err) {
    if (!loaded) (void)load();
    if (err)
        err->clear();
    if (!writable) {
        if (err) *err = "原配置不完整或含未知字段，未覆盖保存；请先备份并修复配置";
        return false;
    }
    std::vector<std::string> old_secrets, new_secrets, protected_values;
    if (std::FILE* file = sshc::open_local_file(cfg_file(), "rb")) {
        std::string previous;
        char buffer[4096];
        size_t count;
        while ((count = std::fread(buffer, 1, sizeof(buffer), file)))
            previous.append(buffer, count);
        const bool read_failed = std::ferror(file) != 0;
        std::fclose(file);
        if (previous != original) {
            if (err) *err = "配置已被其他程序修改，请重新导入后再保存";
            return false;
        }
        JsonScan document(previous);
        const bool valid = document.peek('[') && document.skip();
        document.ws();
        if (read_failed || !valid || document.i != previous.size()) {
            if (err) *err = "原配置读取失败或不完整，未覆盖保存";
            return false;
        }
        JsonScan scan(previous);
        while (scan.i < previous.size()) {
            std::string key, value;
            if (scan.str(&key)) {
                if (key == "pass" && scan.eat(':') && scan.str(&value) && value.rfind("vault:", 0) == 0)
                    old_secrets.push_back(value);
            } else
                ++scan.i;
        }
    }
    else if (errno != ENOENT || !original.empty()) {
        if (err) *err = "原配置无法读取或已被移除，请重新导入后再保存";
        return false;
    }
    bool committed = false;
    aino::detail::ScopeExit rollback([&] {
        if (!committed)
            for (const auto& secret : new_secrets)
                (void)sshc::erase_password(secret);
    });
    std::string txt = "[\n";
    bool first = true;
    for (const Profile& p : all) {
        if (p.src != 0)
            continue; // 导入的只读，不回写
        if (!first)
            txt += ",\n";
        first = false;
        txt += "  {\"name\":\"" + json_esc(p.name) +
               "\""
               ", \"host\":\"" +
               json_esc(p.host) +
               "\""
               ", \"port\":" +
               std::to_string(p.port) + ", \"user\":\"" + json_esc(p.user) +
               "\""
               ", \"key\":\"" +
               json_esc(p.key) +
               "\""
               ", \"pass\":\"";
        std::string stored;
        if (p.remember && !p.stored_pass.empty() && !p.secret_unavailable && p.pass == p.saved_pass) {
            stored = p.stored_pass;
        } else if (p.remember && !p.pass.empty()) {
            const std::string identity = cfg_file() + "|" + std::to_string(p.host.size()) + ":" + p.host +
                                         "|" + std::to_string(p.port) + "|" + std::to_string(p.user.size()) +
                                         ":" + p.user + "|" + p.name;
            if (!sshc::protect_password(identity, p.pass, &stored, err))
                return false;
            new_secrets.push_back(stored);
        } else if (p.remember && p.secret_unavailable)
            stored = p.stored_pass;
        protected_values.push_back(stored);
        txt += json_esc(stored);
        txt += "\", \"remember\":" + std::string(p.remember ? "1" : "0") + "}";
    }
    txt += "\n]\n";
    if (!sshc::atomic_write_file(cfg_file(), txt, err))
        return false;
    committed = true;
    original = txt;
    size_t index = 0;
    for (auto& profile : all)
        if (profile.src == 0) {
            profile.stored_pass = protected_values[index++];
            profile.saved_pass = profile.pass;
            if (!profile.pass.empty()) profile.secret_unavailable = false;
        }
    bool cleanup_failed = false;
    for (const auto& secret : old_secrets)
        if (std::find(protected_values.begin(), protected_values.end(), secret) == protected_values.end() &&
            !sshc::erase_password(secret))
            cleanup_failed = true;
    if (cleanup_failed) {
        warning = "连接已保存，但部分旧凭据未能清理，请在系统凭据服务中检查 aino SSH 条目";
        if (err)
            *err = warning;
    }
    return true;
}

std::string expand_home(std::string p) {
    if (p.rfind("~/", 0) == 0)
        return home_dir() + p.substr(1);
    if (p == "~")
        return home_dir();
    return p;
}

// 解析 ssh_config 语法（Host / HostName / User / Port / IdentityFile）。
// VSCode Remote-SSH 读的就是这个文件，所以"适配 VSCode"= 把这份 config 读进来。
std::vector<Profile> parse_ssh_config(const std::string& path, int src) {
    std::vector<Profile> out;
    std::FILE* f = sshc::open_local_file(path, "rb");
    if (!f)
        return out;
    std::vector<std::string> lines;
    {
        std::string all;
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
            all.append(buf, n);
        std::fclose(f);
        size_t st = 0;
        while (st <= all.size()) {
            size_t e = all.find('\n', st);
            if (e == std::string::npos)
                e = all.size();
            std::string ln = all.substr(st, e - st);
            if (!ln.empty() && ln.back() == '\r')
                ln.pop_back();
            lines.push_back(ln);
            st = e + 1;
        }
    }

    struct Block {
        std::vector<std::string> aliases;
        Profile p;
    };
    std::vector<Block> blocks;
    Block* cur = nullptr;
    for (const std::string& raw : lines) {
        // 去注释 + 去首尾空白
        std::string ln = raw;
        const size_t hash = ln.find('#');
        if (hash != std::string::npos)
            ln.resize(hash);
        const size_t b = ln.find_first_not_of(" \t");
        if (b == std::string::npos)
            continue;
        const size_t e = ln.find_last_not_of(" \t");
        ln = ln.substr(b, e - b + 1);

        std::string key, val;
        const size_t sp = ln.find_first_of(" \t=");
        if (sp == std::string::npos) {
            key = ln;
        } else {
            key = ln.substr(0, sp);
            size_t v = ln.find_first_not_of(" \t=", sp);
            if (v != std::string::npos)
                val = ln.substr(v);
        }
        std::string kl = key;
        for (char& c : kl)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        if (kl == "host") {
            blocks.push_back(Block{});
            cur = &blocks.back();
            // 一个 Host 行可以有多个别名
            size_t i = 0;
            while (i < val.size()) {
                while (i < val.size() && (val[i] == ' ' || val[i] == '\t'))
                    ++i;
                const size_t s2 = i;
                while (i < val.size() && val[i] != ' ' && val[i] != '\t')
                    ++i;
                if (i > s2)
                    cur->aliases.push_back(val.substr(s2, i - s2));
            }
        } else if (!cur) {
            continue; // 全局段（Host 之前）不管
        } else if (kl == "hostname") {
            cur->p.host = val;
        } else if (kl == "user") {
            cur->p.user = val;
        } else if (kl == "port") {
            cur->p.port = std::atoi(val.c_str()) > 0 ? std::atoi(val.c_str()) : 22;
        } else if (kl == "identityfile") {
            cur->p.key = expand_home(val);
        }
    }
    for (Block& b : blocks) {
        for (const std::string& alias : b.aliases) {
            if (alias.find('*') != std::string::npos || alias.find('?') != std::string::npos)
                continue;
            Profile p = b.p;
            p.src = src;
            p.name = alias; // 卡片标题用别名（和 ssh 命令行一致）
            if (p.host.empty())
                p.host = alias; // 没写 HostName 就是别名本身
            out.push_back(p);
        }
    }
    std::sort(out.begin(), out.end(), [](const Profile& x, const Profile& y) {
        return x.name < y.name;
    });
    return out;
}

// VSCode 的 Remote-SSH 默认就用 ~/.ssh/config；只有改过 remote.SSH.configFile
// 才会指向别的文件。这里读一下 settings.json 把那个路径捞出来。
std::string vscode_config_file() {
    std::vector<std::string> cands;
#if defined(_WIN32)
    if (const char* a = std::getenv("APPDATA")) {
        cands.push_back(std::string(a) + "/Code/User/settings.json");
        cands.push_back(std::string(a) + "/Code - Insiders/User/settings.json");
    }
#elif defined(__APPLE__)
    cands.push_back(home_dir() + "/Library/Application Support/Code/User/settings.json");
#else
    cands.push_back(home_dir() + "/.config/Code/User/settings.json");
#endif
    for (const std::string& c : cands) {
        std::FILE* f = sshc::open_local_file(c, "rb");
        if (!f)
            continue;
        std::string txt;
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
            txt.append(buf, n);
        std::fclose(f);
        const std::string key = "\"remote.SSH.configFile\"";
        size_t at = txt.find(key);
        if (at == std::string::npos)
            continue;
        at = txt.find(':', at + key.size());
        if (at == std::string::npos)
            continue;
        ++at;
        while (at < txt.size() && (txt[at] == ' ' || txt[at] == '\t'))
            ++at;
        if (at >= txt.size() || txt[at] != '"')
            continue;
        ++at;
        std::string val;
        while (at < txt.size() && txt[at] != '"') {
            if (txt[at] == '\\' && at + 1 < txt.size()) {
                ++at;
                if (txt[at] == 'n')
                    val.push_back('\n');
                else
                    val.push_back(txt[at]);
            } else {
                val.push_back(txt[at]);
            }
            ++at;
        }
        if (!val.empty())
            return expand_home(val);
    }
    return std::string();
}

// username 留空时按 OpenSSH 的习惯用当前系统账号，否则 libssh2 根本没法认证
std::string current_user() {
#if defined(_WIN32)
    if (const char* u = std::getenv("USERNAME"))
        return u;
#else
    if (const char* u = std::getenv("USER"))
        return u;
    if (const char* u = std::getenv("LOGNAME"))
        return u;
#endif
    return std::string();
}

std::string ssh_config_path() {
    return home_dir() + "/.ssh/config";
}

// 给界面看的路径：Windows 上用反斜杠。
// home_dir() 返回的是 "C:\Users\admin"，拼上 "/.ssh/config" 就变成
// "C:\Users\admin/.ssh/config" 这种花脸，真拿去复制粘贴还容易出错。
std::string display_path(std::string p) {
#if defined(_WIN32)
    for (char& c : p)
        if (c == '/')
            c = '\\';
#endif
    return p;
}

// 直接交给系统编辑器；没有文件关联时，Windows 使用系统记事本。
bool open_external(const std::string& path, std::string* error = nullptr) {
#if defined(_WIN32)
    const std::wstring wide = std::filesystem::u8path(path).wstring();
    INT_PTR result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        wchar_t system_dir[MAX_PATH]{};
        const UINT length = GetSystemDirectoryW(system_dir, MAX_PATH);
        if (length && length < MAX_PATH) {
            const std::wstring editor = std::wstring(system_dir) + L"\\notepad.exe";
            const std::wstring argument = L"\"" + wide + L"\"";
            result = reinterpret_cast<INT_PTR>(
                ShellExecuteW(nullptr, L"open", editor.c_str(), argument.c_str(), nullptr, SW_SHOWNORMAL));
        }
    }
    if (result > 32)
        return true;
    if (error)
        *error = "系统编辑器启动失败（" + std::to_string(result) + "）";
    return false;
#else
    // 对 shell 参数逐个转义，文件名中的空格、引号和 $ 不作为命令解释。
    std::string quoted = "'";
    for (char c : path)
        quoted += c == '\'' ? "'\\''" : std::string(1, c);
    quoted += "'";
#if defined(__APPLE__)
    const std::string command = "open -t " + quoted;
#else
    const std::string command = "xdg-open " + quoted;
#endif
    if (std::system((command + " >/dev/null 2>&1 &").c_str()) == 0)
        return true;
    if (error)
        *error = "无法请求系统编辑器，请检查文件关联";
    return false;
#endif
}

// 仅在配置文件不存在时建立注释模板；原文件字节保持不变。
bool ensure_ssh_config(const std::string& path = ssh_config_path(), std::string* error = nullptr) {
    const auto file = std::filesystem::u8path(path);
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
        if (std::filesystem::is_regular_file(file, ec) && !ec)
            return true;
        if (error)
            *error = "SSH 配置路径不是普通文件：" + display_path(path);
        return false;
    }
    if (ec) {
        if (error)
            *error = "无法访问 SSH 配置：" + ec.message();
        return false;
    }
    return sshc::atomic_write_file(path,
                                   "# SSH client configuration\n"
                                   "# Host my-server\n"
                                   "#   HostName 192.168.1.10\n"
                                   "#   User user\n"
                                   "#   Port 22\n"
                                   "#   IdentityFile ~/.ssh/id_ed25519\n"
                                   "# Save, then reload connections in aino SSH.\n",
                                   error);
}

std::vector<Profile> load_all_profiles(ProfileStore& store) {
    std::vector<Profile> all = store.load();
    auto add = [&](const std::vector<Profile>& v) {
        for (const Profile& p : v) {
            // 按主机、用户和端口去重，本地配置优先。
            bool dup = false;
            for (const Profile& e : all) {
                if (e.host == p.host && e.user == p.user && e.port == p.port) {
                    dup = true;
                    break;
                }
            }
            if (!dup)
                all.push_back(p);
        }
    };
    // 只读**当前登录用户自己的** ~/.ssh/config（路径由 home_dir() 现算）。
    // 机器级那份（C:\ProgramData\ssh\ssh_config、/etc/ssh/ssh_config）不扫：
    // 它属于 OpenSSH 全局默认，跟"这个人连过哪些机器"没关系。
    add(parse_ssh_config(ssh_config_path(), 1));
    const std::string vc = vscode_config_file();
    if (!vc.empty() && vc != ssh_config_path() && !is_machine_wide_path(vc))
        add(parse_ssh_config(vc, 2));

    // 这里**只认 ssh config 文件里真正写了的东西**。
    // 曾经还会把 VSCode settings.json 里 remote.SSH.remotePlatform 的 key 也补成卡片
    // （那是 VSCode 的"连过哪些机器"历史记录），结果用户的 config 里只有一台，
    // 列表里却冒出三台压根没写过的机器 —— 分不清哪个是真配置、哪个是历史残留。
    return all;
}

std::string load_download_dir() {
    std::FILE* f = sshc::open_local_file(download_file(), "rb");
    if (!f)
        return home_dir();
    std::string s;
    char buf[1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        s.append(buf, n);
    std::fclose(f);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s.empty() ? home_dir() : s;
}

bool save_download_dir(const std::string& d, std::string* err = nullptr) {
    return sshc::atomic_write_file(download_file(), d, err);
}

// =============================================================================
//  四、系统目录选择框
//
//  终端里的路径输入框只能手打，选目录还是得弹系统的框。Windows 用
//  SHBrowseForFolder（不依赖 COM 初始化，shell32 就够）；Linux 试 zenity /
//  kdialog，都没有就退化成手输。
// =============================================================================
#if defined(_WIN32)
std::wstring to_wide(const std::string& s) {
    if (s.empty())
        return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}
std::string from_wide(const wchar_t* w) {
    if (!w)
        return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    s.pop_back();
    return s;
}
int CALLBACK set_initial_folder(HWND dialog, UINT message, LPARAM, LPARAM initial) {
    if (message == BFFM_INITIALIZED && initial)
        SendMessageW(dialog, BFFM_SETSELECTIONW, TRUE, initial);
    return 0;
}
#endif

std::string pick_folder(const std::string& initial) {
#if defined(_WIN32)
    BROWSEINFOW bi{};
    bi.hwndOwner = GetActiveWindow();
    bi.lpszTitle = L"选择下载保存目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    const std::wstring w = to_wide(initial);
    bi.lpfn = set_initial_folder;
    bi.lParam = reinterpret_cast<LPARAM>(w.c_str());
    LPITEMIDLIST idl = SHBrowseForFolderW(&bi);
    std::string out;
    if (idl) {
        wchar_t path[MAX_PATH]{};
        if (SHGetPathFromIDListW(idl, path))
            out = from_wide(path);
        CoTaskMemFree(idl);
    }
    return out;
#else
    // Linux/macOS：让系统自己的目录选择器干这活儿
    static const char* cmds[] = {
        "zenity --file-selection --directory --title='选择下载保存目录' 2>/dev/null",
        "kdialog --getexistingdirectory \"$HOME\" 2>/dev/null",
        "osascript -e 'POSIX path of (choose folder)' 2>/dev/null",
    };
    for (const char* c : cmds) {
        const std::string r = shell_capture(c);
        std::string s = r;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
            s.pop_back();
        if (!s.empty())
            return s;
    }
    (void)initial;
    return std::string();
#endif
}

std::vector<std::string> pick_upload_files(const std::string& initial) {
#if defined(_WIN32)
    std::vector<wchar_t> file(32768, L'\0');
    const std::wstring directory = to_wide(initial);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFile = file.data();
    dialog.nMaxFile = DWORD(file.size());
    dialog.lpstrFilter = L"All files\0*.*\0\0";
    dialog.lpstrTitle = L"选择要上传的文件";
    dialog.lpstrInitialDir = directory.empty() ? nullptr : directory.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_ALLOWMULTISELECT;
    if (!GetOpenFileNameW(&dialog))
        return {};
    const wchar_t* next = file.data() + std::wcslen(file.data()) + 1;
    if (!*next)
        return {from_wide(file.data())};
    const std::filesystem::path folder(file.data());
    std::vector<std::string> files;
    while (*next) {
        files.push_back((folder / next).u8string());
        next += std::wcslen(next) + 1;
    }
    return files;
#else
    (void)initial;
    const char* commands[] = {"zenity --file-selection --title='选择要上传的文件' 2>/dev/null",
                              "kdialog --getopenfilename \"$HOME\" 2>/dev/null",
                              "osascript -e 'POSIX path of (choose file)' 2>/dev/null"};
    for (const char* command : commands) {
        std::string result = shell_capture(command);
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
        if (!result.empty())
            return {result};
    }
    return {};
#endif
}

// =============================================================================
//  五、文件操作的异步层
//
//  为什么必须异步：libssh2 的 SFTP 调用是**阻塞**的（等目录包、等 ACK）。
//  在 UI 线程里调它，远端慢一点、或者服务器根本没开 SFTP 子系统，
//  窗口就整个冻住（实测：点一下目录卡 20 秒，用户以为程序死了）。
//  所以 UI 线程只往队列里塞请求，真正干活的是工作线程，结果回到主线程再合并。
// =============================================================================
enum class FsOp { List, Mkdir, Rename, Remove };

struct FsReq {
    FsOp op = FsOp::List;
    std::string a;
    std::string b;
    std::string tag; // 成功后给用户看的提示（空=不说）
};

enum class Prompt { None, Mkdir, Rename, Paste };

struct MenuState {
    bool open = false;
    aino::Vec2 anchor;
    int pressed = -1;
};

struct UploadJob {
    std::string local, remote;
};

// 一个会话 = 一条 SSH 连接 + 一个终端 + 一块文件面板
struct Session : std::enable_shared_from_this<Session> {
    uint64_t ui_scope = 0; // 本窗口内稳定且不复用，关闭时释放内部 UI 状态
    // ---- pty 尺寸同步 ----
    // 用来节流：远端拒收 window-change 时不要每帧都发一个请求。
    int pty_req_cols = -1, pty_req_rows = -1;
    std::chrono::steady_clock::time_point pty_req_at{};

    // ---- 连接 ----
    std::string host;
    int port = 22;
    std::string user;
    std::string pass;
    std::string key;
    std::string title = "会话";

    // 连接状态由 conn_edit 加锁修改，UI 每帧复制快照，避免跨线程访问已释放的字符串缓冲。
    struct ConnInfo {
        bool connecting = false;
        std::string msg = "未连接";
        std::string err;
        FailStage fail_at = FailStage::None; // 失败在哪一层：网络/握手/认证
        std::string sftp_err;                // SFTP 起不来的原因（终端不受影响）
    };
    mutable std::mutex conn_mtx;
    ConnInfo conn;

    std::atomic<bool> retired{false}; // 后台清理完成，可以被回收
    std::atomic<bool> shutdown_done{false};

    ConnInfo conn_snapshot() const {
        std::lock_guard<std::mutex> lk(conn_mtx);
        return conn;
    }
    bool conn_busy() const {
        std::lock_guard<std::mutex> lk(conn_mtx);
        return conn.connecting;
    }
    void conn_edit(const std::function<void(ConnInfo&)>& f) {
        std::lock_guard<std::mutex> lk(conn_mtx);
        f(conn);
    }

    Engine engine;
    Terminal term;
    Clipboard* clip = nullptr;
    std::thread conn_thread;

    struct TransferView {
        std::string name, error;
        uint64_t done = 0, total = 0;
        bool busy = false, finished = false;
    };
    mutable std::mutex xfer_mtx;
    TransferView xfer_view;
    std::atomic<bool> xfer_running{false}, xfer_cancel{false};
    std::thread xfer_thread;
    std::deque<UploadJob> uploads;
    size_t upload_errors = 0;
    std::string last_transfer_error;
    bool refresh_after_transfer = false;

    TransferView transfer_snapshot() const {
        std::lock_guard<std::mutex> lk(xfer_mtx);
        return xfer_view;
    }

    // ---- 终端 ----
    bool term_focus = false;
    bool selecting = false;
    MenuState term_menu;
    float font = 13.0f;
    int view_offset = 0;
    bool follow = true;
    Rect r_term{};

    // ---- 文件 ----
    std::string sftp_path, path_edit, file_filter;
    bool path_focus = false;
    bool file_focus = false;
    bool file_drag_over = false;
    MenuState file_menu;
    bool reset_file_scroll = true;
    std::vector<Entry> entries;
    int sel = -1;
    Prompt prompt = Prompt::None;
    std::string prompt_text, prompt_src, prompt_dir;
    bool confirm_delete = false;
    std::string notice;
    bool notice_bad = false;
    double notice_at = 0.0;

    // ---- 后台文件操作 ----
    std::mutex fs_mtx;
    std::condition_variable fs_cv;
    bool fs_quit = false;
    bool fs_has_req = false;
    std::atomic<bool> fs_busy{false};
    FsReq fs_req;
    bool fs_done = false;
    FsReq fs_done_req;
    std::vector<Entry> fs_done_entries;
    std::string fs_done_err;
    std::string fs_done_cwd;
    std::string fs_done_tag;
    std::thread fs_thread;

    Rect r_list{};

    ~Session() {
        shutdown();
    }

    // 关标签 / 断开 / 退出程序都走这一个入口。顺序不能反：
    //   先等连接线程彻底退出（它内部可能刚 start_reader()/open_sftp()），
    //   再停文件线程，最后才 disconnect。
    // 反过来做（先 disconnect 再 join conn_thread）会把 reader 线程留在
    // 已经释放的 session 上继续跑 —— 那正是"断开就闪退"的来源。
    // 幂等：正常路径已经由后台清理线程调过一次，析构时直接返回。
    void shutdown() {
        if (shutdown_done.exchange(true))
            return;
        xfer_cancel = true;
        engine.request_stop();
        if (conn_thread.joinable())
            conn_thread.join();
        if (xfer_thread.joinable())
            xfer_thread.join();
        stop_fs_worker();
        engine.disconnect();
        clip = nullptr;
        retired = true;
    }

    void stop_fs_worker();
    bool fs_push(const FsReq& r);
};

void fs_worker(Session* s, aino::Window* win) {
    for (;;) {
        FsReq r;
        {
            std::unique_lock<std::mutex> lk(s->fs_mtx);
            s->fs_cv.wait(lk, [&] {
                return s->fs_quit || s->fs_has_req;
            });
            if (s->fs_quit)
                return;
            r = s->fs_req;
            s->fs_has_req = false;
            s->fs_busy = true;
        }

        std::vector<Entry> got;
        std::string err, tag, cwd;
        try {
            switch (r.op) {
            case FsOp::List:
                if (s->engine.list(r.a, &got, &err))
                    cwd = s->engine.cwd_snapshot();
                break;
            case FsOp::Mkdir:
                if (s->engine.mkdir(r.a, &err))
                    tag = r.tag;
                break;
            case FsOp::Rename:
                if (s->engine.rename(r.a, r.b, &err))
                    tag = r.tag;
                break;
            case FsOp::Remove:
                if (s->engine.remove(r.a, &err))
                    tag = r.tag;
                break;
            }
        } catch (const std::exception& e) {
            err = std::string("文件操作失败：") + e.what();
        }

        {
            std::lock_guard<std::mutex> lk(s->fs_mtx);
            s->fs_done = true;
            s->fs_done_req = r;
            s->fs_done_entries.swap(got);
            s->fs_done_err = err;
            s->fs_done_cwd = cwd;
            s->fs_done_tag = tag;
            s->fs_busy = false;
        }
        if (win)
            win->wake();
    }
}

void Session::stop_fs_worker() {
    {
        std::lock_guard<std::mutex> lk(fs_mtx);
        fs_quit = true;
    }
    fs_cv.notify_all();
    if (fs_thread.joinable())
        fs_thread.join();
}

bool Session::fs_push(const FsReq& r) {
    {
        std::lock_guard<std::mutex> lk(fs_mtx);
        if (fs_quit || fs_has_req || fs_busy || fs_done)
            return false;
        fs_req = r;
        fs_has_req = true;
    }
    fs_cv.notify_one();
    return true;
}

void set_notice(Session& s, const std::string& msg, bool bad) {
    s.notice = msg;
    s.notice_bad = bad;
    s.notice_at = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool file_work_pending(Session& session) {
    std::lock_guard<std::mutex> lock(session.fs_mtx);
    return session.fs_has_req || session.fs_busy || session.fs_done;
}

bool start_transfer(Session& s, bool upload, const std::string& local, const std::string& remote,
                    aino::Window* win, bool replace_existing = true) {
    if (s.conn_busy() || !s.engine.connected() || !s.engine.sftp_ready()) {
        set_notice(s, "SFTP 尚未就绪", true);
        return false;
    }
    if (s.xfer_running.exchange(true)) {
        set_notice(s, "已有文件正在传输", true);
        return false;
    }
    if (s.xfer_thread.joinable())
        s.xfer_thread.join();
    s.xfer_cancel = false;
    {
        std::lock_guard<std::mutex> lk(s.xfer_mtx);
        s.xfer_view = {};
        s.xfer_view.name = Engine::base_name(upload ? local : remote);
        s.xfer_view.busy = true;
    }
    // 传输线程提交分块命令；只有 Engine 的 I/O owner 调用网络 API，UI 读取独立快照。
    try {
        s.xfer_thread = std::thread([&s, upload, local, remote, win, replace_existing] {
            try {
                if (upload)
                    s.engine.start_upload(local, remote, &s.xfer_cancel, replace_existing);
                else
                    s.engine.start_download(remote, local, false, &s.xfer_cancel, replace_existing);
                auto next_update = std::chrono::steady_clock::now();
                while (s.engine.transfer_busy() && !s.xfer_cancel) {
                    s.engine.xfer_step(16u << 10, &s.xfer_cancel);
                    const auto now = std::chrono::steady_clock::now();
                    if (now >= next_update) {
                        const auto& t = s.engine.transfer_snapshot();
                        {
                            std::lock_guard<std::mutex> lk(s.xfer_mtx);
                            s.xfer_view = {t.name, t.error, t.done, t.total, true, false};
                        }
                        if (win)
                            win->wake();
                        next_update = now + std::chrono::milliseconds(33);
                    }
                }
                if (s.xfer_cancel && s.engine.transfer_busy())
                    s.engine.cancel_transfer();
                const auto& t = s.engine.transfer_snapshot();
                {
                    std::lock_guard<std::mutex> lk(s.xfer_mtx);
                    s.xfer_view = {t.name, t.error, t.done, t.total, false, true};
                }
                s.engine.clear_transfer();
            } catch (const std::exception& e) {
                // 异常不能越过线程入口；先中断会话，等待 Session::shutdown 统一释放。
                s.engine.request_stop();
                std::lock_guard<std::mutex> lk(s.xfer_mtx);
                s.xfer_view.error = std::string("传输失败：") + e.what();
                s.xfer_view.busy = false;
                s.xfer_view.finished = true;
            }
            s.xfer_running = false;
            if (win)
                win->wake();
        });
    } catch (const std::system_error& error) {
        s.xfer_running = false;
        {
            std::lock_guard<std::mutex> lock(s.xfer_mtx);
            s.xfer_view = {};
        }
        set_notice(s, std::string("无法启动传输：") + error.what(), true);
        return false;
    }
    return true;
}

// 连接放后台线程：握手最多十几秒，UI 不能冻
void begin_connect(Session& s, aino::Window& win) {
    if (s.conn_busy())
        return;
    if (s.xfer_running || !s.uploads.empty()) {
        set_notice(s, "请先取消当前传输", true);
        return;
    }
    if (s.conn_thread.joinable())
        s.conn_thread.join();
    s.sftp_path.clear();
    s.path_edit.clear();
    s.entries.clear();
    s.sel = -1;
    s.reset_file_scroll = true;
    s.file_menu.open = false;
    s.conn_edit([](Session::ConnInfo& c) {
        c.connecting = true;
        c.err.clear();
        c.fail_at = FailStage::None;
        c.msg = "正在连接…";
        c.sftp_err.clear();
    });

    const std::string host = s.host, pass = s.pass, key = s.key;
    // 用户名留空（比如 VSCode 只记了机器名）就按 ssh 的习惯用当前系统账号
    if (s.user.empty())
        s.user = current_user();
    const std::string user = s.user;
    const int port = s.port > 0 ? s.port : 22;
    const int cols = s.term.cols > 0 ? s.term.cols : 80;
    const int rows = s.term.rows > 0 ? s.term.rows : 24;
    Session* sp = &s;
    s.conn_thread = std::thread([sp, host, port, user, pass, key, cols, rows, &win] {
        std::string err;
        try {
            // 重连也遵循“先停生产者，再释放 owner/session”，且整个过程在后台完成。
            sp->stop_fs_worker();
            if (sp->xfer_thread.joinable())
                sp->xfer_thread.join();
            sp->engine.disconnect();
            sp->engine.set_wake([&win] {
                win.wake();
            });
            {
                std::lock_guard<std::mutex> lock(sp->fs_mtx);
                sp->fs_quit = false;
                sp->fs_has_req = false;
                sp->fs_done = false;
                sp->fs_busy = false;
            }
            sp->fs_thread = std::thread(fs_worker, sp, &win);
            const bool ok = sp->engine.connect(host, port, user, pass, key, &err);
            if (ok) {
                Engine& en = sp->engine;
                // 优先建立 shell，避免 SFTP 不可用时延迟终端会话。
                std::string e3;
                if (!en.open_shell(cols, rows, &e3)) {
                    sp->conn_edit([&](Session::ConnInfo& c) {
                        c.err = e3;
                        c.fail_at = FailStage::Shell;
                        c.msg = "已连上，但 shell 打不开";
                    });
                } else {
                    en.start_reader();
                    sp->conn_edit([&](Session::ConnInfo& c) {
                        c.msg = "已连接  " + en.who();
                    });
                }
                // SFTP 是附加能力，起不来不影响终端
                std::string e2;
                const bool sftp_ok = en.open_sftp(&e2) && en.fetch_home(&e2);
                sp->conn_edit([&](Session::ConnInfo& c) {
                    c.sftp_err = sftp_ok ? std::string() : e2;
                });
            } else {
                const FailStage st = sp->engine.fail_stage();
                sp->conn_edit([&](Session::ConnInfo& c) {
                    c.err = err;
                    c.fail_at = st;
                    c.msg = "连接失败";
                });
            }
        } catch (const std::exception& e) {
            sp->engine.request_stop();
            sp->conn_edit([&](Session::ConnInfo& c) {
                c.err = std::string("SSH 连接任务失败：") + e.what();
                c.msg = "连接失败";
                c.sftp_err = c.err;
            });
        }
        bool sftp_ready = false;
        sp->conn_edit([&](Session::ConnInfo& c) {
            c.connecting = false;
            sftp_ready = c.sftp_err.empty();
        });
        win.wake();
        if (sp->engine.connected() && sftp_ready)
            sp->fs_push(FsReq{FsOp::List, sp->engine.cwd_snapshot(), {}, {}});
    });
}

// =============================================================================
//  六、终端渲染
// =============================================================================
struct TermMetrics {
    float adv = 8.0f, cell_h = 17.0f, ascent = 11.0f;
    int cols = 80, rows = 24;
};

// 制表符：自己画线，不用字形。
// 等宽字体里的 ─ │ 在小字号下只有几像素高，逐行排下来是"虚线"，而 vim / tmux /
// htop / ncurses 的边框全靠它们。这里把常用的一批直接画成矩形线，边框才连续。
bool is_box_glyph(uint32_t cp) {
    return cp >= 0x2500 && cp <= 0x254B;
}

bool draw_box_glyph(aino::Ui& ui, uint32_t cp, float x, float y, float w, float h, const Color& c) {
    bool l = false, r = false, u = false, d = false;
    float t = 1.2f;
    switch (cp) {
    case 0x2500:
        l = r = true;
        break; // ─
    case 0x2502:
        u = d = true;
        break; // │
    case 0x250C:
        r = d = true;
        break; // ┌
    case 0x2510:
        l = d = true;
        break; // ┐
    case 0x2514:
        r = u = true;
        break; // └
    case 0x2518:
        l = u = true;
        break; // ┘
    case 0x251C:
        r = u = d = true;
        break; // ├
    case 0x2524:
        l = u = d = true;
        break; // ┤
    case 0x252C:
        l = r = d = true;
        break; // ┬
    case 0x2534:
        l = r = u = true;
        break; // ┴
    case 0x253C:
        l = r = u = d = true;
        break; // ┼
    case 0x2501:
        l = r = true;
        t = 2.4f;
        break; // ━ 粗
    case 0x2503:
        u = d = true;
        t = 2.4f;
        break; // ┃
    case 0x250F:
        r = d = true;
        t = 2.4f;
        break;
    case 0x2513:
        l = d = true;
        t = 2.4f;
        break;
    case 0x2517:
        r = u = true;
        t = 2.4f;
        break;
    case 0x251B:
        l = u = true;
        t = 2.4f;
        break;
    case 0x2523:
        r = u = d = true;
        t = 2.4f;
        break;
    case 0x252B:
        l = u = d = true;
        t = 2.4f;
        break;
    case 0x2533:
        l = r = d = true;
        t = 2.4f;
        break;
    case 0x253B:
        l = r = u = true;
        t = 2.4f;
        break;
    case 0x254B:
        l = r = u = d = true;
        t = 2.4f;
        break;
    default:
        return false; // 双线等：交给字体
    }
    const float xc = x + w * 0.5f, yc = y + h * 0.5f;
    if (l)
        ui.fill_rect(Rect(x - 0.5f, yc - t * 0.5f, w * 0.5f + 1.0f, t), c);
    if (r)
        ui.fill_rect(Rect(xc, yc - t * 0.5f, w * 0.5f + 0.5f, t), c);
    if (u)
        ui.fill_rect(Rect(xc - t * 0.5f, y - 0.5f, t, h * 0.5f + 1.0f), c);
    if (d)
        ui.fill_rect(Rect(xc - t * 0.5f, yc, t, h * 0.5f + 0.5f), c);
    return true;
}

TermMetrics measure_term(aino::Ui& ui, const Rect& area, float font) {
    TermMetrics m;
    m.adv = ui.mono_advance(font);
    if (m.adv < 1.0f)
        m.adv = font * 0.6f;
    m.ascent = ui.mono_ascent(font);
    m.cell_h = std::ceil(m.ascent + font * 0.30f);
    m.cols = std::max(2, int(std::floor((area.w - 20.0f) / m.adv)));
    m.rows = std::max(2, int(std::floor((area.h - 12.0f) / m.cell_h)));
    return m;
}

const std::vector<Cell>* abs_row(const Terminal& t, int abs_line) {
    if (abs_line < 0)
        return nullptr;
    if (abs_line < t.history_size())
        return &t.history_row(abs_line);
    const int s = abs_line - t.history_size();
    if (s >= 0 && s < t.rows)
        return &t.screen_row(s);
    return nullptr;
}

bool row_has_text(const std::vector<Cell>& r) {
    for (const Cell& c : r)
        if (c.ch != ' ' && c.ch != 0)
            return true;
    return false;
}

// SGR 颜色 → 具体色值（内置 16 色用一套不刺眼的深色终端配色）
Color term_palette(int idx, bool bright) {
    static const uint32_t base[8] = {0x2E3440, 0xBF616A, 0xA3BE8C, 0xEBCB8B,
                                     0x81A1C1, 0xB48EAD, 0x88C0D0, 0xD8DEE9};
    if (idx < 0 || idx > 7)
        idx = 7;
    uint32_t v = base[idx];
    if (bright) {
        // 亮色 = 往白里提一档，比另存 8 个常量省事
        const int r = int((v >> 16) & 0xFF), g = int((v >> 8) & 0xFF), b = int(v & 0xFF);
        auto up = [](int c) {
            return std::min(255, c + (255 - c) * 35 / 100);
        };
        v = (uint32_t(up(r)) << 16) | (uint32_t(up(g)) << 8) | uint32_t(up(b));
    }
    return Color::hex(v);
}

// xterm 256 色：0–15 为系统色，16–231 为 6×6×6 色立方，232–255 为灰阶。
Color xterm256(int idx) {
    if (idx < 16)
        return term_palette(idx & 7, (idx & 8) != 0);
    if (idx >= 232) {
        const int v = 8 + (idx - 232) * 10;
        return Color(uint8_t(v), uint8_t(v), uint8_t(v));
    }
    const int i = idx - 16;
    static const int lv[6] = {0, 95, 135, 175, 215, 255};
    const int r = lv[(i / 36) % 6], g = lv[(i / 6) % 6], b = lv[i % 6];
    return Color(uint8_t(r), uint8_t(g), uint8_t(b));
}

// 粗体的"黑"在真实终端里是灰的（不然黑底上必然看不见），所以 bold 要参与取色
Color cell_fg(const Cell& c) {
    if (c.fg == sshc::kDefaultFg)
        return kTermFg;
    if (c.fg & sshc::kRgbFlag)
        return Color(uint8_t((c.fg >> 16) & 0xFF), uint8_t((c.fg >> 8) & 0xFF), uint8_t(c.fg & 0xFF));
    if (c.fg > 255)
        return kTermFg;
    if (c.fg < 8)
        return term_palette(int(c.fg), (c.attr & sshc::A_BOLD) != 0);
    return xterm256(int(c.fg));
}
Color cell_bg(const Cell& c) {
    if (c.bg == sshc::kDefaultBg)
        return kTermBg;
    if (c.bg & sshc::kRgbFlag)
        return Color(uint8_t((c.bg >> 16) & 0xFF), uint8_t((c.bg >> 8) & 0xFF), uint8_t(c.bg & 0xFF));
    if (c.bg > 255)
        return kTermBg;
    return xterm256(int(c.bg));
}

// 按亮度差调整与终端背景过于接近的前景色，保留其余颜色。
Color fix_contrast(Color fg, Color bg) {
    auto lum = [](const Color& c) {
        return int(c.r) * 299 + int(c.g) * 587 + int(c.b) * 114;
    };
    const int lf = lum(fg), lb = lum(bg);
    const int d = lf > lb ? lf - lb : lb - lf;
    const int kMin = 45000; // ≈ 45/255 的亮度差，低于这个就开始兜底
    if (d >= kMin)
        return fg;
    const bool dark_bg = lb < 128000;
    const Color target = dark_bg ? Color(0xD6, 0xD8, 0xE0) : Color(0x18, 0x19, 0x1E);
    const int t = (kMin - d) * 100 / kMin; // 0..100，越接近越往目标色靠
    auto mix = [&](int f, int g) {
        return f + (g - f) * t / 100;
    };
    return Color(uint8_t(mix(fg.r, target.r)), uint8_t(mix(fg.g, target.g)), uint8_t(mix(fg.b, target.b)));
}

// 真正拿去画字的前景色：反显 / 暗色（SGR 2）/ 撞色兜底都在这里一次性处理，
// 画背景、画字、画制表符都走同一个函数，否则游程合并会按不同色值断开。
Color cell_fg_eff(const Cell& c) {
    const bool rev = (c.attr & sshc::A_REV) != 0;
    Color fg = rev ? cell_bg(c) : cell_fg(c);
    const Color bg = rev ? cell_fg(c) : cell_bg(c);
    if (c.attr & sshc::A_DIM) {
        // 变暗 = 往背景色拉 45%，而不是简单乘 0.5（乘 0.5 在深色底上等于看不见）
        auto mix = [](int f, int b) {
            return int(f + (b - f) * 45 / 100);
        };
        fg = Color(uint8_t(mix(fg.r, bg.r)), uint8_t(mix(fg.g, bg.g)), uint8_t(mix(fg.b, bg.b)));
    }
    return fix_contrast(fg, bg);
}

void draw_terminal(aino::Ui& ui, Session& a, const Rect& area, const TermMetrics& m, bool focused) {
    const int hist = a.term.history_size();
    int first = hist - a.view_offset;
    const int total = a.term.total_lines();
    first = std::max(0, std::min(first, std::max(0, total - a.term.rows)));

    const bool has_sel = a.term.has_selection();
    int sl0 = 0, sc0 = 0, sl1 = 0, sc1 = 0;
    if (has_sel)
        a.term.sel_range(&sl0, &sc0, &sl1, &sc1);
    const bool sel_nonempty = has_sel && !(sl0 == sl1 && sc0 == sc1);

    ui.push_clip(area);
    for (int r = 0; r < a.term.rows; ++r) {
        const int line = first + r;
        const std::vector<Cell>* row = abs_row(a.term, line);
        if (!row)
            continue;
        const float y = area.y + float(r) * m.cell_h;
        const int n = int(row->size());

        // 背景：同色游程合并（122x42 从五千次 fill 压到几十次）
        int x = 0;
        while (x < n) {
            const Cell& c = (*row)[size_t(x)];
            const Color bg = (c.attr & sshc::A_REV) ? cell_fg(c) : cell_bg(c);
            int run = 1;
            while (x + run < n) {
                const Cell& d = (*row)[size_t(x + run)];
                const Color b2 = (d.attr & sshc::A_REV) ? cell_fg(d) : cell_bg(d);
                if (!(b2.r == bg.r && b2.g == bg.g && b2.b == bg.b))
                    break;
                ++run;
            }
            if (!(bg.r == kTermBg.r && bg.g == kTermBg.g && bg.b == kTermBg.b))
                ui.fill_rect(Rect(area.x + 10.0f + float(x) * m.adv, y, float(run) * m.adv, m.cell_h), bg);
            x += run;
        }

        // 选区：只有真有内容的行才亮（否则点一下终端就出现两条灰带）
        if (sel_nonempty && line >= sl0 && line <= sl1 && row_has_text(*row)) {
            const int a0 = (line == sl0) ? sc0 : 0;
            const int a1 = (line == sl1) ? sc1 : n - 1;
            if (a1 >= a0)
                ui.fill_rect(
                    Rect(area.x + 10.0f + float(a0) * m.adv, y, float(a1 - a0 + 1) * m.adv, m.cell_h),
                    Color(110, 168, 254, 70));
        }

        // 前景
        x = 0;
        while (x < n) {
            const Cell& c = (*row)[size_t(x)];
            if (c.ch == 0) {
                ++x;
                continue;
            }
            if (c.attr & sshc::A_HIDE) {
                ++x;
                continue;
            }
            const Color fg = cell_fg_eff(c);
            int run = 1;
            std::string text;
            sshc::append_cell_text(text, c);
            bool wide = sshc::wc_width(c.ch) > 1;
            bool boxed = is_box_glyph(c.ch);
            while (x + run < n) {
                const Cell& d = (*row)[size_t(x + run)];
                if (d.ch == 0 || (d.attr & sshc::A_HIDE))
                    break;
                const Color f2 = cell_fg_eff(d);
                if (!(f2.r == fg.r && f2.g == fg.g && f2.b == fg.b))
                    break;
                if ((d.attr & (sshc::A_UNDER | sshc::A_REV)) != (c.attr & (sshc::A_UNDER | sshc::A_REV)))
                    break;
                if (sshc::wc_width(d.ch) > 1)
                    wide = true;
                if (is_box_glyph(d.ch))
                    boxed = true;
                sshc::append_cell_text(text, d);
                ++run;
            }
            if (wide || boxed) {
                // 全角与制表符都逐格画：全角步进未必正好 2 倍；制表符要画线才连得上
                for (int k = 0; k < run; ++k) {
                    const Cell& d = (*row)[size_t(x + k)];
                    if (d.ch == 0 || (d.attr & sshc::A_HIDE))
                        continue;
                    const Color fc = cell_fg_eff(d);
                    const float cx = area.x + 10.0f + float(x + k) * m.adv;
                    if (!draw_box_glyph(ui, d.ch, cx, y, m.adv, m.cell_h, fc)) {
                        std::string one;
                        append_utf8(one, d.ch);
                        ui.draw_mono_text(one, cx, y, fc, a.font);
                    }
                    if (d.attr & sshc::A_UNDER)
                        ui.fill_rect(Rect(cx, y + m.cell_h - 1.5f,
                                          (sshc::wc_width(d.ch) > 1 ? 2.0f : 1.0f) * m.adv, 1.0f),
                                     fc);
                }
            } else {
                const float cx = area.x + 10.0f + float(x) * m.adv;
                ui.draw_mono_text(text, cx, y, fg, a.font);
                if (c.attr & sshc::A_UNDER)
                    ui.fill_rect(Rect(cx, y + m.cell_h - 1.5f, float(run) * m.adv, 1.0f), fg);
            }
            x += run;
        }
    }

    // 光标：实心块 + 反色字符（中文也看得清）
    if (focused && a.term.cursor_visible() && !a.term.alt_screen()) {
        const int cline = hist + a.term.cursor_row();
        if (cline >= first && cline < first + a.term.rows) {
            const int r = cline - first;
            const float cx = area.x + 10.0f + float(a.term.cursor_col()) * m.adv;
            const float cy = area.y + float(r) * m.cell_h;
            const std::vector<Cell>* row = abs_row(a.term, cline);
            uint32_t ch = ' ';
            if (row && a.term.cursor_col() < int(row->size()))
                ch = (*row)[size_t(a.term.cursor_col())].ch;
            ui.fill_rect(Rect(cx, cy, m.adv, m.cell_h), Color(214, 216, 224, 210));
            if (ch != ' ' && ch != 0) {
                std::string one;
                append_utf8(one, ch);
                ui.draw_mono_text(one, cx, cy, kTermBg, a.font);
            }
        }
    }
    ui.pop_clip();
}

// =============================================================================
//  七、终端输入
// =============================================================================
void send_key(Session& session, const std::string& bytes) {
    if (!session.engine.shell_open() || !session.engine.send(bytes))
        return;
    session.follow = true;
    session.view_offset = 0;
}

void copy_terminal(Session& session) {
    const auto text = session.term.selected_text();
    if (text.empty() || !session.clip)
        return;
    const bool copied = session.clip->set(text);
    set_notice(session, copied ? "已复制终端选区" : "系统剪贴板暂不可用", !copied);
}

void paste_terminal(Session& session) {
    if (!session.engine.shell_open() || !session.clip)
        return;
    const auto weak = session.weak_from_this();
    session.clip->request([weak](std::string text) {
    const auto keep = weak.lock();
    if (!keep || keep->shutdown_done || keep->retired || !keep->engine.shell_open()) return;
    auto& session = *keep;
    std::string payload;
    payload.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte == '\r' || byte == '\n') {
            if (byte == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            payload.push_back('\r');
        } else if (byte == '\t' || (byte >= 32 && byte != 127)) {
            payload.push_back(text[i]);
        }
    }
    if (payload.empty()) {
        set_notice(session, "剪贴板没有可粘贴的文字", true);
        return;
    }
    if (session.term.bracketed_paste()) {
        send_key(session, "\x1b[200~" + payload + "\x1b[201~");
    } else if (payload.find('\r') != std::string::npos) {
        session.prompt_text = std::move(payload);
        session.prompt = Prompt::Paste;
    } else {
        send_key(session, payload);
    }
    });
}

bool terminal_shortcut(const aino::Input& input, int key) {
    if (!input.key_pressed[key])
        return false;
#if defined(AINO_PLATFORM_MACOS)
    if (input.super())
        return true;
#endif
    return input.ctrl_of(key) && input.shift_of(key);
}

void handle_term_keys(aino::Ui& ui, Session& a) {
    const auto& in = ui.input();
    if (terminal_shortcut(in, 'C')) {
        copy_terminal(a);
        return;
    }
    if (terminal_shortcut(in, 'V')) {
        paste_terminal(a);
        return;
    }
    if (terminal_shortcut(in, 'A')) {
        a.term.select_all();
        return;
    }
    if (terminal_shortcut(in, 'L')) {
        a.term.reset();
        a.view_offset = 0;
        a.follow = true;
        return;
    }

    if (!in.text_input.empty()) {
        std::string text;
        if (in.alt() && !in.ctrl())
            text.push_back('\x1b');
        text += in.text_input;
        send_key(a, text);
    }

    // 下面这些键按"这一帧应该生效几次"发：长按退格/方向键时，一帧里可能挤进
    // 好几次系统自动重复（帧率掉下来时尤其明显）。只发一次会少走几格，
    // 隔着屏幕看就像"偶尔卡一下"。
    if (in.key_times(aino::KEY_ENTER) > 0) {
        send_key(a, "\r");
        return;
    }
    if (in.key_times(aino::KEY_TAB) > 0) {
        send_key(a, in.shift() ? "\x1b[Z" : "\t");
        return;
    }
    if (const int n = in.key_times(aino::KEY_BACKSPACE); n > 0) {
        send_key(a, std::string(size_t(n), '\x7f'));
        return;
    }
    if (in.key_times(aino::KEY_ESC) > 0) {
        send_key(a, "\x1b");
        return;
    }

    struct KeySeq {
        int key;
        const char* normal;
        const char* app;
    };
    static const KeySeq seqs[] = {
        {aino::KEY_UP, "\x1b[A", "\x1bOA"},       {aino::KEY_DOWN, "\x1b[B", "\x1bOB"},
        {aino::KEY_RIGHT, "\x1b[C", "\x1bOC"},    {aino::KEY_LEFT, "\x1b[D", "\x1bOD"},
        {aino::KEY_HOME, "\x1b[H", "\x1bOH"},     {aino::KEY_END, "\x1b[F", "\x1bOF"},
        {aino::KEY_DELETE, "\x1b[3~", "\x1b[3~"},
    };
    for (const KeySeq& s : seqs) {
        const int times = in.key_times(s.key);
        if (times <= 0)
            continue;
        const char* one = a.term.application_cursor() ? s.app : s.normal;
        std::string batch;
        for (int i = 0; i < times; ++i)
            batch += one;
        send_key(a, batch);
        return;
    }
    // Ctrl+A..Z → 控制字符（Ctrl+C 上面已经特判）。这里只认 key_pressed：
    // 这些组合基本不会按住不放，允许重复反而容易误触发。
    if (in.ctrl()) {
        for (int k = 'A'; k <= 'Z'; ++k) {
            if (in.key_pressed[k]) {
                send_key(a, std::string(1, char(k - 'A' + 1)));
                return;
            }
        }
    }
    if (in.key_pressed[aino::KEY_PAGEUP]) {
        // PgUp/PgDn 不是发给远端，是看回滚缓冲
        const int maxoff = a.term.history_size();
        a.view_offset = std::min(maxoff, a.view_offset + 10);
        a.follow = false;
        return;
    }
    if (in.key_pressed[aino::KEY_PAGEDOWN]) {
        a.view_offset = std::max(0, a.view_offset - 10);
        a.follow = a.view_offset == 0;
        return;
    }
}

// =============================================================================
//  八、示例控件组合（按钮和密码编辑复用库控件）
// =============================================================================
// 有弹窗时，底下的页面只画不接受输入。
bool g_block_input = false;

bool tool_button(aino::Ui& ui, const std::string& label, const Rect& r, bool danger) {
    Style style;
    if (danger)
        style.destructive();
    else
        style.outline();
    if (g_block_input)
        style.off();
    return ui.button(label, r, style);
}

enum class Btn { Normal, Primary, Danger };

bool dlg_button(aino::Ui& ui, const std::string& label, const Rect& r, Btn k) {
    Style style;
    if (k == Btn::Normal)
        style.outline();
    else if (k == Btn::Danger)
        style.destructive();
    if (g_block_input)
        style.off();
    return ui.button(label, r, style);
}

// 密码内容交给 input() 编辑，保留 UTF-8、选区、粘贴与撤销行为。
void password_field(aino::Ui& ui, const aino::Theme& th, std::string* value, bool* reveal, const Rect& r) {
    Style style = Style().secret(!*reveal).hint("输入密码");
    style.disabled = g_block_input;
    ui.input(value, Rect(r.x, r.y, r.w - 32.0f, r.h), style);
    const Rect eye = Rect::centered(r.right() - 14.0f, r.cy(), 24.0f, 24.0f);
    style = Style().ghost(); style.disabled = g_block_input;
    if (ui.button("", eye, style))
        *reveal = !*reveal;
    ui.system_icon(aino::SystemIcon::Eye, eye.inset(5.0f), *reveal ? th.tint : th.label2);
    ui.tooltip(eye, *reveal ? "隐藏密码" : "显示密码");
}

// 普通文本字段（标题 + 输入框）
bool field(aino::Ui& ui, const aino::Theme& th, const char* label, std::string* v, const Rect& r) {
    ui.draw_text(label, Rect(r.x, r.y - 16.0f, r.w, 14.0f), th.label2, Style().font(11.0f));
    Style style; style.disabled = g_block_input;
    return ui.input(v, r, style);
}

// =============================================================================
//  九、SFTP 面板
// =============================================================================
std::string join_remote(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == "/")
        return "/" + name;
    if (dir.back() == '/')
        return dir + name;
    return dir + "/" + name;
}

std::string human_size(long long n) {
    char b[32];
    if (n < 1024)
        std::snprintf(b, sizeof b, "%lld B", n);
    else if (n < 1024LL * 1024)
        std::snprintf(b, sizeof b, "%.1f K", double(n) / 1024.0);
    else if (n < 1024LL * 1024 * 1024)
        std::snprintf(b, sizeof b, "%.1f M", double(n) / (1024.0 * 1024.0));
    else
        std::snprintf(b, sizeof b, "%.2f G", double(n) / (1024.0 * 1024.0 * 1024.0));
    return b;
}

std::string human_time(int64_t t) {
    if (t <= 0)
        return "-";
    const std::time_t tt = std::time_t(t);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    char b[32];
    std::snprintf(b, sizeof b, "%02d-%02d %02d:%02d", tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    return b;
}

void enter_dir(Session& session, const std::string& path) {
    if (!session.engine.sftp_ready() || session.conn_busy())
        return;
    if (path.empty()) {
        session.path_edit = session.sftp_path;
        return;
    }
    if (!session.fs_push({FsOp::List, path, {}, {}}))
        set_notice(session, "正在处理远程文件，请稍候", true);
}

void refresh_list(Session& session) {
    if (!session.sftp_path.empty() && !file_work_pending(session))
        enter_dir(session, session.sftp_path);
}

void go_up(Session& session) {
    const auto& path = session.sftp_path;
    const auto pos = path.find_last_of('/');
    if (!path.empty() && path != "/")
        enter_dir(session, pos == std::string::npos || pos == 0 ? "/" : path.substr(0, pos));
}

struct MenuItem {
    const char* text;
    bool enabled = true;
    bool destructive = false;
};

void open_menu(MenuState& menu, aino::Ui& ui) {
    menu.open = true;
    menu.anchor = ui.input().mouse;
    menu.pressed = -1;
    ui.request_animation_frame();
}

int draw_action_menu(aino::Ui& ui, MenuState& menu, std::vector<MenuItem> items) {
    if (!menu.open)
        return -1;
    static constexpr float width = 208, row_h = 28, pad = 5;
    const float height = float(items.size()) * row_h + pad * 2;
    const Rect box(std::clamp(menu.anchor.x, 4.f, std::max(4.f, ui.width() - width - 4)),
                   std::clamp(menu.anchor.y, 4.f, std::max(4.f, ui.height() - height - 4)), width, height);
    const auto& input = ui.raw_input();
    if (input.key_pressed[aino::KEY_ESC] ||
        ((input.pressed[0] || input.pressed[1]) && !box.contains(input.mouse))) {
        menu.open = false;
        ui.request_animation_frame();
        return -1;
    }

    int hover = -1;
    for (size_t i = 0; i < items.size(); ++i) {
        const Rect row(box.x + pad, box.y + pad + float(i) * row_h, width - pad * 2, row_h);
        if (items[i].enabled && row.contains(input.mouse))
            hover = int(i);
    }
    if (input.pressed[0]) {
        menu.pressed = hover;
        ui.impl->press_taken = true;
    }
    if (input.released[0]) {
        const int picked = menu.pressed == hover ? hover : -1;
        menu.pressed = -1;
        if (picked >= 0) {
            menu.open = false;
            ui.request_animation_frame();
            return picked;
        }
    }
    ui.impl->gate_claim(box, aino::Ui::Impl::Z_FRAME_END);
    ui.deferred([&ui, items = std::move(items), box, hover] {
        const auto& theme = ui.theme();
        ui.fill_rect(box, theme.popover_bg, 9);
        ui.stroke_rect(box, theme.separator, 9, 1);
        for (size_t i = 0; i < items.size(); ++i) {
            const Rect row(box.x + pad, box.y + pad + float(i) * row_h, width - pad * 2, row_h);
            if (hover == int(i))
                ui.fill_rect(row, theme.tint.scaled_alpha(.16f), 5);
            const auto& item = items[i];
            const Color color = !item.enabled ? theme.label3 : item.destructive ? kBad : theme.label;
            ui.draw_text(item.text, row.inset(8, 0), color, Style().font(12));
        }
    });
    return -1;
}

// 文件面板。download_dir 由 App 持有（跨会话共用），所以传指针。
// ---- 连接失败：说清是"网络没通"还是"密码不对" ----
// 用户看到"连不上"时的第一反应是"是不是密码错了？"。所以这里必须点明
// 失败发生在哪一层（网络 / 协议 / 认证），再给一句能照着做的建议，
// 而不只是甩一行系统错误。
struct FailExplain {
    const char* title;
    Color color;
    const char* advice;
};

FailExplain explain_fail(FailStage st) {
    switch (st) {
    case FailStage::Input:
        return {"连接信息有误", kWarn,
                "检查主机、端口和用户名；用户名中不能包含 Tab 等控制字符。尚未向服务器提交密码。"};
    case FailStage::Dns:
        return {"网络没通 · 域名解析失败", kBad,
                "这台机器的名字在本机解析不出来。检查拼写，或确认 VPN / hosts 配置。"};
    case FailStage::Tcp:
        return {"网络没通 · 连不上端口", kBad,
                "请求根本没到服务器，与密码无关。先确认机器在线、端口正确、防火墙放行。"};
    case FailStage::Handshake:
        return {"端口已连通 · SSH 握手失败", kWarn, "查看底层错误，检查超时、代理及双方的 SSH 协议兼容性。"};
    case FailStage::KeyExchange:
        return {"SSH 密钥交换失败", kWarn,
                "检查客户端 SSH 依赖与服务器算法配置。使用 WinCNG 时确认已启用 ECDH 支持。"};
    case FailStage::HostKey:
        return {"主机密钥变了", kWarn,
                "这台机器的密钥与上次记录不一致：可能重装过系统，也可能是中间人。"
                "确认无误后删掉 known_hosts 里那条再连。"};
    case FailStage::Auth:
        return {"网络通了 · 账号认证失败", kBad, "先核对用户名，再检查密码、账号登录权限或认证方式。"};
    case FailStage::Shell:
        return {"登录成功 · 但开不了终端", kWarn,
                "账号能登录，但这台机器禁止交互 shell 或不允许分配 PTY"
                "（不少只允许 SFTP 的账号是这样）。"};
    default:
        return {"连接失败", kBad, ""};
    }
}

// 按实际宽度折行；长主机名和文件名也按 UTF-8 字符拆分。
std::vector<std::string> wrap_text(aino::Ui& ui, const std::string& text, float font, float width,
                                   int weight = 400) {
    std::vector<std::string> lines;
    std::string line;
    for (size_t i = 0; i < text.size();) {
        const unsigned char head = static_cast<unsigned char>(text[i]);
        const size_t length = std::min(text.size() - i, size_t(head >= 0xf0   ? 4
                                                               : head >= 0xe0 ? 3
                                                               : head >= 0xc0 ? 2
                                                                              : 1));
        const std::string character = text.substr(i, length);
        i += length;
        if (character == "\r")
            continue;
        if (character == "\n") {
            lines.push_back(line);
            line.clear();
            continue;
        }
        if (!line.empty() && ui.text_width(line + character, font, weight) > width) {
            lines.push_back(line);
            line.clear();
        }
        line += character;
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

bool remote_leaf(const std::string& name) {
    return !name.empty() && name != "." && name != ".." &&
           name.find('/') == std::string::npos && name.find('\0') == std::string::npos;
}

void queue_uploads(Session& session, const std::vector<std::string>& files, const std::string& directory) {
    constexpr size_t queue_limit = 128;
    size_t added = 0;
    for (const auto& file : files) {
        if (session.uploads.size() >= queue_limit)
            break;
        const auto path = std::filesystem::u8path(file);
        const auto name = path.filename().u8string();
        if (!path.is_absolute() || !remote_leaf(name))
            continue;
        session.uploads.push_back({file, join_remote(directory, name)});
        ++added;
    }
    const size_t skipped = files.size() - added;
    std::string notice = added ? "已加入上传队列 " + std::to_string(added) + " 个文件" : "没有文件加入上传";
    if (skipped)
        notice += "，跳过 " + std::to_string(skipped) + " 项（路径无效或超出队列上限）";
    set_notice(session, notice, skipped != 0);
}

void download_entry(Session& session, const Entry& entry, const std::string& directory, aino::Window* window) {
    const auto name = std::filesystem::u8path(entry.name);
    if (!remote_leaf(entry.name) || name.has_root_path() || name.has_parent_path()
#if defined(_WIN32)
        || entry.name.find_first_of("\\:") != std::string::npos
#endif
    ) {
        set_notice(session, "远程文件名不能直接用于本地保存", true);
        return;
    }
    if (directory.empty()) {
        set_notice(session, "请先设置本地保存目录", true);
        return;
    }
    const auto target = std::filesystem::u8path(directory) / name;
    start_transfer(session, false, target.u8string(), join_remote(session.sftp_path, entry.name), window, false);
}

struct SftpRow {
    int index;
    std::vector<std::string> lines;
    float height;
};

std::vector<SftpRow> sftp_rows(aino::Ui& ui, const Session& session, float width) {
    std::vector<SftpRow> rows;
    if (!session.sftp_path.empty() && session.sftp_path != "/" && session.file_filter.empty())
        rows.push_back({0, {"上级目录"}, 38});
    auto fold = [](std::string text) {
        for (char& c : text) {
            if (c >= 'A' && c <= 'Z')
                c = char(c + 'a' - 'A');
        }
        return text;
    };
    const auto query = fold(session.file_filter);
    for (size_t i = 0; i < session.entries.size(); ++i) {
        const auto& entry = session.entries[i];
        if (!query.empty() && fold(entry.name).find(query) == std::string::npos)
            continue;
        auto lines = wrap_text(ui, entry.name, 12, std::max(40.f, width - 48));
        const float height = 28 + float(lines.size()) * 18;
        rows.push_back({int(i + 1), std::move(lines), height});
    }
    return rows;
}

void draw_sftp(aino::Ui& ui, Session& session, const aino::Theme& theme, const Rect& area,
               std::string* download_dir, aino::Window* window, bool native_file_drop) {
    const auto connection = session.conn_snapshot();
    const auto& input = ui.input();
    const bool pending = file_work_pending(session);
    const bool ready = session.engine.sftp_ready() && session.engine.connected() && !connection.connecting;
    const bool can_browse = ready && !pending;
    const bool can_transfer = can_browse && !session.sftp_path.empty();
    const bool busy = session.xfer_running || !session.uploads.empty();
    const float x = area.x + 12;
    const float width = area.w - 24;
    const Rect path(x, area.y + 42, width, 30);
    const Rect filter(x, path.bottom() + 8, width, 28);
    const Rect footer(x, area.bottom() - 22 - 116, width, 116);
    const Rect list(x, filter.bottom() + 29, width, std::max(0.f, footer.y - filter.bottom() - 41));
    session.r_list = list;

    if (g_block_input) {
        session.file_menu.open = false;
    } else if (input.pressed[0] && !session.file_menu.open && !ui.impl->gate_blocked_pointer()) {
        session.path_focus = path.contains(input.mouse);
        session.file_focus = list.contains(input.mouse);
    }

    auto action = [&](const char* title, const Rect& box, bool enabled, bool primary = false) {
        Style style = primary ? Style().primary() : Style().outline();
        style.disabled = g_block_input || session.file_menu.open || !enabled;
        return ui.button(title, box, style);
    };

    ui.label("远程文件", {x + 4, area.y + 4, width - 116, 28}, Style().font(13).semi_bold());
    if (action("上级", {area.right() - 118, area.y + 4, 50, 28},
               can_browse && !session.sftp_path.empty() && session.sftp_path != "/"))
        go_up(session);
    if (action("刷新", {area.right() - 62, area.y + 4, 50, 28}, can_transfer))
        refresh_list(session);

    if (!session.path_focus && !pending)
        session.path_edit = session.sftp_path;
    Style path_style = Style().hint("输入远程路径后按回车");
    path_style.disabled = g_block_input || session.file_menu.open || !ready;
    ui.input(&session.path_edit, path, path_style);
    if (!g_block_input && !session.file_menu.open && session.path_focus && input.key_pressed[aino::KEY_ENTER] && can_browse) {
        enter_dir(session, session.path_edit);
        session.path_focus = false;
        aino::detail::drop_focus(ui.impl);
    }

    Style filter_style = Style().hint("在当前目录中查找");
    filter_style.disabled = g_block_input || session.file_menu.open;
    if (ui.search_field(&session.file_filter, filter, filter_style))
        session.reset_file_scroll = true;
    const auto rows = sftp_rows(ui, session, width - 10);
    std::string summary = pending ? "正在读取远程目录…" :
        std::to_string(rows.size() - (!rows.empty() && rows.front().index == 0 ? 1 : 0)) + " 项";
    if (can_transfer && native_file_drop)
        summary += " · 可拖入上传";
    ui.draw_text(summary, {x + 4, filter.bottom() + 5, width - 8, 19}, theme.label2, Style().font(11));

    const std::string note = !connection.sftp_err.empty() ? connection.sftp_err :
        pending && session.entries.empty() ? "正在加载…" :
        rows.empty() ? (session.entries.empty() ? (ready ? "此目录为空" : "连接后可浏览远程文件") : "没有匹配的文件") : "";
    const auto note_lines = wrap_text(ui, note, 11, width - 20);
    float height = note_lines.empty() ? 0.f : 16.f + float(note_lines.size()) * 17;
    for (const auto& row : rows)
        height += row.height;
    ui.panel(list, Style().background(theme.is_dark() ? theme.card_bg : theme.control_bg).corner(12));
    if (session.reset_file_scroll) {
        ui.scroll_to_index(list, 0, 1, int(height + 8));
        session.reset_file_scroll = false;
    }

    int activated = -1;
    if (input.pressed[1] && list.contains(input.mouse) && !g_block_input && !ui.impl->gate_blocked_pointer()) {
        session.sel = -1;
        session.term_menu.open = false;
        session.term_focus = false;
        session.file_focus = true;
        session.path_focus = false;
        aino::detail::drop_focus(ui.impl);
        open_menu(session.file_menu, ui);
    }
    {
        auto scroll = ui.scroll(list, height + 8);
        float y = list.y + 4 - scroll.offset;
        for (const auto& line : note_lines) {
            ui.draw_text(line, {x + 10, y + 8, width - 20, 17}, theme.label2, Style().font(11));
            y += 17;
        }
        if (!note_lines.empty())
            y += 16;
        for (const auto& layout : rows) {
            const Rect row(x + 4, y, width - 14, layout.height);
            y += layout.height;
            if (row.bottom() <= list.y || row.y >= list.bottom())
                continue;
            const bool up = layout.index == 0;
            const Entry* entry = up ? nullptr : &session.entries[size_t(layout.index - 1)];
            const bool directory = up || entry->dir;
            const bool over = list.contains(input.mouse) && row.contains(input.mouse) &&
                              !g_block_input && !ui.impl->gate_blocked_pointer();
            if (over && input.pressed[1] && session.file_menu.open)
                session.sel = layout.index;
            if (over && input.pressed[0] && !session.file_menu.open) {
                session.sel = layout.index;
                if (input.double_click)
                    activated = layout.index;
                ui.request_animation_frame();
            }
            if (session.sel == layout.index)
                ui.fill_rect(row, theme.tint.scaled_alpha(.17f), 6);
            else if (over && !session.file_menu.open)
                ui.fill_rect(row, theme.label.scaled_alpha(.06f), 6);
            ui.impl->hover_region(row);
            ui.system_icon(directory ? aino::SystemIcon::Folder : aino::SystemIcon::Text,
                           {row.x + 8, row.y + 10, 16, 16}, directory ? theme.tint : theme.label2);
            float text_y = row.y + 8;
            for (const auto& line : layout.lines) {
                ui.draw_text(line, {row.x + 34, text_y, row.w - 44, 18}, theme.label, Style().font(12));
                text_y += 18;
            }
            if (entry) {
                const auto kind = entry->dir ? std::string("文件夹") : human_size(entry->size);
                ui.draw_text(kind + " · " + human_time(entry->mtime),
                             {row.x + 34, text_y + 2, row.w - 44, 16}, theme.label2, Style().font(10.5f));
            }
        }
    }

    const Entry* selected = session.sel >= 1 && session.sel <= int(session.entries.size())
                                ? &session.entries[size_t(session.sel - 1)] : nullptr;
    const bool visible_selection = std::any_of(rows.begin(), rows.end(), [&](const SftpRow& row) {
        return row.index == session.sel;
    });
    if (!visible_selection)
        selected = nullptr;
    const bool list_keys = session.file_focus && !session.file_menu.open && !g_block_input;
    if (list_keys && (input.key_pressed[aino::KEY_UP] || input.key_pressed[aino::KEY_DOWN]) && !rows.empty()) {
        const auto current = std::find_if(rows.begin(), rows.end(), [&](const SftpRow& row) {
            return row.index == session.sel;
        });
        const int direction = input.key_pressed[aino::KEY_UP] ? -1 : 1;
        const size_t at = current == rows.end() ? 0 :
            size_t(std::clamp(int(current - rows.begin()) + direction, 0, int(rows.size()) - 1));
        session.sel = rows[at].index;
        float row_top = note_lines.empty() ? 0.f : 16.f + float(note_lines.size()) * 17;
        for (size_t i = 0; i < at; ++i)
            row_top += rows[i].height;
        ui.scroll_to_index(list, int(row_top), 1, int(height + 8));
        ui.request_animation_frame();
    }
    if (list_keys && input.key_pressed[aino::KEY_ENTER] && visible_selection)
        activated = session.sel;

    enum Action { Open, Download, Upload, Mkdir, Rename, Remove, CopyPath, Refresh };
    int command = -1;
    if (session.file_menu.open) {
        command = draw_action_menu(ui, session.file_menu,
            {{"打开文件夹", can_transfer && (session.sel == 0 || (selected && selected->dir))},
             {"下载文件", can_transfer && selected && !selected->dir && !busy},
             {"上传文件…", can_transfer}, {"新建文件夹…", can_transfer},
             {"重命名…", can_transfer && selected}, {"删除…", can_transfer && selected, true},
             {"复制远程路径", selected || !session.sftp_path.empty()}, {"刷新", can_transfer}});
    }
    if (list_keys) {
        if (input.key_pressed[aino::KEY_F2] && selected)
            command = Rename;
        if (input.key_pressed[aino::KEY_DELETE] && selected)
            command = Remove;
        if (input.key_pressed[aino::KEY_F5])
            command = Refresh;
        if (input.key_pressed[aino::KEY_BACKSPACE] && can_browse)
            go_up(session);
    }
    if (activated >= 0) {
        command = selected && !selected->dir ? Download : Open;
    }

    ui.panel(footer, Style().background(theme.is_dark() ? theme.card_bg : theme.control_bg).corner(12));
    const float inner_x = footer.x + 12;
    const float inner_w = footer.w - 24;
    const float two = (inner_w - 6) * .5f;
    const float three = (inner_w - 12) / 3;
    if (action("上传…", {inner_x, footer.y + 10, two, 26}, can_transfer, true))
        command = Upload;
    if (action("下载", {inner_x + two + 6, footer.y + 10, two, 26},
               can_transfer && selected && !selected->dir && !busy))
        command = Download;
    if (action("新建", {inner_x, footer.y + 40, three, 25}, can_transfer))
        command = Mkdir;
    if (action("改名", {inner_x + three + 6, footer.y + 40, three, 25}, can_transfer && selected))
        command = Rename;
    if (action("删除", {inner_x + (three + 6) * 2, footer.y + 40, three, 25}, can_transfer && selected))
        command = Remove;

    const Rect local_path(inner_x, footer.y + 74, inner_w - 53, 26);
    Style local_style = Style().hint("本地保存目录");
    local_style.disabled = g_block_input || session.file_menu.open;
    ui.input(download_dir, local_path, local_style);
    ui.tooltip(local_path, "下载文件保存到此目录");
    if (action("浏览", {footer.right() - 59, footer.y + 74, 47, 26}, true)) {
        const std::string picked = pick_folder(*download_dir);
        if (!picked.empty()) {
            std::string error;
            if (save_download_dir(picked, &error))
                *download_dir = picked;
            else
                set_notice(session, error, true);
        }
    }

    if (session.file_drag_over && can_transfer) {
        ui.fill_rect(list, theme.tint.scaled_alpha(.13f), 12);
        ui.stroke_rect(list, theme.tint, 12, 2);
        ui.draw_text("松开以上传到当前目录", list.inset(8), theme.label, Style().font(12).center());
    }
    if (command == CopyPath && session.clip) {
        const auto target = selected ? join_remote(session.sftp_path, selected->name) : session.sftp_path;
        const bool copied = session.clip->set(target);
        set_notice(session, copied ? "已复制远程路径" : "系统剪贴板暂不可用", !copied);
    } else if (command == Refresh && can_browse) {
        refresh_list(session);
    } else if (can_transfer) {
        switch (command) {
        case Open:
            if (session.sel == 0)
                go_up(session);
            else if (selected && selected->dir)
                enter_dir(session, join_remote(session.sftp_path, selected->name));
            break;
        case Download:
            if (selected && !selected->dir && !busy)
                download_entry(session, *selected, *download_dir, window);
            break;
        case Upload: {
            const auto files = pick_upload_files(*download_dir);
            if (!files.empty()) {
                queue_uploads(session, files, session.sftp_path);
                if (window)
                    window->wake();
            }
            break;
        }
        case Mkdir:
            session.prompt = Prompt::Mkdir;
            session.prompt_dir = session.sftp_path;
            session.prompt_text.clear();
            break;
        case Rename:
        case Remove:
            if (selected) {
                session.prompt_dir = session.sftp_path;
                session.prompt_src = selected->name;
                session.prompt_text = selected->name;
                session.confirm_delete = command == Remove;
                session.prompt = command == Rename ? Prompt::Rename : Prompt::None;
            }
            break;
        default:
            break;
        }
    }
}

// 确认 / 新建 / 重命名的浮层
void draw_prompt(aino::Ui& ui, Session& a, const aino::Theme& th) {
    if (!a.confirm_delete && a.prompt == Prompt::None)
        return;
    const aino::Input& in = ui.input();
    const float W = ui.width(), H = ui.height();
    const bool deleting = a.confirm_delete;
    const bool pasting = a.prompt == Prompt::Paste;
    const bool mkdir = a.prompt == Prompt::Mkdir;
    const char* title = deleting ? "确认删除" : pasting ? "粘贴到终端" : mkdir ? "新建文件夹" : "重命名";
    const std::string description = deleting ? "删除后无法恢复：" + a.prompt_src :
        pasting ? "剪贴板含换行，粘贴后可能执行命令。" : mkdir ? "文件夹名称" : "新名称";
    const auto lines = wrap_text(ui, description, 12, 320);
    const bool editing = !deleting && !pasting;
    const float detail_h = 20 * float(lines.size());
    const float height = 106 + detail_h + (editing ? 36 : 0);
    const Rect box = Rect::centered(W * .5f, H * .5f, 360, height);
    const float bx1 = box.x + 20, bx2 = box.right() - 20;
    const float bw = (bx2 - bx1 - 10) * .5f;
    ui.fill_rect({0, 0, W, H}, Color(0, 0, 0, 130));
    ui.fill_rect(box, th.popover_bg, 10);
    ui.stroke_rect(box, th.separator, 10, 1);
    ui.draw_text(title, {bx1, box.y + 14, bx2 - bx1, 22}, th.label, Style().font(14).bold());
    float detail_y = box.y + 42;
    for (const auto& line : lines) {
        ui.draw_text(line, {bx1, detail_y, bx2 - bx1, 20}, th.label2, Style().font(12));
        detail_y += 20;
    }
    if (editing)
        ui.input(&a.prompt_text, {bx1, detail_y + 4, bx2 - bx1, 28}, Style().hint("输入名称"));

    const bool cancel = dlg_button(ui, "取消", {bx1, box.bottom() - 46, bw, 30}, Btn::Normal) ||
                        in.key_pressed[aino::KEY_ESC];
    const bool confirm = dlg_button(ui, deleting ? "删除" : pasting ? "粘贴" : "确定",
                                    {bx2 - bw, box.bottom() - 46, bw, 30},
                                    deleting ? Btn::Danger : Btn::Primary) ||
                         (editing && in.key_pressed[aino::KEY_ENTER]);
    if (!cancel && !confirm)
        return;
    if (confirm && !cancel) {
        bool accepted = true;
        if (pasting) {
            send_key(a, a.prompt_text);
        } else if (deleting) {
            accepted = a.fs_push({FsOp::Remove, join_remote(a.prompt_dir, a.prompt_src), {}, "已删除 " + a.prompt_src});
        } else if (!remote_leaf(a.prompt_text)) {
            set_notice(a, "请输入单个文件名，不能使用斜杠、. 或 ..", true);
            return;
        } else if (mkdir) {
            accepted = a.fs_push({FsOp::Mkdir, join_remote(a.prompt_dir, a.prompt_text), {}, "已创建目录"});
        } else if (a.prompt_text != a.prompt_src) {
            accepted = a.fs_push({FsOp::Rename, join_remote(a.prompt_dir, a.prompt_src),
                                  join_remote(a.prompt_dir, a.prompt_text), "已重命名"});
        }
        if (!accepted)
            set_notice(a, "远程文件正在处理其他操作，请稍候重试", true);
    }
    a.confirm_delete = false;
    a.prompt = Prompt::None;
    a.prompt_text.clear();
    aino::detail::drop_focus(ui.impl);
    ui.request_animation_frame();
}

// =============================================================================
//  十、App：连接卡片 + 多标签
// =============================================================================
struct App {
    Clipboard clip;
    std::vector<Profile> profiles;
    ProfileStore profile_store;
    std::thread profile_thread;
    std::atomic<bool> profile_done{false};
    bool profile_ok = false;
    std::vector<Profile> profile_result;
    std::string profile_message;
    std::function<void()> profile_committed;

    ~App() {
        if (profile_thread.joinable()) profile_thread.join();
        shutdown_all();
    }
    bool profiles_busy() const { return profile_thread.joinable(); }
    bool queue_profiles(bool reload, std::vector<Profile> next = {}, std::function<void()> committed = {}) {
        if (profiles_busy()) { status = "连接配置正在处理，请稍候"; return false; }
        profile_done = false;
        profile_committed = std::move(committed);
        status = reload ? "正在读取连接配置…" : "正在保存连接配置…";
        profile_thread = std::thread([this, reload, next = std::move(next)]() mutable {
            try {
                profile_message.clear();
                if (reload) {
                    next = load_all_profiles(profile_store);
                    profile_ok = true;
                    profile_message = profile_store.warning;
                } else {
                    profile_ok = profile_store.save(next, &profile_message);
                }
                profile_result = std::move(next);
            } catch (const std::exception& e) {
                profile_ok = false;
                profile_message = std::string("连接配置处理失败：") + e.what();
            } catch (...) {
                profile_ok = false;
                profile_message = "连接配置处理失败";
            }
            profile_done.store(true, std::memory_order_release);
            if (win) win->wake();
        });
        return true;
    }
    void collect_profiles() {
        if (!profiles_busy() || !profile_done.load(std::memory_order_acquire)) return;
        profile_thread.join();
        if (profile_ok) profiles = std::move(profile_result);
        status = profile_message.empty() ? (profile_ok ? "连接配置已更新" : "连接配置未保存") : profile_message;
        auto committed = std::move(profile_committed);
        profile_committed = {};
        if (profile_ok && committed) committed();
    }
    std::vector<std::shared_ptr<Session>> tabs;

    // 正在后台收拾的会话。
    // 关闭标签不能就地析构：Session 里有个连接线程，可能正卡在 8 秒 TCP 超时或
    // 20 秒握手超时里；在 UI 线程 join 它 = 界面直接冻住。所以先摘到这儿，
    // 由后台线程 shutdown()，UI 每帧回收已经 retired 的项。
    std::vector<std::shared_ptr<Session>> dying;
    int active = -1;
    bool show_start = true;
    bool sidebar_open = true;
    int sidebar_page = 0; // 0：保存的连接；1：活动会话的 SFTP
    float sidebar_width = 300;
    std::string profile_filter;

    // 启动页
    std::string status;
    bool editor_open = false;
    int editing = -1; // profiles 下标；-1 = 新建
    Profile draft;
    bool draft_remember = false;
    bool draft_reveal = false;

    // 补录密码
    bool ask_open = false;
    int ask_index = -1;
    std::string ask_pass;
    bool ask_reveal = false;

    std::string download_dir;
    bool native_file_drop = false;
    aino::Window* win = nullptr;
    uint64_t next_session_scope = 1;

    // 有任何弹窗时，底下的启动页不能再接收点击 —— 否则在编辑器里点一下
    // 文字区，会顺带把盖在下面的连接卡片也"点开"，弹出个密码框
    bool modal_open() const {
        return editor_open || ask_open;
    }

    // 把第 i 个会话摘下来丢给后台收拾。UI 立刻就能继续画，不等任何网络超时。
    void retire(int i) {
        if (i < 0 || i >= int(tabs.size()))
            return;
        std::shared_ptr<Session> s = std::move(tabs[size_t(i)]);
        tabs.erase(tabs.begin() + i);
        if (s) {
            if (win)
                win->ui().release_state_scope(s->ui_scope);
            std::shared_ptr<Session> keep = s; // 让对象活到后台线程跑完
            std::thread([keep] {
                keep->shutdown();
            }).detach();
            dying.push_back(std::move(s));
        }
        if (tabs.empty()) {
            active = -1;
            show_start = true;
        } else {
            if (i < active)
                --active;
            active = std::min(active, int(tabs.size()) - 1);
            if (active < 0)
                active = 0;
        }
    }

    // 每帧回收：后台已经 shutdown 完的才真正释放（此时线程都 join 过了）
    void reap() {
        if (std::getenv("AINO_SSH_NO_REAP"))
            return; // 排障用：不释放，看崩溃是否消失
        for (size_t i = 0; i < dying.size();) {
            if (dying[i] && dying[i]->retired)
                dying.erase(dying.begin() + long(i));
            else
                ++i;
        }
    }

    // 退出程序时调用：等所有后台收拾完，再让 Window 析构
    void shutdown_all() {
        for (auto& session : tabs) if (session) session->engine.request_stop();
        for (auto& session : dying) if (session) session->engine.request_stop();
        for (auto& t : tabs) {
            if (t)
                t->shutdown();
        }
        tabs.clear();
        for (;;) {
            bool pending = false;
            for (auto& d : dying)
                if (d && !d->retired)
                    pending = true;
            if (!pending)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        dying.clear();
        active = -1;
    }
};

Session& active_session(App& app) {
    // 兜底：调用点都应该先判断下标，但这里绝不返回越界引用 ——
    // tabs[size_t(-1)] 是"点关标签就闪退"的直接死因。
    if (app.active < 0 || app.active >= int(app.tabs.size())) {
        static Session fallback; // 只会在编码错时被用到，正常路径永不触发
        return fallback;
    }
    return *app.tabs[size_t(app.active)];
}

bool save_profile(App& app, const Profile& p, int index, std::function<void()> committed = {}) {
    if (app.profiles_busy()) { app.status = "连接配置正在处理，请稍候"; return false; }
    Profile clean = p;
    clean.host = sshc::trim_connection_identity(clean.host);
    clean.user = sshc::trim_connection_identity(clean.user);
    if (!sshc::validate_connection_identity(clean.host, clean.port, clean.user, &app.status))
        return false;
    auto next = app.profiles;
    if (index >= 0 && index < int(next.size())) {
        const int src = next[size_t(index)].src;
        if (src != 0)
            return false; // 导入的不能改
        next[size_t(index)] = clean;
        next[size_t(index)].src = 0;
    } else {
        Profile q = clean;
        q.src = 0;
        next.push_back(q);
    }
    return app.queue_profiles(false, std::move(next), std::move(committed));
}

// 打开一个会话（补齐密码后再连）
void open_session(App& app, const Profile& p, const std::string& pass) {
    auto s = std::make_shared<Session>();
    s->ui_scope = app.next_session_scope++;
    s->host = sshc::trim_connection_identity(p.host);
    s->port = p.port > 0 ? p.port : 22;
    s->user = sshc::trim_connection_identity(p.user);
    s->pass = pass;
    s->key = p.key;
    s->title = !p.name.empty() ? p.name : (!s->user.empty() ? s->user + "@" + s->host : s->host);
    s->clip = &app.clip;
    s->term_focus = true;
    s->engine.set_wake([w = app.win] {
        if (w)
            w->wake();
    });
    begin_connect(*s, *app.win);
    app.tabs.push_back(std::move(s));
    app.active = int(app.tabs.size()) - 1;
    app.show_start = false;
    app.sidebar_page = 1;
    app.status.clear();
}

void close_tab(App& app, int i) {
    // 交给后台收拾：连接线程可能正卡在超时里，就地 join 会把界面冻住
    app.retire(i);
}

int find_profile_index(const App& app, const Session& s) {
    for (size_t i = 0; i < app.profiles.size(); ++i) {
        const Profile& p = app.profiles[i];
        if (p.host == s.host && p.port == s.port && p.user == s.user)
            return int(i);
    }
    return -1;
}

std::string profile_config_path(const Profile& profile) {
    if (profile.src == 2) {
        const std::string configured = vscode_config_file();
        if (!configured.empty() && !is_machine_wide_path(configured))
            return configured;
    }
    return ssh_config_path();
}

bool open_config_file(App& app, const std::string& path = ssh_config_path(),
                      const std::function<bool(const std::string&, std::string*)>& opener = open_external) {
    std::string error;
    if (!ensure_ssh_config(path, &error) || !opener(path, &error)) {
        app.status = error.empty() ? "无法打开 SSH 配置文件" : error;
        return false;
    }
    app.status = "已请求系统编辑器打开配置；保存后点击「重新导入连接」。";
    return true;
}

// 覆盖在终端区中央的失败说明卡（不是一行小字 —— 那样等于没提示）
void draw_fail_panel(aino::Ui& ui, Session& a, App& app, const aino::Theme& th, const Rect& area) {
    const Session::ConnInfo cs = a.conn_snapshot();
    if (cs.err.empty() || cs.connecting)
        return;

    const FailExplain fx = explain_fail(cs.fail_at);
    const float pad = 22.0f;
    const float pw = std::min(600.0f, area.w - 60.0f);
    const float text_w = pw - pad * 2.0f;
    const auto err_lines = wrap_text(ui, cs.err, 12.5f, text_w);
    const auto adv_lines = wrap_text(ui, fx.advice ? fx.advice : "", 12.0f, text_w);

    float ph = pad + 26.0f + 10.0f;               // 标题
    ph += float(err_lines.size()) * 19.0f + 8.0f; // 原因
    if (!adv_lines.empty())
        ph += float(adv_lines.size()) * 18.0f + 12.0f;
    ph += 40.0f + pad; // 按钮行

    const Rect panel(area.x + (area.w - pw) * 0.5f, area.y + std::max(24.0f, (area.h - ph) * 0.42f), pw, ph);
    ui.fill_rect(panel, th.is_dark() ? Color::hex(0x14161B) : th.control_bg, 10.0f);
    ui.stroke_rect(panel, th.is_dark() ? Color::hex(0x2A2F3A) : th.separator, 10.0f, 1.0f);
    // 错误状态强调线。
    ui.fill_rect(Rect(panel.x + 1.0f, panel.y, panel.w - 2.0f, 2.0f), fx.color, 0.0f);

    float y = panel.y + pad;
    ui.draw_text(fx.title, Rect(panel.x + pad, y, text_w, 24.0f), th.is_dark() ? fx.color : th.label,
                 Style().font(15.0f).bold());
    y += 26.0f + 10.0f;
    for (const std::string& ln : err_lines) {
        ui.draw_text(ln, Rect(panel.x + pad, y, text_w, 18.0f), th.label, Style().font(12.5f));
        y += 19.0f;
    }
    y += 8.0f;
    for (const std::string& ln : adv_lines) {
        ui.draw_text(ln, Rect(panel.x + pad, y, text_w, 17.0f), th.label2, Style().font(12.0f));
        y += 18.0f;
    }
    if (!adv_lines.empty())
        y += 12.0f;

    const float bw = 92.0f, bh = 30.0f;
    const Rect r_retry(panel.x + pad, y, bw, bh);
    const Rect r_edit(panel.x + pad + bw + 10.0f, y, bw + 12.0f, bh);
    if (dlg_button(ui, "重试", r_retry, Btn::Primary)) {
        if (app.win)
            begin_connect(a, *app.win);
    }
    if (dlg_button(ui, "编辑连接", r_edit, Btn::Normal)) {
        const int idx = find_profile_index(app, a);
        if (idx >= 0 && app.profiles[size_t(idx)].src == 0) {
            app.editor_open = true;
            app.editing = idx;
            app.draft = app.profiles[size_t(idx)];
            app.draft_remember = app.profiles[size_t(idx)].remember;
            app.draft_reveal = false;
        } else {
            open_config_file(app,
                             idx >= 0 ? profile_config_path(app.profiles[size_t(idx)]) : ssh_config_path());
        }
    }
}

// ---- 会话视图 ----
void session_frame(aino::Ui& ui, Session& a, App& app, const aino::Theme& th, const Rect& body);

void new_profile(App& app) {
    if (app.profiles_busy()) return;
    app.editor_open = true;
    app.editing = -1;
    app.draft = Profile{};
    app.draft_reveal = app.draft_remember = false;
}

void request_connection(App& app, int index, bool reuse = false) {
    if (index < 0 || index >= int(app.profiles.size()))
        return;
    const Profile& profile = app.profiles[size_t(index)];
    if (reuse)
        for (size_t i = 0; i < app.tabs.size(); ++i) {
            const auto& session = app.tabs[i];
            if (session->host == profile.host && session->port == profile.port &&
                session->user == profile.user) {
                app.active = int(i);
                app.show_start = false;
                session->term_focus = true;
                if (app.win)
                    app.win->ui().set_focus(nullptr);
                return;
            }
        }
    if (app.win && (!profile.pass.empty() || !profile.key.empty()))
        open_session(app, profile, profile.pass);
    else {
        app.ask_open = true;
        app.ask_index = index;
        app.ask_pass.clear();
        app.ask_reveal = false;
    }
}

struct ConnectionRow {
    int index = 0;
    std::vector<std::string> title, detail;
    float height = 76;
};

bool matches_profile(const Profile& profile, const std::string& filter) {
    auto fold = [](std::string text) {
        for (char& c : text)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    return filter.empty() ||
           fold(profile.name + " " + profile.host + " " + profile.user).find(fold(filter)) !=
               std::string::npos;
}

ConnectionRow connection_row(aino::Ui& ui, const Profile& profile, int index, float width, bool compact) {
    ConnectionRow row;
    row.index = index;
    const float text_width = std::max(40.f, width - (compact ? 100.f : 286.f));
    row.title = wrap_text(ui, profile.name.empty() ? profile.host : profile.name, 12.5f, text_width, 600);
    row.detail = wrap_text(ui,
                           (profile.user.empty() ? "默认用户" : profile.user) + "@" + profile.host + ":" +
                               std::to_string(profile.port),
                           11.f, text_width);
    row.height = std::max(76.f, 28.f + float(row.title.size()) * 18.f + float(row.detail.size()) * 16.f);
    return row;
}

void draw_connection_text(aino::Ui& ui, const ConnectionRow& layout, const Rect& row,
                          const aino::Theme& theme, bool compact) {
    const float text_width = std::max(40.f, row.w - (compact ? 100.f : 286.f));
    const float text_x = row.x + 44;
    float y = row.y + 12;
    for (const auto& line : layout.title) {
        ui.draw_text(line, {text_x, y, text_width, 18}, theme.label, Style().font(12.5f).semi_bold());
        y += 18;
    }
    for (const auto& line : layout.detail) {
        ui.draw_text(line, {text_x, y, text_width, 16}, theme.label2, Style().font(11));
        y += 16;
    }
}

void draw_connections(aino::Ui& ui, App& app, const aino::Theme& theme, const Rect& view, bool compact) {
    const float margin = compact ? 12.f : 28.f;
    const float width = std::min(compact ? view.w - margin * 2 : 820.f, view.w - margin * 2);
    const float x = view.cx() - width * .5f;
    std::array<std::vector<ConnectionRow>, 3> groups;
    float total = 8;
    int count = 0;
    for (size_t i = 0; i < app.profiles.size(); ++i) {
        const auto& profile = app.profiles[i];
        if (!matches_profile(profile, app.profile_filter))
            continue;
        auto row = connection_row(ui, profile, int(i), width, compact);
        groups[size_t(std::max(0, std::min(2, profile.src)))].push_back(std::move(row));
        ++count;
    }
    for (const auto& group : groups)
        if (!group.empty()) {
            total += 38 + 24;
            for (const auto& row : group)
                total += row.height;
        }
    if (!count)
        total += 164;
    auto scroll = ui.scroll(view, total);
    float y = view.y + 8 - scroll.offset;
    for (size_t source = 0; source < groups.size(); ++source) {
        const auto& group = groups[source];
        if (group.empty())
            continue;
        const std::string title = std::string(src_badge(int(source))) + " · " + std::to_string(group.size());
        ui.label(title, {x + 2, y, width - 4, 24}, Style().font(13).semi_bold());
        y += 38;
        float height = 0;
        for (const auto& row : group)
            height += row.height;
        ui.panel({x, y, width, height},
                 Style().background(theme.is_dark() ? theme.card_bg : theme.control_bg).corner(12));
        for (size_t i = 0; i < group.size(); ++i) {
            const auto& layout = group[i];
            const auto& profile = app.profiles[size_t(layout.index)];
            const Rect row(x, y, width, layout.height);
            if (row.bottom() >= view.y && row.y < view.bottom()) {
                bool active = false;
                if (!app.show_start && app.active >= 0 && app.active < int(app.tabs.size()))
                    active = find_profile_index(app, *app.tabs[size_t(app.active)]) == layout.index;
                if (compact) {
                    Style style = Style().ghost().id(0x434F4E0000ull + uint64_t(layout.index));
                    style.disabled = g_block_input;
                    if (ui.button("", {row.x, row.y, row.w - 48, row.h}, style))
                        request_connection(app, layout.index, true);
                    if (active)
                        ui.fill_rect({row.x + 1, row.y + 8, 3, row.h - 16}, theme.tint, 1.5f);
                }
                ui.system_icon(aino::SystemIcon::Network, {row.x + 14, row.y + 17, 18, 18},
                               active ? theme.tint : theme.label2);
                draw_connection_text(ui, layout, row, theme, compact);
                if (compact) {
                    const Rect connect(row.right() - 42, row.cy() - 16, 32, 32);
                    Style style = Style().ghost().id(0x434F4E1000ull + uint64_t(layout.index));
                    style.disabled = g_block_input;
                    if (ui.icon_button("", connect, style))
                        request_connection(app, layout.index);
                    ui.system_icon(aino::SystemIcon::Play, connect.inset(8), theme.tint);
                    ui.tooltip(connect, "新建此服务器的会话");
                } else {
                    const float action = row.right() - 226;
                    if (dlg_button(ui, "连接", {action, row.cy() - 15, 64, 30}, Btn::Normal)) {
                        request_connection(app, layout.index);
                        return;
                    }
                    if (profile.src == 0) {
                        if (tool_button(ui, "编辑", {action + 76, row.cy() - 15, 60, 30}, false) && !app.profiles_busy()) {
                            app.editor_open = true;
                            app.editing = layout.index;
                            app.draft = profile;
                            app.draft_remember = profile.remember;
                            app.draft_reveal = false;
                            return;
                        }
                        if (tool_button(ui, "删除", {action + 148, row.cy() - 15, 60, 30}, true)) {
                            auto next = app.profiles;
                            next.erase(next.begin() + layout.index);
                            app.queue_profiles(false, std::move(next));
                            return;
                        }
                    } else if (tool_button(ui, "打开配置", {action + 76, row.cy() - 15, 132, 30}, false)) {
                        open_config_file(app, profile_config_path(profile));
                        return;
                    }
                }
            }
            y += row.h;
            if (i + 1 < group.size())
                ui.separator({x + 16, y - 1, width - 32, 1});
        }
        y += 24;
    }
    if (!count) {
        ui.panel({x, y, width, 140},
                 Style().background(theme.is_dark() ? theme.card_bg : theme.control_bg).corner(12));
        ui.label(app.profile_filter.empty() ? "还没有连接" : "没有匹配的连接",
                 {x + 12, y + 28, width - 24, 24}, Style().font(14).semi_bold().center());
        if (!app.profile_filter.empty()) {
            if (tool_button(ui, "清除搜索", {x + width * .5f - 52, y + 76, 104, 30}, false))
                app.profile_filter.clear();
        } else if (dlg_button(ui, "新建连接", {x + width * .5f - 52, y + 76, 104, 30}, Btn::Primary))
            new_profile(app);
    }
}

void draw_start(aino::Ui& ui, App& app, const aino::Theme& theme, const Rect& view) {
    ui.fill_rect(view, theme.is_dark() ? theme.window_bg : Color::hex(0xF5F5F7));
    const float width = std::min(820.f, view.w - 56);
    const float x = view.cx() - width * .5f;
    ui.heading("连接管理", {x, view.y + 26, width, 36}, Style().font(26).bold());
    ui.draw_text("选择连接开启终端；已有会话可从顶部标签继续。", {x, view.y + 72, width, 22}, theme.label2,
                 Style().font(12));
    const Rect list(view.x, view.y + 108, view.w, std::max(0.f, view.h - 108));
    draw_connections(ui, app, theme, list, false);
}

struct WorkspaceLayout {
    Rect sidebar, tabs, content;
};

WorkspaceLayout workspace_layout(aino::Ui& ui, App& app) {
    const Rect body(0, 56, ui.width(), std::max(0.f, ui.height() - 56));
    float start = 0;
    if (app.sidebar_open) {
        const float maximum = std::min(420.f, std::max(260.f, body.w - 467.f));
        app.sidebar_width = std::max(260.f, std::min(maximum, app.sidebar_width));
        float ratio = (app.sidebar_width + 3.5f) / body.w;
        if (!g_block_input)
            ui.split_pane(&ratio, body, nullptr, nullptr, true, 0.f, Style().id(0x53534853504C4954ull));
        app.sidebar_width = std::max(260.f, std::min(maximum, body.w * ratio - 3.5f));
        start = app.sidebar_width + 7;
    }
    return {{0, body.y, start ? app.sidebar_width : 0, body.h},
            {start, body.y, body.w - start, 40},
            {start, body.y + 40, body.w - start, std::max(0.f, body.h - 40)}};
}

void draw_toolbar(aino::Ui& ui, App& app, const aino::Theme& theme) {
    ui.fill_rect({0, 0, ui.width(), 56}, theme.card_bg);
    ui.separator({0, 55, ui.width(), 1});
    Style style = Style().ghost();
    style.disabled = g_block_input;
    const Rect toggle(12, 12, 32, 32);
    if (ui.icon_button("", toggle, style))
        app.sidebar_open = !app.sidebar_open;
    ui.system_icon(aino::SystemIcon::Navigation, toggle.inset(7), theme.label2);
    ui.tooltip(toggle, app.sidebar_open ? "收起侧栏" : "展开侧栏");
    ui.label("aino SSH", {56, 14, 94, 28}, Style().font(16).semi_bold());
    if (dlg_button(ui, "新建连接", {164, 12, 104, 32}, Btn::Primary))
        new_profile(app);
    if (tool_button(ui, "连接管理", {280, 12, 96, 32}, false))
        app.show_start = true;
    if (tool_button(ui, "打开配置文件", {ui.width() - 286, 12, 120, 32}, false))
        open_config_file(app);
    if (tool_button(ui, "重新导入连接", {ui.width() - 156, 12, 104, 32}, false)) {
        app.queue_profiles(true);
    }
    const Rect appearance(ui.width() - 42, 12, 30, 32);
    if (ui.icon_button("", appearance, style))
        ui.set_theme(theme.is_dark() ? aino::Theme::light() : aino::Theme::dark());
    ui.system_icon(theme.is_dark() ? aino::SystemIcon::Sun : aino::SystemIcon::Moon, appearance.inset(7),
                   theme.label2);
    ui.tooltip(appearance, "切换深浅主题");
}

void draw_workspace_sidebar(aino::Ui& ui, App& app, const aino::Theme& theme, const Rect& area) {
    ui.fill_rect(area, theme.sidebar_bg);
    ui.separator({area.right(), area.y, 1, area.h});
    Style style;
    style.disabled = g_block_input;
    ui.segmented({"连接", "远程文件"}, &app.sidebar_page, {area.x + 12, area.y + 12, area.w - 24, 30}, style);
    if (app.sidebar_page == 1) {
        if (app.active >= 0 && app.active < int(app.tabs.size())) {
            auto& session = *app.tabs[size_t(app.active)];
            auto scope = ui.state_scope(session.ui_scope);
            draw_sftp(ui, session, theme, {area.x, area.y + 52, area.w, area.h - 52}, &app.download_dir,
                      app.win, app.native_file_drop);
        } else {
            ui.label("尚无活动会话", {area.x + 16, area.y + 88, area.w - 32, 24},
                     Style().font(14).semi_bold().center());
            ui.draw_text("连接服务器后在这里浏览远程文件。", {area.x + 16, area.y + 124, area.w - 32, 20},
                         theme.label2, Style().font(11));
            if (tool_button(ui, "查看所有连接", {area.cx() - 64, area.y + 166, 128, 32}, false)) {
                app.sidebar_page = 0;
                app.show_start = true;
            }
        }
        return;
    }
    ui.search_field(&app.profile_filter, {area.x + 12, area.y + 58, area.w - 24, 30},
                    style.hint("搜索名称、主机、用户"));
    const auto path_lines = wrap_text(ui, display_path(ssh_config_path()), 10.5f, area.w - 56);
    const float footer_h = 48 + float(path_lines.size()) * 15;
    const Rect footer(area.x + 12, area.bottom() - 22 - footer_h, area.w - 24, footer_h);
    ui.panel(footer, Style().background(theme.is_dark() ? theme.card_bg : theme.control_bg).corner(12));
    ui.label("SSH 配置文件", {footer.x + 16, footer.y + 12, footer.w - 32, 18}, Style().font(11).semi_bold());
    float y = footer.y + 34;
    for (const auto& line : path_lines) {
        ui.draw_text(line, {footer.x + 16, y, footer.w - 32, 15}, theme.label2, Style().font(10.5f));
        y += 15;
    }
    const Rect list(area.x, area.y + 104, area.w, std::max(0.f, footer.y - 16 - area.y - 104));
    draw_connections(ui, app, theme, list, true);
}

// ---- 连接编辑器（新建 / 编辑）----
void draw_editor(aino::Ui& ui, App& app, const aino::Theme& th) {
    g_block_input = app.profiles_busy();
    const float W = ui.width(), H = ui.height();
    const aino::Input& in = ui.input();
    ui.fill_rect(Rect(0, 0, W, H), Color(0, 0, 0, 140));

    const Rect box(W * 0.5f - 260.0f, H * 0.5f - 190.0f, 520.0f, 380.0f);
    ui.fill_rect(box, th.popover_bg, 12.0f);
    ui.stroke_rect(box, th.separator, 12.0f, 1.0f);

    const float x1 = box.x + 24.0f, x2 = box.right() - 24.0f;
    ui.draw_text(app.editing >= 0 ? "编辑连接" : "新建连接", Rect(x1, box.y + 18.0f, x2 - x1, 24.0f),
                 th.label, Style().font(15.0f).bold());

    const float lh = 54.0f;
    float y = box.y + 62.0f;
    const float half = (x2 - x1 - 12.0f) * 0.5f;

    field(ui, th, "名称（显示在连接列表中）", &app.draft.name, Rect(x1, y, x2 - x1, 30.0f));
    y += lh;
    field(ui, th, "主机", &app.draft.host, Rect(x1, y, half, 30.0f));
    {
        std::string port_s = std::to_string(app.draft.port);
        ui.draw_text("端口", Rect(x1 + half + 12.0f, y - 16.0f, half, 14.0f), th.label2,
                     Style().font(11.0f));
        Style edit_style; edit_style.disabled = app.profiles_busy();
        if (ui.input(&port_s, Rect(x1 + half + 12.0f, y, half, 30.0f), edit_style)) {
            const int v = std::atoi(port_s.c_str());
            if (v > 0 && v < 65536)
                app.draft.port = v;
        }
    }
    y += lh;
    field(ui, th, "用户", &app.draft.user, Rect(x1, y, half, 30.0f));
    ui.draw_text("密码", Rect(x1 + half + 12.0f, y - 16.0f, half, 14.0f), th.label2,
                 Style().font(11.0f));
    password_field(ui, th, &app.draft.pass, &app.draft_reveal, Rect(x1 + half + 12.0f, y, half, 30.0f));
    y += lh;
    field(ui, th, "私钥（可选，密码为空时用）", &app.draft.key, Rect(x1, y, x2 - x1, 30.0f));
    y += 40.0f;

    // 记住密码
    {
        const Rect cb(x1, y, 16.0f, 16.0f);
        Style edit_style; edit_style.disabled = app.profiles_busy();
        ui.checkbox(&app.draft_remember, "", cb, edit_style);
        ui.draw_text("记住密码（由当前用户的系统凭据服务保护）",
                     Rect(cb.right() + 10.0f, y - 2.0f, x2 - cb.right() - 20.0f, 20.0f), th.label2,
                     Style().font(11.5f));
    }

    // 按钮
    const float by = box.bottom() - 52.0f;
    const float bw = (x2 - x1 - 20.0f) / 3.0f;
    const bool cancel = dlg_button(ui, "取消", Rect(x1, by, bw, 34.0f), Btn::Normal);
    if (!app.profiles_busy() && (cancel || in.key_pressed[aino::KEY_ESC])) {
        app.editor_open = false;
        return;
    }
    app.draft.remember = app.draft_remember;
    if (dlg_button(ui, "仅保存", Rect(x1 + bw + 10.0f, by, bw, 34.0f), Btn::Normal)) {
        if (!app.draft.host.empty()) {
            save_profile(app, app.draft, app.editing, [&app] { app.editor_open = false; });
        }
    }
    if (dlg_button(ui, "保存并连接", Rect(x1 + (bw + 10.0f) * 2.0f, by, bw, 34.0f), Btn::Primary)) {
        if (!app.draft.host.empty()) {
            const Profile p = app.draft;
            save_profile(app, p, app.editing, [&app, p] {
            const std::string save_warning = app.status;
            app.editor_open = false;
            if (!p.pass.empty() || !p.key.empty())
                open_session(app, p, p.pass);
            else {
                app.ask_open = true;
                app.ask_index = -1;
                app.ask_pass.clear();
                app.ask_reveal = false;
                app.draft = p; // 待补密码后连接
            }
            if (!save_warning.empty())
                app.status = save_warning;
            });
        }
    }
}

// ---- 补录密码 ----
void draw_ask_password(aino::Ui& ui, App& app, const aino::Theme& th) {
    g_block_input = false;
    const float W = ui.width(), H = ui.height();
    const aino::Input& in = ui.input();
    ui.fill_rect(Rect(0, 0, W, H), Color(0, 0, 0, 140));
    const Rect box(W * 0.5f - 200.0f, H * 0.5f - 90.0f, 400.0f, 180.0f);
    ui.fill_rect(box, th.popover_bg, 12.0f);
    ui.stroke_rect(box, th.separator, 12.0f, 1.0f);

    std::string who = app.draft.host;
    if (app.ask_index >= 0 && app.ask_index < int(app.profiles.size()))
        who =
            app.profiles[size_t(app.ask_index)].name + "  (" + app.profiles[size_t(app.ask_index)].host + ")";

    const float x1 = box.x + 24.0f, x2 = box.right() - 24.0f;
    ui.draw_text("输入密码", Rect(x1, box.y + 18.0f, x2 - x1, 22.0f), th.label,
                 Style().font(14.0f).bold());
    ui.draw_text(who, Rect(x1, box.y + 44.0f, x2 - x1, 18.0f), th.label2, Style().font(11.5f));
    password_field(ui, th, &app.ask_pass, &app.ask_reveal, Rect(x1, box.y + 68.0f, x2 - x1, 32.0f));

    const float by = box.bottom() - 50.0f;
    const float bw = (x2 - x1 - 10.0f) * 0.5f;
    if (dlg_button(ui, "取消", Rect(x1, by, bw, 32.0f), Btn::Normal) || in.key_pressed[aino::KEY_ESC]) {
        app.ask_open = false;
        return;
    }
    const bool go =
        dlg_button(ui, "连接", Rect(x2 - bw, by, bw, 32.0f), Btn::Primary) || in.key_pressed[aino::KEY_ENTER];
    if (go && !app.ask_pass.empty()) {
        Profile p;
        if (app.ask_index >= 0 && app.ask_index < int(app.profiles.size()))
            p = app.profiles[size_t(app.ask_index)];
        else
            p = app.draft;
        app.ask_open = false;
        open_session(app, p, app.ask_pass);
    }
}

// ---- 标签栏 ----
void draw_tabs(aino::Ui& ui, App& app, const aino::Theme& th, const Rect& area) {
    const float W = area.w, h = area.h;
    ui.fill_rect(area, th.card_bg);
    ui.separator({area.x, area.bottom() - 1, W, 1});
    Style home_style = Style().font(12).padding(12);
    home_style.disabled = g_block_input;
    if (ui.nav_item("连接管理", app.show_start, {area.x + 8, area.y + 6, 80, h - 12}, home_style))
        app.show_start = true;

    std::vector<std::string> labels;
    std::vector<Color> indicators;
    labels.reserve(app.tabs.size());
    indicators.reserve(app.tabs.size());
    for (const auto& session : app.tabs) {
        labels.push_back(session->title);
        indicators.push_back(session->conn_busy() ? kWarn : (session->engine.connected() ? kOk : th.label3));
    }
    int selected = app.show_start ? -1 : app.active;
    int closed = -1;
    aino::TabOptions options;
    options.close_index = &closed;
    options.indicators = &indicators;
    options.max_width = 200.0f;
    Style style = Style().font(12.5f);
    style.disabled = g_block_input;
    if (ui.tabs(labels, &selected, Rect(area.x + 96, area.y + 6, std::max(0.0f, W - 140), h - 12), options,
                style)) {
        app.active = selected;
        app.show_start = false;
        if (selected >= 0 && selected < int(app.tabs.size()))
            app.tabs[size_t(selected)]->term_focus = true;
        ui.set_focus(nullptr);
    }

    const Rect add(area.right() - 36, area.y + (h - 24) * .5f, 24, 24);
    Style add_style = Style().ghost();
    add_style.disabled = g_block_input;
    if (ui.icon_button("", add, add_style))
        app.show_start = true;
    ui.system_icon(aino::SystemIcon::Plus, add.inset(6.0f),
                   (app.show_start ? th.tint : th.label2).scaled_alpha(g_block_input ? 0.45f : 1.0f));

    if (closed >= 0)
        close_tab(app, closed);
}

void update_session(Session& a, aino::Window* window = nullptr) {
    if (std::string error = a.engine.take_io_error(); !error.empty())
        set_notice(a, error, true);
    // 后台收字节 → 解析器
    if (std::string out = a.engine.take_output(); !out.empty()) {
        a.term.feed(out.data(), out.size());
        if (a.follow)
            a.view_offset = 0;
    }
    if (std::string rep = a.term.take_reply(); !rep.empty())
        a.engine.send(rep);

    {
        Session::TransferView t;
        {
            std::lock_guard<std::mutex> lk(a.xfer_mtx);
            if (a.xfer_view.finished) {
                t = a.xfer_view;
                a.xfer_view.finished = false;
            }
        }
        if (t.finished) {
            set_notice(a, t.error.empty() ? ("完成：" + t.name) : t.error, !t.error.empty());
            if (!a.xfer_cancel && !t.error.empty()) {
                ++a.upload_errors;
                a.last_transfer_error = t.error;
            }
            a.refresh_after_transfer = true;
        }
    }

    // 后台文件操作结果
    {
        FsReq req;
        std::vector<Entry> got;
        std::string err, cwd, tag;
        bool has = false;
        {
            std::lock_guard<std::mutex> lk(a.fs_mtx);
            if (a.fs_done) {
                a.fs_done = false;
                has = true;
                req = a.fs_done_req;
                got.swap(a.fs_done_entries);
                err = a.fs_done_err;
                cwd = a.fs_done_cwd;
                tag = a.fs_done_tag;
            }
        }
        if (has) {
            if (req.op == FsOp::List) {
                if (err.empty()) {
                    const bool changed = !cwd.empty() && cwd != a.sftp_path;
                    std::string selected_name;
                    if (!changed && a.sel > 0 && a.sel <= int(a.entries.size()))
                        selected_name = a.entries[size_t(a.sel - 1)].name;
                    a.entries.swap(got);
                    if (!cwd.empty())
                        a.sftp_path = cwd;
                    if (changed) {
                        a.reset_file_scroll = true;
                        a.file_filter.clear();
                        a.file_menu.open = false;
                    }
                    a.sel = -1;
                    if (!selected_name.empty()) {
                        const auto selected = std::find_if(a.entries.begin(), a.entries.end(), [&](const Entry& entry) {
                            return entry.name == selected_name;
                        });
                        if (selected != a.entries.end())
                            a.sel = int(selected - a.entries.begin()) + 1;
                        else
                            a.file_menu.open = false;
                    }
                } else {
                    set_notice(a, err, true);
                }
            } else {
                set_notice(a, err.empty() ? tag : err, !err.empty());
                refresh_list(a);
            }
        }
    }
    if (!a.xfer_running && !a.conn_busy()) {
        if (!a.engine.connected() && !a.uploads.empty()) {
            a.uploads.clear();
            set_notice(a, "连接已断开，待上传文件已移出队列", true);
        }
        if (!a.uploads.empty() && !file_work_pending(a)) {
            auto job = std::move(a.uploads.front());
            a.uploads.pop_front();
            if (!start_transfer(a, true, job.local, job.remote, window, false)) {
                ++a.upload_errors;
                a.last_transfer_error = a.notice;
                a.refresh_after_transfer = true;
                if (window)
                    window->wake();
            }
        } else if (a.uploads.empty() && a.refresh_after_transfer && !file_work_pending(a)) {
            a.refresh_after_transfer = false;
            if (a.upload_errors) {
                set_notice(a, "传输结束，" + std::to_string(a.upload_errors) + " 项失败。" + a.last_transfer_error, true);
                a.upload_errors = 0;
                a.last_transfer_error.clear();
            }
            refresh_list(a);
        }
    }
}

// ---- 会话主体 ----
void session_frame(aino::Ui& ui, Session& a, App& app, const aino::Theme& th, const Rect& body) {
    auto owner = ui.state_scope(a.ui_scope);
    const Session::ConnInfo cs = a.conn_snapshot();
    const float W = body.w, H = body.h;
    const auto transfer = a.transfer_snapshot();
    const double now =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!a.notice.empty() && now - a.notice_at < 3)
        ui.request_frame_in(float(3 - (now - a.notice_at)));
    const bool has_notice = !a.notice.empty() && now - a.notice_at < 3;
    const std::string status = has_notice ? a.notice : !app.status.empty() ? app.status :
        a.engine.connected() ? "右键菜单 · Ctrl+Shift+C/V/A · " + std::to_string(a.term.cols) + " × " +
                               std::to_string(a.term.rows) : "尚未连接";
    const auto status_lines = wrap_text(ui, status, 11.f, W - 28);
    const auto transfer_lines =
        wrap_text(ui, transfer.name + " · " + human_size(transfer.done) + " / " + human_size(transfer.total) +
                      (a.uploads.empty() ? "" : " · 待上传 " + std::to_string(a.uploads.size()) + " 项"),
                  11.f, W - 224);
    const float transfer_h = transfer.busy ? std::max(30.f, float(transfer_lines.size()) * 16 + 8) : 0;
    const float status_h = 12 + float(status_lines.size()) * 16 + transfer_h;
    const Rect r_frame(body.x, body.y, W, H - status_h);
    const auto target_lines =
        wrap_text(ui, a.user + "@" + a.host + ":" + std::to_string(a.port), 11.5f, W - 208);
    const float strip_h = std::max(40.f, 16 + float(target_lines.size()) * 17);
    ui.fill_rect({r_frame.x, r_frame.y, r_frame.w, strip_h}, th.card_bg);
    ui.separator({r_frame.x, r_frame.y + strip_h - 1, r_frame.w, 1});
    float target_y = r_frame.y + 8;
    for (const auto& line : target_lines) {
        ui.draw_text(line, {r_frame.x + 14, target_y, W - 208, 17}, th.label, Style().font(11.5f));
        target_y += 17;
    }
    const bool connected = a.engine.connected();
    const char* state = cs.connecting ? "连接中…" : connected ? "已连接" : "已断开";
    ui.fill_circle(r_frame.right() - 184, r_frame.y + strip_h * .5f, 3,
                   cs.connecting ? kWarn
                   : connected   ? kOk
                                 : th.label3);
    ui.draw_text(state, {r_frame.right() - 174, r_frame.y, 86, strip_h}, th.label2, Style().font(11));
    if (tool_button(ui, "关闭", {r_frame.right() - 82, r_frame.y + (strip_h - 28) * .5f, 70, 28}, false)) {
        app.retire(app.active);
        return;
    }

    const Rect r_ter(r_frame.x, r_frame.y + strip_h, r_frame.w, r_frame.h - strip_h);
    a.r_term = r_ter;

    // 尺寸变化 → 通知远端（否则 vim 会花屏）
    //
    // 这里不能只判"我自己的 term 尺寸变没变"：开 shell 时传的是**当时**的
    // term 尺寸（通常还是默认 80x24），而布局测量往往在通道开好之后才跑完。
    // 一旦测量结果和 begin_connect 用的值不同，`a.term` 已经被改成新尺寸，
    // 后续每帧都比较不出差异，pty 就永远卡在 80x24 —— 远端按 80 列折行、
    // 把状态栏/命令行画在第 24 行，界面上就是"内容挤在左上角、底部提示不见了"。
    // 所以要比 **远端实际认的 pty 尺寸**（engine.pty_cols/rows），每帧收敛一次。
    const TermMetrics tm = measure_term(ui, r_ter, a.font);
    if (tm.cols != a.term.cols || tm.rows != a.term.rows)
        a.term.resize(tm.cols, tm.rows);
    if (a.engine.shell_open() && (a.engine.pty_cols() != tm.cols || a.engine.pty_rows() != tm.rows)) {
        const auto now = std::chrono::steady_clock::now();
        const bool changed = (tm.cols != a.pty_req_cols || tm.rows != a.pty_req_rows);
        // 尺寸一变立刻发；没变但还没同步上（上次失败）就每 0.5s 补一次，
        // 避免服务端拒收时每帧往通道里塞一个请求。
        if (changed || now - a.pty_req_at > std::chrono::milliseconds(500)) {
            a.pty_req_cols = tm.cols;
            a.pty_req_rows = tm.rows;
            a.pty_req_at = now;
            a.engine.request_pty_resize(tm.cols, tm.rows);
        }
    }

    ui.fill_rect(r_ter, kTermBg);
    bool empty_terminal = a.term.history_size() == 0;
    for (int row = 0; empty_terminal && row < a.term.rows; ++row)
        empty_terminal = !row_has_text(a.term.screen_row(row));
    if (!a.engine.connected() && empty_terminal) {
        ui.draw_text("连接后这里就是远程 shell", Rect(r_ter.x, r_ter.y + 24.0f, r_ter.w, 20.0f),
                     Color::hex(0x3F4450), Style().font(12.0f).center());
    }
    draw_terminal(ui, a, r_ter, tm, a.term_focus);

    // 连接失败的说明卡盖在终端上：一行小字提示等于没提示，
    // 用户真正想知道的是"网络不通"还是"密码错"。
    const bool fail_shown = !cs.err.empty() && !cs.connecting;
    draw_fail_panel(ui, a, app, th, r_ter);

    // 状态快照落盘：GUI 没法用眼睛断言的地方（连接状态、终端内容、文件列表），
    // 靠它做自动化核对。AINO_SSH_DEBUG_FILE=build/ssh_state.txt
    if (const char* dbg = std::getenv("AINO_SSH_DEBUG_FILE")) {
        if (std::FILE* f = std::fopen(dbg, "wb")) {
            std::fprintf(
                f,
                "conn=%d shell=%d rows=%d cols=%d total=%d hist=%d sftp_path=%s sftp_err=%s entries=%zu busy=%d tabs=%zu\n",
                int(a.engine.connected()), int(a.engine.shell_open()), a.term.rows, a.term.cols,
                a.term.total_lines(), a.term.history_size(), a.sftp_path.c_str(), cs.sftp_err.c_str(),
                a.entries.size(), int(a.fs_busy.load()), app.tabs.size());
            std::fprintf(
                f,
                "connecting=%d stage=%d conn_msg=%s conn_err=%s notice=%s bad=%d prompt=%d prompt_text=%s\n",
                int(cs.connecting), int(cs.fail_at), cs.msg.c_str(), cs.err.c_str(), a.notice.c_str(),
                int(a.notice_bad), int(a.prompt), a.prompt_text.c_str());
            std::fprintf(f, "selection=%d term_menu=%d file_menu=%d file_selection=%d queued_uploads=%zu\n",
                         int(a.term.has_selection()), int(a.term_menu.open), int(a.file_menu.open),
                         a.sel, a.uploads.size());
            for (const Entry& e : a.entries)
                std::fprintf(f, "  file %s%s size=%lld\n", e.name.c_str(), e.dir ? "/" : "",
                             (long long)e.size);
            const int from = std::max(0, a.term.total_lines() - a.term.rows);
            for (int i = from; i < a.term.total_lines(); ++i)
                std::fprintf(f, "[%d] %s\n", i, a.term.line_text(i).c_str());
            std::fclose(f);
        }
    }

    const auto& in = ui.input();
    const bool terminal_enabled = !g_block_input && !fail_shown && !a.confirm_delete && a.prompt == Prompt::None;
    const bool over_term = terminal_enabled && r_ter.contains(in.mouse) && !ui.impl->gate_blocked_pointer();
    if (!terminal_enabled)
        a.term_menu.open = false;
    if (over_term && in.pressed[1]) {
        a.term_focus = true;
        a.file_focus = a.path_focus = false;
        a.file_menu.open = false;
        aino::detail::drop_focus(ui.impl);
        open_menu(a.term_menu, ui);
    }
    const bool menu_was_open = a.term_menu.open || a.file_menu.open;
    if (a.term_menu.open) {
        const int command = draw_action_menu(ui, a.term_menu,
            {{"复制", a.term.has_selection()}, {"粘贴", a.engine.shell_open()}, {"全选"}});
        if (command == 0)
            copy_terminal(a);
        else if (command == 1)
            paste_terminal(a);
        else if (command == 2)
            a.term.select_all();
    }

    const auto pointer_cell = [&] {
        const int first = std::max(0, a.term.history_size() - a.view_offset);
        const int row = int((in.mouse.y - r_ter.y) / tm.cell_h);
        const int column = int((in.mouse.x - r_ter.x - 10) / tm.adv);
        return std::pair<int, int>{first + std::clamp(row, 0, a.term.rows - 1), column};
    };
    if (in.pressed[0] && !menu_was_open) {
        a.term_focus = over_term;
        a.selecting = over_term;
        if (over_term) {
            const auto [line, column] = pointer_cell();
            aino::detail::drop_focus(ui.impl);
            if (in.shift() && a.term.has_selection())
                a.term.sel_extend(line, column);
            else
                a.term.sel_begin(line, column);
            ui.request_animation_frame();
        }
    }
    if (a.selecting && terminal_enabled && (in.down[0] || in.released[0])) {
        const auto [line, column] = pointer_cell();
        int l0, c0, l1, c1;
        a.term.sel_range(&l0, &c0, &l1, &c1);
        a.term.sel_extend(line, column);
        int next_l0, next_c0, next_l1, next_c1;
        a.term.sel_range(&next_l0, &next_c0, &next_l1, &next_c1);
        if (in.released[0] && next_l0 == next_l1 && next_c0 == next_c1)
            a.term.clear_selection();
        if (in.released[0] || l0 != next_l0 || c0 != next_c0 || l1 != next_l1 || c1 != next_c1)
            ui.request_animation_frame();
    }
    if (!in.down[0] || !terminal_enabled)
        a.selecting = false;
    if (in.wheel != 0 && over_term && !menu_was_open) {
        a.view_offset = std::clamp(a.view_offset + int(in.wheel * 3), 0, a.term.history_size());
        a.follow = a.view_offset == 0;
        ui.request_animation_frame();
    }
    if (terminal_enabled && a.term_focus && !menu_was_open) {
        handle_term_keys(ui, a);
        if (a.prompt != Prompt::None || in.key_pressed['A'] || in.key_pressed[aino::KEY_PAGEUP] ||
            in.key_pressed[aino::KEY_PAGEDOWN])
            ui.request_animation_frame();
    }

    const Rect footer(body.x, body.bottom() - status_h, W, status_h);
    ui.fill_rect(footer, th.card_bg);
    ui.separator({footer.x, footer.y, footer.w, 1});
    float status_y = footer.y + 6;
    for (const auto& line : status_lines) {
        ui.draw_text(line, {footer.x + 14, status_y, W - 28, 16}, has_notice && a.notice_bad ? kBad : th.label2,
                     Style().font(11));
        status_y += 16;
    }
    if (transfer.busy) {
        float text_y = status_y + 4;
        for (const auto& line : transfer_lines) {
            ui.draw_text(line, {footer.x + 14, text_y, W - 224, 16}, th.label2, Style().font(11));
            text_y += 16;
        }
        const Rect progress(footer.right() - 190, status_y + transfer_h * .5f - 3, 100, 6);
        const float fraction =
            transfer.total ? std::min(1.f, float(double(transfer.done) / double(transfer.total))) : 0;
        ui.fill_rect(progress, th.separator, 3);
        ui.fill_rect({progress.x, progress.y, progress.w * fraction, progress.h}, th.tint, 3);
        if (tool_button(ui, a.xfer_cancel ? "取消中…" : "取消",
                        {footer.right() - 76, status_y + (transfer_h - 26) * .5f, 62, 26}, false)) {
            a.uploads.clear();
            a.xfer_cancel = true;
            a.engine.request_transfer_cancel();
        }
    }
}

// =============================================================================
//  十一、每帧入口
// =============================================================================
void draw_workspace(aino::Ui& ui, App& app, const aino::Theme& th) {
    // 常驻工具栏、可收起的连接/SFTP 侧栏、独立标签栏与终端工作区。
    bool session_modal = false;
    if (app.active >= 0 && app.active < int(app.tabs.size())) {
        const auto& session = *app.tabs[size_t(app.active)];
        session_modal = session.confirm_delete || session.prompt != Prompt::None;
    }
    g_block_input = app.modal_open() || session_modal;
    draw_toolbar(ui, app, th);
    g_block_input = app.modal_open() || session_modal;
    const auto layout = workspace_layout(ui, app);
    if (app.sidebar_open)
        draw_workspace_sidebar(ui, app, th, layout.sidebar);
    g_block_input = app.modal_open() || session_modal;
    if (!app.tabs.empty() && (app.active < 0 || app.active >= int(app.tabs.size())))
        app.active = 0;
    draw_tabs(ui, app, th, layout.tabs);
    if (app.tabs.empty() || app.show_start) {
        const auto notices = wrap_text(ui, app.status, 11.f, layout.content.w - 56);
        const float note_height = app.status.empty() ? 0.f : 16 + float(notices.size()) * 16;
        draw_start(ui, app, th,
                   {layout.content.x, layout.content.y, layout.content.w, layout.content.h - note_height});
        if (!app.status.empty()) {
            const auto lines = wrap_text(ui, app.status, 11.f, layout.content.w - 56);
            const float height = 16 + float(lines.size()) * 16;
            const Rect note(layout.content.x, layout.content.bottom() - height, layout.content.w, height);
            ui.fill_rect(note, th.card_bg);
            float y = note.y + 8;
            for (const auto& line : lines) {
                ui.draw_text(line, {note.x + 28, y, note.w - 56, 16}, th.label2, Style().font(11));
                y += 16;
            }
        }
    } else if (app.active >= 0 && app.active < int(app.tabs.size())) {
        session_frame(ui, active_session(app), app, th, layout.content);
    }
    if (app.active >= 0 && app.active < int(app.tabs.size())) {
        g_block_input = false;
        draw_prompt(ui, active_session(app), th);
    }

    if (app.editor_open)
        draw_editor(ui, app, th);
    if (app.ask_open)
        draw_ask_password(ui, app, th);

    // 启动页也落一份快照：GUI 排版没法用眼睛断言，"导入了哪些连接"就靠它核对
    if (app.tabs.empty() || app.show_start) {
        if (const char* dbg = std::getenv("AINO_SSH_DEBUG_FILE")) {
            if (std::FILE* f = std::fopen(dbg, "wb")) {
                std::fprintf(f, "start=tabs=%zu show=%d editor=%d ask=%d profiles=%zu dl_dir=%s status=%s\n",
                             app.tabs.size(), int(app.show_start), int(app.editor_open), int(app.ask_open),
                             app.profiles.size(), app.download_dir.c_str(), app.status.c_str());
                std::fprintf(f, "pointer=%.1f,%.1f pressed=%d released=%d gate=%d active=%llu\n",
                             ui.input().mouse.x, ui.input().mouse.y, int(ui.input().pressed[0]),
                             int(ui.input().released[0]), int(ui.impl->gate_on),
                             static_cast<unsigned long long>(ui.impl->active));
                // 扫的是哪份 ssh config：断言"只扫当前登录用户自己的那份"就靠这行
                std::fprintf(f, "ssh_config=%s machine_wide=%d user=%s\n", ssh_config_path().c_str(),
                             int(is_machine_wide_path(ssh_config_path())), current_user().c_str());
                for (const Profile& p : app.profiles)
                    std::fprintf(f, "  prof name=%s host=%s port=%d user=%s src=%d remember=%d haspass=%d\n",
                                 p.name.c_str(), p.host.c_str(), p.port, p.user.c_str(), p.src,
                                 int(p.remember), int(!p.pass.empty()));
                std::fclose(f);
            }
        }
    }
}

#if defined(AINO_PLATFORM_WINDOWS) || defined(AINO_PLATFORM_WAYLAND)
std::shared_ptr<Session> drop_session(App& app, aino::Vec2 point) {
    if (!app.sidebar_open || app.sidebar_page != 1 || app.modal_open() ||
        app.active < 0 || app.active >= int(app.tabs.size()))
        return {};
    auto session = app.tabs[size_t(app.active)];
    if (!session->r_list.contains(point) || session->confirm_delete || session->prompt != Prompt::None ||
        session->file_menu.open || session->term_menu.open || session->sftp_path.empty() ||
        !session->engine.connected() || !session->engine.sftp_ready() || session->conn_busy() ||
        file_work_pending(*session))
        return {};
    return session;
}

void show_drop_target(App& app, const Session* target) {
    bool changed = false;
    for (const auto& session : app.tabs) {
        const bool hover = session.get() == target;
        if (session->file_drag_over != hover) {
            session->file_drag_over = hover;
            changed = true;
        }
    }
    if (changed)
        app.win->wake();
}
#endif

#if defined(AINO_PLATFORM_WINDOWS)
class ExplorerDropTarget final : public IDropTarget {
public:
    ExplorerDropTarget(HWND hwnd, App& app) : hwnd_(hwnd), app_(app) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDropTarget)
            return E_NOINTERFACE;
        *result = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --refs_;
        if (!remaining)
            delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL point, DWORD* effect) override {
        auto format = file_format();
        has_files_ = data && data->QueryGetData(&format) == S_OK;
        return DragOver(0, point, effect);
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL point, DWORD* effect) override {
        const auto session = target_at(point, effect);
        *effect = session ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        show_drop_target(app_, session.get());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        has_files_ = false;
        show_drop_target(app_, nullptr);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL point, DWORD* effect) override {
        const auto session = target_at(point, effect);
        *effect = DROPEFFECT_NONE;
        DragLeave();
        if (!session || !data)
            return S_OK;
        const auto directory = session->sftp_path;
        auto format = file_format();
        STGMEDIUM medium{};
        if (data->GetData(&format, &medium) != S_OK)
            return S_OK;
        aino::detail::ScopeExit release([&] { ReleaseStgMedium(&medium); });

        try {
            if (medium.tymed != TYMED_HGLOBAL || !medium.hGlobal)
                return S_OK;
            const auto drop = reinterpret_cast<HDROP>(medium.hGlobal);
            const UINT count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
            std::vector<std::string> files;
            for (UINT i = 0; i < std::min(count, UINT(128)); ++i) {
                const UINT length = DragQueryFileW(drop, i, nullptr, 0);
                if (!length)
                    continue;
                std::wstring path(size_t(length) + 1, L'\0');
                if (DragQueryFileW(drop, i, path.data(), UINT(path.size())) == length)
                    files.push_back(from_wide(path.c_str()));
            }
            if (drop_session(app_, client_point(point)) != session || session->sftp_path != directory)
                return S_OK;
            const size_t previous = session->uploads.size();
            queue_uploads(*session, files, directory);
            if (session->uploads.size() > previous)
                *effect = DROPEFFECT_COPY;
            if (count > 128)
                set_notice(*session, session->notice + "，单次拖入最多处理 128 项", true);
        } catch (const std::exception&) {
            set_notice(*session, "无法读取拖入的文件", true);
        }
        app_.win->wake();
        return S_OK;
    }

private:
    static FORMATETC file_format() { return {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}; }

    aino::Vec2 client_point(POINTL point) const {
        POINT client{point.x, point.y};
        ScreenToClient(hwnd_, &client);
        const float scale = app_.win->dpi_scale();
        return {client.x / scale, client.y / scale};
    }
    std::shared_ptr<Session> target_at(POINTL point, const DWORD* effect) const {
        if (!has_files_ || !(*effect & DROPEFFECT_COPY))
            return {};
        return drop_session(app_, client_point(point));
    }

    HWND hwnd_;
    App& app_;
    ULONG refs_ = 1;
    bool has_files_ = false;
};
#elif defined(AINO_PLATFORM_WAYLAND)
std::vector<std::string> local_file_uris(std::string_view payload) {
    constexpr std::string_view scheme = "file://";
    std::vector<std::string> files;
    auto hex = [](char ch) {
        if (ch >= '0' && ch <= '9')
            return ch - '0';
        if (ch >= 'a' && ch <= 'f')
            return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F')
            return ch - 'A' + 10;
        return -1;
    };
    while (!payload.empty() && files.size() < 128) {
        const size_t end = payload.find('\n');
        auto line = payload.substr(0, end);
        payload.remove_prefix(end == std::string_view::npos ? payload.size() : end + 1);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.substr(0, scheme.size()) != scheme)
            continue;
        line.remove_prefix(scheme.size());
        if (line.substr(0, 10) == "localhost/")
            line.remove_prefix(9);
        if (line.empty() || line.front() != '/')
            continue;
        std::string path;
        for (size_t i = 0; i < line.size(); ++i) {
            char ch = line[i];
            if (ch == '%') {
                const int high = i + 2 < line.size() ? hex(line[i + 1]) : -1;
                const int low = i + 2 < line.size() ? hex(line[i + 2]) : -1;
                if (high < 0 || low < 0) {
                    path.clear();
                    break;
                }
                ch = char(high * 16 + low);
                i += 2;
            }
            if (ch == '\0') {
                path.clear();
                break;
            }
            path.push_back(ch);
        }
        if (!path.empty())
            files.push_back(std::move(path));
    }
    return files;
}

#endif

void frame(aino::Ui& ui, App& app, const aino::Theme& theme) {
    app.collect_profiles();
    ui.clear(theme.window_bg);
    ui.set_animating(false);
    app.reap();
    for (auto& session : app.tabs)
        if (session)
            update_session(*session, app.win);
    draw_workspace(ui, app, theme);
}

} // namespace

// 颜色映射自检，覆盖 xterm 256 色、真彩色和暗色前景对比度。
int color_selftest() {
    int fail = 0;
    auto check = [&](const char* what, Color c, int er, int eg, int eb) {
        const bool ok = c.r == er && c.g == eg && c.b == eb;
        if (!ok)
            ++fail;
        std::printf("  %s %-42s 得到 #%02X%02X%02X 期望 #%02X%02X%02X\n", ok ? "ok  " : "FAIL", what, c.r,
                    c.g, c.b, er, eg, eb);
    };

    sshc::Terminal t;
    t.resize(40, 4);
    auto feed = [&](const char* s) {
        t.feed(s, std::strlen(s));
    };
    // 每个序列后面跟一个可见字符，再逐格取色
    feed("\x1b[38;5;196mA"       // 256 色第 196 号 = 亮红 (255,0,0)
         "\x1b[38;5;16mB"        // 256 色第 16 号 = 色立方最暗一档 (0,0,0)
         "\x1b[38;5;232mC"       // 灰阶第一档 (8,8,8)
         "\x1b[38;5;244mD"       // 灰阶中段 (128,128,128)
         "\x1b[38;2;255;128;0mE" // 真彩色橙
         "\x1b[1;30mF"           // 粗体黑 → 与黑底拉开（不然看不见）
         "\x1b[0m");
    auto fg_at = [&](int x) {
        return cell_fg_eff(t.screen_row(0)[size_t(x)]);
    };

    check("38;5;196 → 亮红", fg_at(0), 255, 0, 0);
    check("38;5;244 → 灰阶 128", fg_at(3), 128, 128, 128);
    check("38;2;255;128;0 → 真彩色", fg_at(4), 255, 128, 0);

    // 调色板换算单独验（这一层是"查表对不对"）
    check("xterm256(232) → 灰阶 8", xterm256(232), 8, 8, 8);
    check("xterm256(196) → 色立方", xterm256(196), 255, 0, 0);
    check("xterm256(21) → 纯蓝", xterm256(21), 0, 0, 255);

    // 显示色这一层是"看得见吗"：太暗的必须被对比度兜底抬起来
    auto visible = [&](const char* what, int x) {
        const Color c = fg_at(x);
        const int l1 = c.r * 299 + c.g * 587 + c.b * 114;
        const int l2 = kTermBg.r * 299 + kTermBg.g * 587 + kTermBg.b * 114;
        const int d = l1 > l2 ? l1 - l2 : l2 - l1;
        if (d < 40000) {
            ++fail;
            std::printf("  FAIL %-42s #%02X%02X%02X 与底色差 %d\n", what, c.r, c.g, c.b, d);
        } else
            std::printf("  ok   %-42s #%02X%02X%02X 与底色差 %d\n", what, c.r, c.g, c.b, d);
    };
    visible("38;5;16 纯黑 → 兜底可见", 1);
    visible("38;5;232 极暗灰 → 兜底可见", 2);
    visible("1;30 粗体黑 → 可见", 5);

    // 反显 + 暗色不该崩
    feed("\x1b[7;2mG\x1b[0m");
    (void)fg_at(6);
    std::printf("  ok   %-42s\n", "反显 + SGR2 不崩");

    std::printf("\n%s（失败 %d 项）\n", fail ? "FAIL" : "PASS", fail);
    return fail ? 1 : 0;
}

int app_main() {
    if (std::getenv("AINO_SSH_SELFTEST"))
        return color_selftest();

    aino::WindowConfig cfg;
#if !defined(AINO_PLATFORM_WINDOWS)
    cfg.icon_path = aino::executable_directory() + "aino_icon.png";
#endif
    cfg.width = 1320;
    cfg.height = 820;
    cfg.min_width = 980;
    cfg.min_height = 600;
    cfg.title = "aino · SSH";
    cfg.center = true;

    aino::Window win(cfg);
    if (!win.ok())
        return 1;
    win.set_theme(aino::Theme::dark());

    App app;
    app.win = &win;
    app.clip.win = &win;
    app.download_dir = load_download_dir();
    app.queue_profiles(true);

    // 拖拽选区要跟着指针实时更新，而库默认"同一控件内移动不重绘"
    win.set_hover_skip(false);

    // 自动化/快速验证：给了主机就直接连，跳过启动页
    if (const char* h = std::getenv("AINO_SSH_HOST")) {
        Profile p;
        p.host = h;
        p.port = std::getenv("AINO_SSH_PORT") ? std::atoi(std::getenv("AINO_SSH_PORT")) : 22;
        p.user = std::getenv("AINO_SSH_USER") ? std::getenv("AINO_SSH_USER") : "";
        p.pass = std::getenv("AINO_SSH_PASS") ? std::getenv("AINO_SSH_PASS") : "";
        p.key = std::getenv("AINO_SSH_KEY") ? std::getenv("AINO_SSH_KEY") : "";
        p.name = p.user.empty() ? p.host : (p.user + "@" + p.host);
        open_session(app, p, p.pass);
    }
    if (const char* f = std::getenv("AINO_SSH_FONT")) {
        for (auto& t : app.tabs)
            t->font = float(std::atof(f));
    }

#if defined(AINO_PLATFORM_WINDOWS)
    const auto hwnd = static_cast<aino::detail::Win32State*>(win.impl->native)->hwnd;
    const HRESULT ole = OleInitialize(nullptr);
    if (SUCCEEDED(ole)) {
        auto* target = new ExplorerDropTarget(hwnd, app);
        app.native_file_drop = RegisterDragDrop(hwnd, target) == S_OK;
        target->Release();
    }
    aino::detail::ScopeExit unregister_drop([&] {
        if (app.native_file_drop)
            RevokeDragDrop(hwnd);
        if (SUCCEEDED(ole))
            OleUninitialize();
    });
#elif defined(AINO_PLATFORM_WAYLAND)
    auto* native = static_cast<aino::detail::WaylandState*>(win.impl->native);
    app.native_file_drop = native->data_manager != nullptr;
    native->file_drag = [&app](float x, float y) {
        const auto session = drop_session(app, {x, y});
        show_drop_target(app, session.get());
        return bool(session);
    };
    native->file_drop = [&app](float x, float y) -> std::function<void(std::string)> {
        const auto session = drop_session(app, {x, y});
        if (!session)
            return {};
        const auto directory = session->sftp_path;
        return [target = std::weak_ptr<Session>(session), directory, window = app.win](std::string payload) {
            const auto current = target.lock();
            if (!current || current->retired || current->shutdown_done || current->conn_busy() ||
                !current->engine.connected())
                return;
            auto files = local_file_uris(payload);
            if (files.empty())
                set_notice(*current, "没有收到可上传的本地文件", true);
            else
                queue_uploads(*current, files, directory);
            window->wake();
        };
    };
    aino::detail::ScopeExit unregister_drop([&] {
        native->file_drag = {};
        native->file_drop = {};
    });

#endif

    win.run([&](aino::Ui& ui) {
        frame(ui, app, ui.theme());
    });

    // 顺序很重要：先停后台线程，再让 Window 析构
    app.shutdown_all();
    return 0;
}

// =============================================================================
//  入口
//
//  必须用 WINDOWS 子系统（构建脚本里 /SUBSYSTEM:WINDOWS）：否则双击或在 shell
//  里启动都会先开一个黑色控制台窗口挂在后面，就是"启动还开着个终端"。
//
//  WINDOWS 子系统的进程本身没有控制台，所以自检分支要主动 AttachConsole 到
//  父进程，把 stdout 接回去，不然 `AINO_SSH_SELFTEST=1 ./aino_ssh.exe` 什么都看不见。
// =============================================================================
#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (std::getenv("AINO_SSH_SELFTEST")) {
        // 只有在 stdout 压根没有句柄时（双击启动 / 没重定向）才挂到父控制台。
        // 判断必须用 GetFileType：从 shell 里 `> file` 或 `| tail` 启动时，
        // 继承来的句柄本身就能用；此时若 AttachConsole + freopen("CONOUT$")
        // 反而把流从管道/文件切到控制台，输出就"消失"了（文件 0 字节）。
        const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!h || GetFileType(h) == FILE_TYPE_UNKNOWN) {
            if (AttachConsole(ATTACH_PARENT_PROCESS)) {
                std::FILE* f = nullptr;
                freopen_s(&f, "CONOUT$", "w", stdout);
                freopen_s(&f, "CONOUT$", "w", stderr);
            }
        }
    }
    return app_main();
}
#else
int main() {
    return app_main();
}
#endif
