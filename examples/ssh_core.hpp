// =============================================================================
//  ssh_core.hpp —— SSH 会话 + SFTP + 终端模拟器（给 ssh_client.cpp 用）
//
//  分三层，全部平台无关（Windows / Linux / macOS 同一份代码）：
//
//    1. Engine    libssh2 的封装：TCP 连接、主机密钥校验（TOFU）、认证、
//                 PTY shell 通道、SFTP 子系统。认证完成后只有 I/O owner 线程
//                 调用 libssh2；后台业务提交有界命令，UI 只收发队列与快照。
//    2. Terminal  VT100/xterm 解析器 + 字符网格 + 回滚缓冲。不做渲染，
//                 只维护"屏幕状态"，渲染在 app 那边用 ui.draw_mono_text()。
//    3. Transfer  上传/下载的分块状态机，由后台线程推进。
//
//  为什么用 libssh2 而不是调系统 ssh.exe：
//    · 密码、密钥都在进程里做，不依赖外部程序是否存在、也不用解析 stderr；
//    · SFTP 有结构化 API（属性、错误码），文件面板才做得出来；
//    · 终端窗口大小变化可以主动通知对端（pty window-change）。
//  代价是需要链一个库：Windows 使用 mbedTLS 后端，
//  Linux 用发行版的 libssh2（OpenSSL 后端），代码零改动。
// =============================================================================
#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define SSHC_SOCK SOCKET
#  define SSHC_BADSOCK INVALID_SOCKET
#  define sshc_closesock closesocket
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <poll.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/ioctl.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  define SSHC_SOCK int
#  define SSHC_BADSOCK (-1)
#  define sshc_closesock ::close
#endif

#include <libssh2.h>
#include <libssh2_sftp.h>
#include "ssh_files.hpp"

namespace sshc {

// =============================================================================
//  0. 小工具
// =============================================================================

// libssh2 的错误码转人话。认证失败之类的场景直接编译进去，不用翻文档。
inline std::string err_text(LIBSSH2_SESSION* s, int code) {
    char* msg = nullptr;
    if (s) libssh2_session_last_error(s, &msg, nullptr, 0);
    if (msg && *msg) return fmt("%s (%d)", msg, code);
    switch (code) {
    case LIBSSH2_ERROR_SOCKET_NONE:     return "socket 错误";
    case LIBSSH2_ERROR_BANNER_RECV:     return "收不到 SSH 版本横幅（端口不对？或不是 SSH 服务）";
    case LIBSSH2_ERROR_BANNER_SEND:     return "发送版本横幅失败";
    case LIBSSH2_ERROR_KEX_FAILURE:     return "密钥交换失败（算法不被支持）";
    case LIBSSH2_ERROR_AUTHENTICATION_FAILED: return "认证失败（用户名或密码不对）";
    case LIBSSH2_ERROR_SOCKET_TIMEOUT:  return "连接超时";
    default:                            return fmt("libssh2 错误 %d", code);
    }
}

// 只清理连接标识两端的空白，绝不修改密码或私钥口令。
inline std::string trim_connection_identity(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n\v\f");
    return first == std::string::npos ? std::string()
        : value.substr(first, value.find_last_not_of(" \t\r\n\v\f") - first + 1);
}

inline bool validate_connection_identity(const std::string& host, int port,
                                         const std::string& user, std::string* error) {
    const char* message = nullptr;
    if (host.empty()) message = "主机地址不能为空";
    else if (port < 1 || port > 65535) message = "端口必须在 1 到 65535 之间";
    else {
        for (unsigned char c : host) if (c <= 32 || c == 127) { message = "主机地址包含空格或控制字符，请重新输入"; break; }
        if (!message) for (unsigned char c : user) if (c < 32 || c == 127) {
            message = "用户名包含 Tab 或其它控制字符，请重新输入";
            break;
        }
    }
    if (error) *error = message ? message : "";
    return !message;
}

// 本地 TOFU 主机记录及文件支持见 ssh_files.hpp，可不链接 libssh2 独立验证。
// =============================================================================
//  2. Engine —— SSH 会话
// =============================================================================
struct Entry {
    std::string name;
    uint64_t    size  = 0;
    int64_t     mtime = 0;      // unix 秒
    uint32_t    perm  = 0;
    bool        dir   = false;
    bool        link  = false;
};

// 失败发生在哪一层 —— UI 靠它区分"网络没通"和"密码不对"。
// 用户看到"连不上"时最想知道的就是这个区别，所以不能只给一个字符串。
enum class FailStage {
    None = 0,
    Dns,         // 域名解析不了
    Tcp,         // socket 建不起来 / 连不通（拒绝、超时、不可达）
    Handshake,   // TCP 通了，但 SSH 握手未完成；不能据此断定对端不是 SSH
    HostKey,     // 主机密钥变了（可能被劫持，也可能机器重装过）
    Auth,        // SSH 层通了，账号验证没过（密码错 / 账号被禁）
    Shell,       // 认证过了，但开不了交互 shell（禁 login / 无 PTY）
    KeyExchange, // 客户端算法配置或 SSH 密钥交换失败；保留已有阶段的枚举值
    Input,       // 本地连接信息无效，尚未访问网络或提交凭据
};

// 一个非阻塞 loopback UDP socket 同时等待网络与本地命令，避免定期唤醒线程。
class EngineWake {
public:
    ~EngineWake() { if (fd_ != SSHC_BADSOCK) sshc_closesock(fd_); }
    bool open() {
        if (fd_ != SSHC_BADSOCK) return true;
        SSHC_SOCK candidate = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (candidate == SSHC_BADSOCK) return false;
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#if defined(_WIN32)
        int length = sizeof(address);
        u_long nonblocking = 1;
        if (ioctlsocket(candidate, FIONBIO, &nonblocking) != 0) { sshc_closesock(candidate); return false; }
#else
        socklen_t length = sizeof(address);
        const int flags = fcntl(candidate, F_GETFL, 0);
        if (flags < 0 || fcntl(candidate, F_SETFL, flags | O_NONBLOCK) != 0) {
            sshc_closesock(candidate); return false;
        }
        fcntl(candidate, F_SETFD, FD_CLOEXEC);
#endif
        if (::bind(candidate, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            getsockname(candidate, reinterpret_cast<sockaddr*>(&address), &length) != 0 ||
            ::connect(candidate, reinterpret_cast<sockaddr*>(&address), length) != 0) {
            sshc_closesock(candidate); return false;
        }
        fd_.store(candidate);
        return true;
    }
    void notify() { if (fd_ != SSHC_BADSOCK) (void)::send(fd_, "w", 1, 0); }
    void consume() { char data[256]; if (fd_ != SSHC_BADSOCK) while (::recv(fd_, data, sizeof(data), 0) > 0) {} }
    SSHC_SOCK descriptor() const { return fd_; }
private:
    std::atomic<SSHC_SOCK> fd_{SSHC_BADSOCK};
};

class Engine {
public:
    using WakeFn = std::function<void()>;

    ~Engine() { disconnect(); }
    void set_wake(WakeFn fn) { wake_ = std::move(fn); }
    // 永久终止此 Engine。只 shutdown、不 close；拥有者释放前句柄不会被系统复用。
    // 不等待命令队列，关会话时可打断握手或 SFTP 网络等待。
    void request_stop() {
        std::lock_guard<std::mutex> lock(transport_mtx_);
        stop_requested_ = true;
        if (transport_ != SSHC_BADSOCK) {
#if defined(_WIN32)
            ::shutdown(transport_, SD_BOTH);
#else
            ::shutdown(transport_, SHUT_RDWR);
#endif
        }
        io_wake_.notify();
    }

    // ---- 连接 ----
    bool connect(const std::string& raw_host, int port, const std::string& raw_user,
                 const std::string& pass, const std::string& key_path,
                 std::string* err) {
        disarm(err);
        if (stop_requested_) return fail(err, "连接已取消");
        fail_stage_ = int(FailStage::Input);
        const std::string host = trim_connection_identity(raw_host);
        const std::string user = trim_connection_identity(raw_user);
        if (!validate_connection_identity(host, port, user, err)) return false;
        if (user.empty()) return fail(err, "用户名不能为空，请填写远程系统的登录用户");
        static std::once_flag once;
        std::call_once(once, [] {
#if defined(_WIN32)
            WSADATA d;
            WSAStartup(MAKEWORD(2, 2), &d);
#endif
            libssh2_init(0);
        });

        fail_stage_ = int(FailStage::None);

        // TCP
        fail_stage_ = int(FailStage::Dns);
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        const std::string port_s = std::to_string(port);
        if (getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res) != 0 || !res) {
            return fail(err, "域名解析失败：解析不到 " + host +
                             "（机器名拼错了？或需要连 VPN / 改 hosts）");
        }
        fail_stage_ = int(FailStage::Tcp);
        if (stop_requested_) { freeaddrinfo(res); return fail(err, "连接已取消"); }
        SSHC_SOCK fd = SSHC_BADSOCK;
        // 每个地址都可能以不同方式失败，只报"连不上"用户没法判断到底是
        // 网络不通、端口没开、还是被防火墙丢包。这里把最后一次的原因留住。
        int  last_so   = 0;
        bool tried_any = false;
        for (addrinfo* ai = res; ai; ai = ai->ai_next) {
            if (stop_requested_) break;
            fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (fd == SSHC_BADSOCK) continue;
            {
                std::lock_guard<std::mutex> lock(transport_mtx_);
                if (stop_requested_) { sshc_closesock(fd); fd = SSHC_BADSOCK; break; }
                transport_ = fd;
            }
            tried_any = true;
            // 非阻塞 connect + select，这样"连不上"能在 10 秒内报错，
            // 而不是让整个界面等内核的 20 秒默认超时。
            if (!set_nonblocking(fd, true)) {
                last_so = sock_last_error(); close_transport(fd); fd = SSHC_BADSOCK; continue;
            }
            int rc = ::connect(fd, ai->ai_addr, int(ai->ai_addrlen));
            if (rc != 0 && !connect_in_progress()) {
                last_so = sock_last_error();
                close_transport(fd); fd = SSHC_BADSOCK; continue;
            }
            if (rc != 0) {
                const bool ready = wait_sock(fd, true, kTcpTimeoutMs);
                // 先看 socket 上挂的错误，再判定超时。
                // 被 RST 的连接（端口没开、防火墙 reject）会立刻把错误放进
                // SO_ERROR，此时如果只信 select 的返回值，就会把"拒绝连接"
                // 误报成"超时 —— 网络不通"，用户按这个提示去查网络纯属白费功夫。
                int soerr = 0; socklen_t l = sizeof soerr;
                ::getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soerr), &l);
                if (soerr != 0) {
                    last_so = soerr;
                    close_transport(fd); fd = SSHC_BADSOCK; continue;
                }
                if (!ready) {
                    last_so = kSoTimedOut;             // 真·一直没有回应
                    close_transport(fd); fd = SSHC_BADSOCK; continue;
                }
            }
            int soerr = 0; socklen_t l = sizeof soerr;
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soerr), &l);
            if (soerr != 0) { last_so = soerr; close_transport(fd); fd = SSHC_BADSOCK; continue; }
            break;
        }
        freeaddrinfo(res);
        if (stop_requested_) {
            if (fd != SSHC_BADSOCK) close_transport(fd);
            return fail(err, "连接已取消");
        }
        if (fd == SSHC_BADSOCK)
            return fail(err, tcp_trouble_text(last_so, tried_any, host, port));
        // libssh2 的阻塞模式通过内部 select 实现超时；OS socket 必须保持非阻塞。
        set_nodelay(fd);

        // 握手
        fail_stage_ = int(FailStage::Handshake);
        kbd_pass_ = pass;    // keyboard-interactive 回调要拿到密码（经 session 的 abstract）
        LIBSSH2_SESSION* sess = libssh2_session_init_ex(nullptr, nullptr, nullptr, &kbd_pass_);
        if (!sess) { close_transport(fd); return fail(err, "libssh2_session_init 失败"); }
        libssh2_session_set_timeout(sess, 20000);
        // WinCNG 后端下 group16/18-sha512 走的是 GEX 代码路径：和 paramiko 协商
        // group16-sha512 之后它发的是 GEX_REPLY，对端直接报 MessageOrderError。
        // 优先 Curve25519/ECDH，同时保留 DH 选项；是否兼容取决于双方的实际配置。
        // 列表不会为加密后端增加算法，WinCNG 的 ECDH 仍须在依赖构建时启用。
        // libssh2 会滤掉后端不支持的算法；可用 AINO_SSH_KEX 覆盖优先级。
        {
            const char* kex = std::getenv("AINO_SSH_KEX");
            const int rc = libssh2_session_method_pref(sess, LIBSSH2_METHOD_KEX,
                kex ? kex :
                "curve25519-sha256,curve25519-sha256@libssh.org,"
                "ecdh-sha2-nistp256,ecdh-sha2-nistp384,ecdh-sha2-nistp521,"
                "diffie-hellman-group-exchange-sha256,diffie-hellman-group16-sha512,"
                "diffie-hellman-group18-sha512,diffie-hellman-group14-sha256");
            if (rc != 0) {
                const std::string m = err_text(sess, rc);
                libssh2_session_free(sess);
                close_transport(fd);
                fail_stage_ = int(FailStage::KeyExchange);
                return fail(err, "设置客户端 SSH 算法失败：" + m);
            }
        }
        if (const int rc = libssh2_session_handshake(sess, fd); rc != 0) {
            const std::string m = err_text(sess, rc);
            const char* kname = libssh2_session_methods(sess, LIBSSH2_METHOD_KEX);
            const std::string our_kex = kname ? kname : "(未协商)";
            libssh2_session_free(sess);
            close_transport(fd);
            if (rc == LIBSSH2_ERROR_KEX_FAILURE) {
                fail_stage_ = int(FailStage::KeyExchange);
                return fail(err, "SSH 密钥交换失败，请检查客户端依赖与服务器算法是否兼容。"
                                 "协商算法：" + our_kex + "；底层错误：" + m);
            }
            return fail(err, "SSH 握手失败，TCP 已连接但握手尚未完成：" + m);
        }

        // 主机密钥（TOFU）
        fail_stage_ = int(FailStage::HostKey);
        const char* hash = libssh2_hostkey_hash(sess, LIBSSH2_HOSTKEY_HASH_SHA256);
        if (!hash) {
            libssh2_session_disconnect(sess, "missing host key fingerprint");
            libssh2_session_free(sess);
            close_transport(fd);
            return fail(err, "无法取得主机密钥指纹，拒绝未经验证的连接");
        }
        {
            std::string fp = "sha256:";
            static const char* hex = "0123456789abcdef";
            for (int i = 0; i < 32; ++i) {
                const unsigned char b = static_cast<unsigned char>(hash[i]);
                fp.push_back(hex[b >> 4]);
                fp.push_back(hex[b & 15]);
            }
            fingerprint_ = fp;
            std::string herr;
            if (known_.verify(host, port, fp, &herr) < 0) {
                libssh2_session_disconnect(sess, "host key mismatch");
                libssh2_session_free(sess);
                close_transport(fd);
                return fail(err, herr);
            }
            note_ = herr;
        }

        // 指定私钥时 pass 是私钥口令；否则用于密码 / 键盘交互认证。
        fail_stage_ = int(FailStage::Auth);
        int auth = LIBSSH2_ERROR_AUTHENTICATION_FAILED;
        if (!key_path.empty()) {
            auth = libssh2_userauth_publickey_fromfile_ex(sess, user.c_str(), unsigned(user.size()),
                nullptr, key_path.c_str(), pass.empty() ? nullptr : pass.c_str());
        } else if (!pass.empty()) {
            auth = libssh2_userauth_password_ex(sess, user.c_str(), unsigned(user.size()),
                                                pass.c_str(), unsigned(pass.size()), nullptr);
            if (auth != 0) auth = userauth_keyboard(sess, user, pass);
        } else {
            // 没有密码也没有密钥：如果服务端允许 "none"（少见），这一步就过了
            auth = libssh2_userauth_authenticated(sess) ? 0 : LIBSSH2_ERROR_AUTHENTICATION_FAILED;
        }
        if (auth != 0) {
            const std::string m = err_text(sess, auth);
            libssh2_session_disconnect(sess, "auth failed");
            libssh2_session_free(sess);
            close_transport(fd);
            // 认证阶段的错误与网络错误分开报告；无凭据时提供补充提示。
            if (pass.empty() && key_path.empty())
                return fail(err, "认证失败 —— 网络和 SSH 都是通的，但没有可用的凭据："
                                 "这台服务器需要密码或密钥，补一个再连");
            return fail(err, "认证失败 —— 服务器未接受用户「" + user + "」的登录。"
                             "请核对用户名、密码及账号登录权限（服务端回：" + m + "）");
        }

        {
            std::lock_guard<std::mutex> lk(mtx_);
            sess_ = sess;
            sock_ = fd;
            connected_ = true;
            host_ = host;
            port_ = port;
            user_ = user;
        }
        try { start_io(); }
        catch (const std::exception& e) {
            disconnect();
            return fail(err, std::string("无法启动 SSH I/O 线程：") + e.what());
        }
        if (err) *err = note_;
        return true;
    }

    void disconnect() {
        // 调用者先停止命令生产者。shutdown 打断正在等待的协议，再 join 唯一执行线程。
        interrupt_transport();
        stop_io();
        LIBSSH2_SESSION* dead = nullptr;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            use_cleanup_transport();
            if (xfer_.kind != Transfer::None && !xfer_.finalized) {
                xfer_.error = "连接已关闭";
                xfer_.finished = true;
                finish_transfer();
            }
            if (sess_) libssh2_session_set_blocking(sess_, 1);
            if (sess_) libssh2_session_set_timeout(sess_, 100);
            if (sftp_) { libssh2_sftp_shutdown(sftp_); sftp_ = nullptr; }
            sftp_ready_ = false;
            if (chan_) {
                libssh2_channel_send_eof(chan_);
                libssh2_channel_close(chan_);
                libssh2_channel_free(chan_);
                chan_ = nullptr;
            }
            if (sess_) {
                libssh2_session_disconnect(sess_, "bye");
                dead = sess_;
                sess_ = nullptr;
            }
            if (sock_ != SSHC_BADSOCK) { close_transport(sock_); sock_ = SSHC_BADSOCK; }
            connected_ = false;
            shell_open_ = false;
            pty_cols_ = 0;
            pty_rows_ = 0;
            pty_request_ = 0;
            eof_ = false;
            xfer_ = Transfer{};
            write_buffer_.clear(); write_offset_ = 0;
            wire_failed_ = false; wire_error_.clear(); reader_enabled_ = false;
            transfer_cancel_ = false; cancel_deadline_ = IoClock::time_point::max();
            { std::lock_guard<std::mutex> g(rx_mtx_); rx_.clear(); }
            { std::lock_guard<std::mutex> g(tx_mtx_); tx_.clear(); queued_input_ = 0; io_error_.clear(); }
        }
        if (dead) libssh2_session_free(dead);
    }

    bool connected() const { return connected_; }
    bool shell_open() const { return shell_open_; }
    // 远端 pty 当前认定的尺寸（最后一次成功发给对端的值）。
    // UI 侧必须拿它来比对，而不是比"我自己的 term 尺寸变没变" —— 详见
    // ssh_client.cpp 里 sync_pty_size() 的注释。
    int  pty_cols() const { return pty_cols_.load(); }
    int  pty_rows() const { return pty_rows_.load(); }
    FailStage fail_stage() const { return FailStage(fail_stage_.load()); }
    bool shell_eof() const { return eof_; }
    std::string cwd_snapshot() {
        if (!on_io_thread() && io_running_) return run_io([this] { return cwd_; });
        return cwd_;
    }
    // 兼容引用接口仅限无并发目录修改的调用者；后台客户端使用 cwd_snapshot()。
    const std::string& cwd() const { return cwd_; }
    const std::string& fingerprint() const { return fingerprint_; }
    std::string who() const { return fmt("%s@%s:%d", user_.c_str(), host_.c_str(), port_); }

    // ---- 终端通道 ----
    bool open_shell(int cols, int rows, std::string* err) {
        if (!on_io_thread() && io_running_) return run_io([&] { return open_shell(cols, rows, err); });
        if (!sess_ || !connected_) return fail(err, "尚未连接");
        if (chan_) return true;
        LIBSSH2_CHANNEL* ch = await_io([&] { return libssh2_channel_open_session(sess_); });
        if (!ch) return fail(err, "打不开通道：" + err_text(sess_, libssh2_session_last_errno(sess_)));
        // 告诉远端"我们支持 24 位真彩"。
        //
        // 为什么要这一步：helix / vim / btop 这些程序并不盲发 RGB，它们看 COLORTERM
        // 决定走真彩还是把主题色近似到 256 色。只有 TERM=xterm-256color 时 helix 会
        // 把默认主题按 256 色抖动，观感就是"颜色不对、发灰"。
        // sshd 端要配 AcceptEnv 才会收（Ubuntu 常见 `AcceptEnv LANG LC_*`，
        // 有的机器额外放行 COLORTERM）；对方不收就返回错误，退回 256 色，无副作用。
        await_io([&] { return libssh2_channel_setenv(ch, "COLORTERM", "truecolor"); });
        // xterm-256color：远端程序据此决定配色与按键序列，报别的会导致 vim 之类退化
        if (await_io([&] { return libssh2_channel_request_pty_ex(ch, "xterm-256color", 14, nullptr, 0,
                                           cols, rows, 0, 0); }) != 0) {
            await_io([&] { return libssh2_channel_free(ch); }, false);
            return fail(err, "申请 PTY 失败（该账号可能禁止交互登录）");
        }
        if (await_io([&] { return libssh2_channel_shell(ch); }) != 0) {
            await_io([&] { return libssh2_channel_free(ch); }, false);
            return fail(err, "启动 shell 失败");
        }
        chan_ = ch;
        shell_open_ = true;
        eof_ = false;
        pty_cols_ = cols;
        pty_rows_ = rows;
        return true;
    }

    void resize_pty(int cols, int rows) {
        if (!on_io_thread() && io_running_) return run_io([&] { resize_pty(cols, rows); });
        if (!chan_) return;
        if (await_io([&] { return libssh2_channel_request_pty_size_ex(chan_, cols, rows, 0, 0); }, false) == 0) {
            pty_cols_ = cols;
            pty_rows_ = rows;
        }
    }

    // UI 只提交最新尺寸，网络请求由 I/O owner 执行。
    void request_pty_resize(int cols, int rows) {
        if (cols > 0 && rows > 0)
            pty_request_ = (uint64_t(uint32_t(cols)) << 32) | uint32_t(rows);
        io_wake_.notify();
    }

    // 写终端（键盘输入、粘贴、鼠标上报）。
    //
    // **不在这里直接写网络**：远端的窗口（flow control）满了以后
    // libssh2_channel_write 会阻塞，如果就地写，UI 线程会跟着卡住 —— 敲键盘没反应
    // 但鼠标还能动，是这类客户端最经典的假死。所以这里只入队，真正的写由后台
    // I/O owner 执行（start_reader 仅启用终端读取）。
    bool send(const std::string& bytes) {
        if (bytes.empty()) return true;
        {
            std::lock_guard<std::mutex> lk(tx_mtx_);
            if (!shell_open_ || eof_ || bytes.size() > kQueueLimit - queued_input_) {
                io_error_ = "终端发送队列已满或通道已关闭，本次输入未发送，请稍后重试";
                if (wake_) wake_();
                return false;
            }
            tx_ += bytes;
            queued_input_ += bytes.size();
        }
        io_wake_.notify();
        return true;
    }

    std::string take_io_error() {
        std::lock_guard<std::mutex> lk(tx_mtx_);
        std::string error; error.swap(io_error_); return error;
    }

private:
    bool write_pending() {
        // EAGAIN 后保持地址和长度，满足 libssh2 的写入重试契约。每轮只发一块，不持锁睡眠。
        if (write_buffer_.empty()) {
            std::lock_guard<std::mutex> lk(tx_mtx_);
            write_buffer_.swap(tx_); write_offset_ = 0;
        }
        if (write_buffer_.empty()) return false;
        if (!chan_ || eof_) return false;
        const size_t count = std::min<size_t>(16384, write_buffer_.size() - write_offset_);
        const ssize_t n = await_io([&] { return libssh2_channel_write_ex(chan_, 0, write_buffer_.data() + write_offset_, count); }, false, true);
        if (n == LIBSSH2_ERROR_EAGAIN || n == 0) {
            return false;
        }
        if (n < 0) {
            eof_ = true;
            std::lock_guard<std::mutex> queue(tx_mtx_);
            io_error_ = "终端写入失败：" + err_text(sess_, int(n));
            if (wake_) wake_();
            return false;
        }
        write_offset_ += size_t(n);
        { std::lock_guard<std::mutex> queue(tx_mtx_); queued_input_ -= size_t(n); }
        if (write_offset_ == write_buffer_.size()) { write_buffer_.clear(); write_offset_ = 0; }
        return true;
    }

public:
    // 限制单帧解析工作量；满队列停止读通道，让 SSH/TCP 的窗口提供无损背压。
    std::string take_output(size_t budget = 256u << 10) {
        std::string out;
        bool remaining = false;
        {
            std::lock_guard<std::mutex> lk(rx_mtx_);
            if (rx_.size() <= budget) out.swap(rx_);
            else { out = rx_.substr(0, budget); rx_.erase(0, budget); remaining = true; }
        }
        if (!out.empty()) io_wake_.notify();
        if (remaining && wake_) wake_();
        return out;
    }
    bool has_output() const {
        std::lock_guard<std::mutex> lk(rx_mtx_);
        return !rx_.empty();
    }
    size_t pending_bytes() const {
        std::lock_guard<std::mutex> lk(rx_mtx_);
        return rx_.size();
    }

    void start_reader() { reader_enabled_ = true; io_wake_.notify(); }
    void stop_reader() { reader_enabled_ = false; io_wake_.notify(); }

    // UI 可以立即通知正在等待的传输；只停止新数据，已发出的请求必须先收尾。
    void request_transfer_cancel() { transfer_cancel_ = true; io_wake_.notify(); }

    // ---- SFTP ----
    bool sftp_ready() const { return sftp_ready_; }

    bool open_sftp(std::string* err) {
        if (!on_io_thread() && io_running_) return run_io([&] { return open_sftp(err); });
        if (!sess_ || !connected_) return fail(err, "尚未连接");
        if (sftp_) return true;
        sftp_ = await_io([&] { return libssh2_sftp_init(sess_); });
        if (!sftp_) return fail(err, "SFTP 不可用（服务端未开子系统？）：" +
                                     err_text(sess_, libssh2_session_last_errno(sess_)));
        sftp_ready_ = true;
        return true;
    }

    // 绝对路径归一化：SFTP 的路径是服务端语义，不能拿本地规则乱拼。
    std::string resolve(const std::string& path) const {
        if (path.empty()) return "/";
        if (path[0] == '/') return normalize(path);
        return normalize((cwd_.empty() ? "/" : cwd_) + "/" + path);
    }

    bool list(const std::string& path, std::vector<Entry>* out, std::string* err) {
        if (!on_io_thread() && io_running_) return run_io([&] { return list(path, out, err); });
        if (!sess_ || !connected_) return fail(err, "尚未连接");
        if (!sftp_ && !init_sftp(err)) return false;
        const std::string real = resolve(path);
        LIBSSH2_SFTP_HANDLE* h = await_io([&] { return libssh2_sftp_opendir(sftp_, real.c_str()); });
        if (!h) return fail(err, fmt("打不开目录 %s（%s）", real.c_str(),
                                     sftp_error().c_str()));
        std::vector<Entry> entries;
        int read_error = 0;
        for (;;) {
            char name[1024];
            LIBSSH2_SFTP_ATTRIBUTES attrs{};
            const int n = await_io([&] { return libssh2_sftp_readdir_ex(h, name, sizeof name, nullptr, 0, &attrs); });
            if (n <= 0) { read_error = n; break; }
            Entry e;
            e.name.assign(name, size_t(n));
            if (e.name == "." || e.name == "..") continue;
            e.size  = attrs.filesize;
            e.mtime = int64_t(attrs.mtime);
            e.perm  = attrs.permissions;
            e.dir   = (attrs.permissions & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFDIR;
            e.link  = (attrs.permissions & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFLNK;
            entries.push_back(std::move(e));
        }
        const std::string read_message = read_error < 0 ? sftp_error() : std::string();
        const int close_error = await_io([&] { return libssh2_sftp_closedir(h); });
        if (read_error < 0) return fail(err, "读取目录失败：" + read_message);
        if (close_error < 0) return fail(err, "关闭目录失败：" + sftp_error());
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            if (a.dir != b.dir) return a.dir;                  // 目录在前
            return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
                [](unsigned char x, unsigned char y) { return std::tolower(x) < std::tolower(y); });
        });
        *out = std::move(entries);
        cwd_ = real;
        return true;
    }

    // 服务端当前目录（登录后问一次，作为文件面板的起点）
    bool fetch_home(std::string* err) {
        if (!on_io_thread() && io_running_) return run_io([&] { return fetch_home(err); });
        if (!sess_ || !connected_) return fail(err, "尚未连接");
        if (!sftp_ && !init_sftp(err)) return false;
        char buf[1024];
        const int n = await_io([&] { return libssh2_sftp_realpath(sftp_, ".", buf, sizeof buf); });
        if (n > 0) { cwd_ = normalize(std::string(buf, size_t(n))); return true; }
        return fail(err, "无法读取远端主目录：" + sftp_error());
    }

    bool mkdir(const std::string& p, std::string* err) {
        if (!on_io_thread() && io_running_) return run_io([&] { return mkdir(p, err); });
        if (!sftp_ || !connected_) return fail(err, "SFTP 未就绪或连接已关闭");
        const std::string r = resolve(p);
        if (await_io([&] { return libssh2_sftp_mkdir(sftp_, r.c_str(), 0755); }) != 0)
            return fail(err, "新建目录失败：" + sftp_error());
        return true;
    }

    bool rmdir(const std::string& p, std::string* err) {
        if (!on_io_thread() && io_running_) return run_io([&] { return rmdir(p, err); });
        if (!sftp_ || !connected_) return fail(err, "SFTP 未就绪或连接已关闭");
        const std::string r = resolve(p);
        if (await_io([&] { return libssh2_sftp_rmdir(sftp_, r.c_str()); }) != 0)
            return fail(err, "删除目录失败（非空？）：" + sftp_error());
        return true;
    }

    bool unlink(const std::string& p, std::string* err) {
        if (!on_io_thread() && io_running_) return run_io([&] { return unlink(p, err); });
        if (!sftp_ || !connected_) return fail(err, "SFTP 未就绪或连接已关闭");
        const std::string r = resolve(p);
        if (await_io([&] { return libssh2_sftp_unlink(sftp_, r.c_str()); }) != 0)
            return fail(err, "删除文件失败：" + sftp_error());
        return true;
    }

    // 不管它是什么，删掉就行（UI 上"删除"按钮只有一条路径）。
    // 用 lstat 看条目本身：目录走 rmdir，目录符号链接仍走 unlink。
    bool remove(const std::string& p, std::string* err) {
        if (!on_io_thread() && io_running_) return run_io([&] { return remove(p, err); });
        bool is_dir = false;
        {
            if (!sftp_ || !connected_) return fail(err, "SFTP 未就绪或连接已关闭");
            const std::string r = resolve(p);
            LIBSSH2_SFTP_ATTRIBUTES at{};
            if (await_io([&] { return libssh2_sftp_lstat(sftp_, r.c_str(), &at); }) != 0)
                return fail(err, "找不到：" + r);
            is_dir = (at.permissions & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFDIR;
        }
        return is_dir ? rmdir(p, err) : unlink(p, err);
    }

    bool rename(const std::string& from, const std::string& to, std::string* err) {
        if (!on_io_thread() && io_running_) return run_io([&] { return rename(from, to, err); });
        if (!sftp_ || !connected_) return fail(err, "SFTP 未就绪或连接已关闭");
        const std::string a = resolve(from), b = resolve(to);
        // 目标已存在时 SFTP 默认覆盖；这里先探一下，避免误删——重命名不该有破坏性
        LIBSSH2_SFTP_ATTRIBUTES at{};
        const int inspected = await_io([&] { return libssh2_sftp_lstat(sftp_, b.c_str(), &at); });
        if (inspected == 0)
            return fail(err, "目标已存在：" + b);
        if (!sftp_missing(inspected))
            return fail(err, "无法检查重命名目标：" + sftp_error());
        // 检查和提交间目标仍可能出现；明确不请求覆盖，由服务端原子判定冲突。
        if (await_io([&] { return libssh2_sftp_rename_ex(sftp_, a.c_str(), unsigned(a.size()),
                                 b.c_str(), unsigned(b.size()), 0); }) != 0)
            return fail(err, "重命名失败：" + sftp_error());
        return true;
    }

    // ---- 分块传输（业务后台分块提交，I/O owner 执行） ----
    struct Transfer {
        enum Kind { None, Download, Upload };
        Kind kind = None;
        std::string remote, local, name;
        uint64_t done = 0, total = 0;
        std::string error;
        bool finished = false;
        // 内部状态
        LIBSSH2_SFTP_HANDLE* rh = nullptr;
        std::FILE* fh = nullptr;
        bool dir_mode = false;
        bool total_known = false;
        bool finalized = false;
        std::string temporary;
        LIBSSH2_SFTP_ATTRIBUTES destination_attributes{};
    };

    void start_download(const std::string& remote, const std::string& local, bool is_dir, const std::atomic<bool>* cancel = nullptr) {
        if (!on_io_thread() && io_running_) return run_io([&] { start_download(remote, local, is_dir, cancel); });
        TransferOperation operation(*this, cancel);
        if (xfer_.kind != Transfer::None) return;
        cancel_deadline_ = IoClock::time_point::max();
        xfer_ = Transfer{};
        xfer_.kind = Transfer::Download;
        xfer_.remote = remote;
        xfer_.local = local;
        xfer_.name = base_name(remote);
        xfer_.dir_mode = is_dir;
        if (!sftp_) { xfer_.error = "SFTP 尚未就绪"; xfer_.finished = true; finish_transfer(); return; }
        // 目录下载：把"服务器上的目录"映射到本地同名目录（不做递归 —— 递归很容易
        // 一层层卡住界面，v1 明确只搬单文件/整个目录用打包，见 README 的说明）
        if (!is_dir) {
            xfer_.rh = await_io([&] { return libssh2_sftp_open(sftp_, remote.c_str(), LIBSSH2_FXF_READ, 0); });
            if (!xfer_.rh) { xfer_.error = "打不开远端文件：" + sftp_error(); xfer_.finished = true; }
            LIBSSH2_SFTP_ATTRIBUTES at{};
            if (xfer_.rh && await_io([&] { return libssh2_sftp_fstat(xfer_.rh, &at); }) == 0 && (at.flags & LIBSSH2_SFTP_ATTR_SIZE)) {
                xfer_.total = at.filesize;
                xfer_.total_known = true;
            }
            if (xfer_.rh) {
                std::error_code path_error;
                const auto destination = std::filesystem::u8path(local);
                const auto status = std::filesystem::symlink_status(destination, path_error);
                if (path_error && path_error != std::errc::no_such_file_or_directory) {
                    xfer_.error = "无法检查下载目标"; xfer_.finished = true; finish_transfer(); return;
                }
                if (std::filesystem::is_symlink(status)) {
                    const auto resolved = std::filesystem::canonical(destination, path_error);
                    if (path_error) { xfer_.error = "无法解析下载目标符号链接"; xfer_.finished = true; finish_transfer(); return; }
                    xfer_.local = resolved.u8string();
                }
                xfer_.fh = create_transfer_file(xfer_.local, &xfer_.temporary);
                if (!xfer_.fh) { xfer_.error = "本地写不了：" + local; xfer_.finished = true; }
            }
        } else {
            xfer_.error = "暂不支持整目录下载";
            xfer_.finished = true;
        }
        if (xfer_.finished) finish_transfer();
    }

    void start_upload(const std::string& local, const std::string& remote, const std::atomic<bool>* cancel = nullptr) {
        if (!on_io_thread() && io_running_) return run_io([&] { start_upload(local, remote, cancel); });
        TransferOperation operation(*this, cancel);
        if (xfer_.kind != Transfer::None) return;
        cancel_deadline_ = IoClock::time_point::max();
        xfer_ = Transfer{};
        xfer_.kind = Transfer::Upload;
        xfer_.local = local;
        xfer_.remote = remote;
        xfer_.name = base_name(local);
        if (!sftp_) { xfer_.error = "SFTP 尚未就绪"; xfer_.finished = true; finish_transfer(); return; }
        xfer_.fh = open_upload_file(local);
        if (!xfer_.fh) { xfer_.error = "本地读不了：" + local; xfer_.finished = true; return; }
        if (!local_file_size(xfer_.fh, &xfer_.total, &xfer_.error)) {
            xfer_.finished = true;
            finish_transfer();
            return;
        }
        xfer_.total_known = true;
        LIBSSH2_SFTP_ATTRIBUTES attributes{};
        const int inspected = await_io([&] { return libssh2_sftp_lstat(sftp_, remote.c_str(), &attributes); });
        if (inspected == 0 && (attributes.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS)
            && (attributes.permissions & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFLNK) {
            char resolved[32768];
            const int count = await_io([&] { return libssh2_sftp_realpath(sftp_, remote.c_str(), resolved, sizeof(resolved)); });
            if (count <= 0) {
                xfer_.error = "无法解析上传目标符号链接"; xfer_.finished = true; finish_transfer(); return;
            }
            xfer_.remote.assign(resolved, size_t(count));
        } else if (inspected != 0 && !sftp_missing(inspected)) {
            xfer_.error = "无法检查上传目标：" + sftp_error(); xfer_.finished = true; finish_transfer(); return;
        }
        const int target_status = await_io([&] { return libssh2_sftp_stat(sftp_, xfer_.remote.c_str(), &attributes); });
        if (target_status == 0) {
            xfer_.destination_attributes = attributes;
            xfer_.destination_attributes.flags &= LIBSSH2_SFTP_ATTR_UIDGID | LIBSSH2_SFTP_ATTR_PERMISSIONS;
        } else if (!sftp_missing(target_status)) {
            xfer_.error = "无法读取目标属性：" + sftp_error(); xfer_.finished = true; finish_transfer(); return;
        } else {
            xfer_.destination_attributes.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
            // 新文件保持私有；不能在提交前 chmod(0644) 绕过服务端的私有 umask。
            xfer_.destination_attributes.permissions = 0600;
        }
        static std::atomic<unsigned long> next{0};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 16; ++attempt) {
            xfer_.temporary = xfer_.remote + ".aino-part." + std::to_string(tick) + "." + std::to_string(next++);
            xfer_.rh = await_io([&] { return libssh2_sftp_open(sftp_, xfer_.temporary.c_str(),
                LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_EXCL, 0600); });
            if (xfer_.rh || libssh2_session_last_errno(sess_) != LIBSSH2_ERROR_SFTP_PROTOCOL
                || libssh2_sftp_last_error(sftp_) != LIBSSH2_FX_FILE_ALREADY_EXISTS) break;
        }
        if (!xfer_.rh) { xfer_.error = "打不开远端目标：" + sftp_error(); xfer_.finished = true; }
        if (xfer_.finished) { xfer_.temporary.clear(); finish_transfer(); }
    }

    // 兼容旧调用：只允许同一传输生产者在同步命令返回后读取；UI 使用 Session 快照。
    Transfer& transfer() { return xfer_; }
    bool transfer_busy() const { return xfer_.kind != Transfer::None && !xfer_.finished; }

    // 由传输线程分块推进；返回 true 表示本次搬运了字节。
    bool xfer_step(uint64_t budget = 16 * 1024, const std::atomic<bool>* cancel = nullptr) {
        if (!on_io_thread() && io_running_) return run_io([&] { return xfer_step(budget, cancel); });
        TransferOperation operation(*this, cancel);
        if (xfer_.kind == Transfer::None || xfer_.finished) return false;
        budget = std::min<uint64_t>(budget, 16u << 10);
        static thread_local std::vector<char> buf;
        if (buf.size() < budget) buf.resize(size_t(budget));
        uint64_t moved = 0;
        auto cancelled = [&] {
            if (!wire_failed_ && !transfer_cancel_ && (!cancel || !cancel->load())) return false;
            xfer_.error = wire_failed_ ? wire_error_ : "已取消";
            xfer_.finished = true; return true;
        };
        while (moved < budget && !xfer_.finished && !cancelled()) {
            const size_t want = size_t(std::min<uint64_t>(budget - moved, buf.size()));
            if (xfer_.kind == Transfer::Download) {
                const ssize_t n = await_io([&] { return libssh2_sftp_read(xfer_.rh, buf.data(), want); });
                if (n == LIBSSH2_ERROR_EAGAIN) { std::this_thread::yield(); continue; }
                if (n < 0) { xfer_.error = "读取失败：" + sftp_error(); xfer_.finished = true; break; }
                if (n == 0) { xfer_.finished = true; break; }
                const size_t written = xfer_.fh ? std::fwrite(buf.data(), 1, size_t(n), xfer_.fh) : 0;
                xfer_.done += written;
                moved += written;
                if (written != size_t(n)) {
                    xfer_.error = "本地写入失败：" + xfer_.local;
                    xfer_.finished = true;
                }
            } else {
                if (!xfer_.fh) { xfer_.finished = true; break; }
                const size_t got = std::fread(buf.data(), 1, want, xfer_.fh);
                if (got == 0) {
                    if (std::ferror(xfer_.fh)) xfer_.error = "本地读取失败：" + xfer_.local;
                    xfer_.finished = true;
                    break;
                }
                size_t off = 0;
                while (off < got && !cancelled()) {
                    const ssize_t n = await_io([&] { return libssh2_sftp_write(xfer_.rh, buf.data() + off, got - off); });
                    if (n == LIBSSH2_ERROR_EAGAIN) { std::this_thread::yield(); continue; }
                    if (n <= 0) { xfer_.error = "写入失败：" + sftp_error(); xfer_.finished = true; break; }
                    off += size_t(n);
                }
                xfer_.done += off;
                moved += off;
            }
        }
        if (xfer_.finished) finish_transfer();
        return moved > 0;
    }

    void cancel_transfer() {
        if (!on_io_thread() && io_running_) return run_io([&] { cancel_transfer(); });
        TransferOperation operation(*this, nullptr);
        if (xfer_.kind == Transfer::None || xfer_.finalized) return;
        xfer_.error = wire_failed_ ? wire_error_ : "已取消";
        xfer_.finished = true;
        finish_transfer();
    }

    void clear_transfer() {
        if (!on_io_thread() && io_running_) return run_io([&] { clear_transfer(); });
        TransferOperation operation(*this, nullptr);
        if (xfer_.kind != Transfer::None && xfer_.finished) {
            finish_transfer();
            xfer_.kind = Transfer::None;
            xfer_.done = xfer_.total = 0;
            xfer_.error.clear();
            xfer_.name.clear();
            xfer_.remote.clear();
            xfer_.local.clear();
            transfer_cancel_ = false;
        }
    }

    // 关闭一条会话前要清干净（析构里也会走一遍）
    static std::string base_name(const std::string& p) {
        const size_t i = p.find_last_of("/\\");
        return i == std::string::npos ? p : p.substr(i + 1);
    }

    static std::string normalize(const std::string& in) {
        std::vector<std::string> parts;
        size_t i = 0;
        while (i <= in.size()) {
            size_t j = in.find('/', i);
            if (j == std::string::npos) j = in.size();
            const std::string seg = in.substr(i, j - i);
            if (seg.empty() || seg == ".") { /* 跳过 */ }
            else if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
            else parts.push_back(seg);
            i = j + 1;
        }
        std::string out = "/";
        for (size_t k = 0; k < parts.size(); ++k) {
            out += parts[k];
            if (k + 1 < parts.size()) out += "/";
        }
        return out;
    }

private:
    static Engine*& current_io_engine() { static thread_local Engine* owner = nullptr; return owner; }
    bool on_io_thread() const { return current_io_engine() == this; }

    template<class Fn> auto run_io(Fn fn) -> decltype(fn()) {
        using Result = decltype(fn());
        auto task = std::make_shared<std::packaged_task<Result()>>([this, fn = std::move(fn)]() mutable -> Result {
            try { return fn(); }
            catch (...) {
                // 异常可能打断协议阶段；关闭传输后才能安全处理后续的清理命令。
                fail_transport("SSH 命令执行失败，已断开连接");
                throw;
            }
        });
        auto result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(command_mtx_);
            if (io_stopping_ || commands_.size() >= 64) {
                // 客户端只有连接、目录和传输三个生产者；上限避免未来调用者无界排队。
                throw std::runtime_error(io_stopping_ ? "SSH I/O 正在关闭" : "SSH 命令队列已满");
            }
            commands_.push_back([task] { (*task)(); });
        }
        io_wake_.notify();
        return result.get();
    }

    struct TransferOperation {
        Engine& owner;
        bool previous;
        const std::atomic<bool>* token;
        TransferOperation(Engine& engine, const std::atomic<bool>* cancel)
            : owner(engine), previous(engine.transfer_operation_), token(engine.transfer_token_) {
            owner.transfer_operation_ = true;
            if (cancel) owner.transfer_token_ = cancel;
        }
        ~TransferOperation() { owner.transfer_operation_ = previous; owner.transfer_token_ = token; }
    };

    void interrupt_transport() {
        std::lock_guard<std::mutex> lock(transport_mtx_);
        if (transport_ == SSHC_BADSOCK) return;
#if defined(_WIN32)
        ::shutdown(transport_, SD_BOTH);
#else
        ::shutdown(transport_, SHUT_RDWR);
#endif
        io_wake_.notify();
    }

    void fail_transport(const char* message) {
        if (wire_failed_) return;
        wire_failed_ = true;
        wire_error_ = message;
        connected_ = false; sftp_ready_ = false; eof_ = true;
        interrupt_transport();
        use_cleanup_transport();
        {
            std::lock_guard<std::mutex> lock(tx_mtx_);
            io_error_ = message;
        }
        if (wake_) wake_();
    }

    // 仅用于已经 shutdown 的会话：丢弃待发半包、令读取立即失败，完成库内部的
    // EAGAIN 状态收尾。绝不重新访问网络；避免旧半包阻止 CLOSE/free 释放句柄。
    void use_cleanup_transport() {
        if (!sess_) return;
        const auto send = +[](libssh2_socket_t, const void*, size_t length, int, void**) -> ssize_t {
            return ssize_t(length);
        };
        const auto recv = +[](libssh2_socket_t, void*, size_t, int, void**) -> ssize_t {
            return -ECONNRESET;
        };
#if LIBSSH2_VERSION_NUM >= 0x010B01
        libssh2_session_callback_set2(sess_, LIBSSH2_CALLBACK_SEND, reinterpret_cast<libssh2_cb_generic*>(send));
        libssh2_session_callback_set2(sess_, LIBSSH2_CALLBACK_RECV, reinterpret_cast<libssh2_cb_generic*>(recv));
#else
        libssh2_session_callback_set(sess_, LIBSSH2_CALLBACK_SEND, reinterpret_cast<void*>(send));
        libssh2_session_callback_set(sess_, LIBSSH2_CALLBACK_RECV, reinterpret_cast<void*>(recv));
#endif
    }

    void wait_transport(int directions, int timeout_ms, bool network = true) {
        const auto signal = io_wake_.descriptor();
        if (signal == SSHC_BADSOCK && (timeout_ms < 0 || timeout_ms > 50)) timeout_ms = 50;
#if defined(_WIN32)
        fd_set reads, writes; FD_ZERO(&reads); FD_ZERO(&writes);
        if (signal != SSHC_BADSOCK) FD_SET(signal, &reads);
        if (network && sock_ != SSHC_BADSOCK) {
            if (directions & LIBSSH2_SESSION_BLOCK_INBOUND) FD_SET(sock_, &reads);
            if (directions & LIBSSH2_SESSION_BLOCK_OUTBOUND) FD_SET(sock_, &writes);
        }
        timeval timeout{timeout_ms < 0 ? 0 : timeout_ms / 1000, timeout_ms < 0 ? 0 : (timeout_ms % 1000) * 1000};
        const int rc = ::select(0, &reads, &writes, nullptr, timeout_ms < 0 ? nullptr : &timeout);
        if (rc == SOCKET_ERROR && WSAGetLastError() != WSAEINTR) fail_transport("SSH socket 等待失败");
#else
        pollfd descriptors[2] = {{signal, POLLIN, 0}, {network ? sock_ : SSHC_BADSOCK,
            short(((directions & LIBSSH2_SESSION_BLOCK_INBOUND) ? POLLIN : 0)
                | ((directions & LIBSSH2_SESSION_BLOCK_OUTBOUND) ? POLLOUT : 0)), 0}};
        const int rc = ::poll(descriptors, 2, timeout_ms);
        if ((rc < 0 && errno != EINTR) || (descriptors[1].revents & POLLNVAL))
            fail_transport("SSH socket 等待失败");
#endif
        io_wake_.consume();
    }

    // 参数和栈上缓冲在重试期间保持存活。单 owner 调用 libssh2，等待时不持应用锁。
    // 仅在没有待发 transport packet 时服务终端；不能在 OUTBOUND 的半包中插入另一条写入。
    template<class Fn> auto await_io(Fn fn, bool service_terminal = true, bool zero_waits = false,
                                     bool idle_read = false) -> decltype(fn()) {
        using Result = decltype(fn());
        const auto previous = operation_deadline_;
        operation_deadline_ = std::min(previous, IoClock::now() + std::chrono::seconds(20));
        struct Restore {
            Engine& owner; IoClock::time_point previous;
            ~Restore() { owner.operation_deadline_ = previous; }
        } restore{*this, previous};
        auto failure = []() -> Result {
            if constexpr (std::is_pointer_v<Result>) return nullptr;
            else return Result(LIBSSH2_ERROR_SOCKET_DISCONNECT);
        };
        for (;;) {
            const Result result = fn();
            bool pending;
            if constexpr (std::is_pointer_v<Result>)
                pending = !result && sess_ && libssh2_session_last_errno(sess_) == LIBSSH2_ERROR_EAGAIN;
            else pending = result == LIBSSH2_ERROR_EAGAIN || (zero_waits && result == 0);
            if (!pending) {
                int code;
                if constexpr (std::is_pointer_v<Result>) code = result ? 0 : libssh2_session_last_errno(sess_);
                else code = int(result);
                if (!wire_failed_ && (code == LIBSSH2_ERROR_SOCKET_SEND || code == LIBSSH2_ERROR_SOCKET_RECV
                    || code == LIBSSH2_ERROR_SOCKET_DISCONNECT || code == LIBSSH2_ERROR_SOCKET_TIMEOUT))
                    fail_transport("SSH 网络连接已中断");
                return result;
            }
            if (wire_failed_) return failure();
            auto deadline = operation_deadline_;
            // UI 取消也覆盖排在传输前面的目录请求，不能再等待它的完整 20 秒。
            const bool cancelling = transfer_cancel_
                || (transfer_operation_ && transfer_token_ && transfer_token_->load());
            if (cancelling) {
                if (cancel_deadline_ == IoClock::time_point::max())
                    cancel_deadline_ = IoClock::now() + std::chrono::seconds(2);
                deadline = std::min(deadline, cancel_deadline_);
            }
            if (stop_requested_ || io_stopping_ || IoClock::now() >= deadline) {
                fail_transport(stop_requested_ || io_stopping_ ? "会话已关闭"
                    : cancelling && cancel_deadline_ <= IoClock::now()
                        ? "取消收尾超时，已断开连接；远端临时文件可能需要清理"
                        : "SSH 请求超时，已断开连接以释放未完成的协议状态");
                // 网络已关闭；cleanup callbacks 完成原半包并让响应等待立即失败。
                const Result stopped = fn();
                if constexpr (std::is_pointer_v<Result>) return stopped;
                else return stopped == LIBSSH2_ERROR_EAGAIN ? failure() : stopped;
            }
            int directions = libssh2_session_block_directions(sess_);
            if (!directions) directions = LIBSSH2_SESSION_BLOCK_INBOUND;
            if (idle_read && !(directions & LIBSSH2_SESSION_BLOCK_OUTBOUND)) return result;
            if (service_terminal && !(directions & LIBSSH2_SESSION_BLOCK_OUTBOUND)
                && pump_terminal(true)) continue; // 终端读取也可能把当前 SFTP 回复读入 libssh2。
            const auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - IoClock::now()).count();
            // 兼容旧调用者直接写 cancel token；UI 入口另会主动 wake，无常态轮询。
            const int delay = int(std::max<int64_t>(0, std::min<int64_t>(left, transfer_token_ ? 50 : 20000)));
            wait_transport(directions, delay, !wire_failed_);
        }
    }

    bool pump_terminal(bool buffered_only = false) {
        if (!reader_enabled_ || !chan_ || wire_failed_ || eof_) return false;
        bool changed = false;
        if (const auto size = pty_request_.exchange(0)) resize_pty(int(size >> 32), int(uint32_t(size)));
        // write_pending 完整恢复其半包后才返回，保证下一命令能安全使用同一 transport。
        changed |= write_pending();
        size_t budget = 256u << 10;
        for (int stream : {0, SSH_EXTENDED_DATA_STDERR}) {
            while (budget && !wire_failed_) {
                size_t available;
                { std::lock_guard<std::mutex> lock(rx_mtx_); available = kQueueLimit - rx_.size(); }
                if (!available || (buffered_only && libssh2_poll_channel_read(chan_, stream) <= 0)) break;
                char buffer[16384];
                const auto count = std::min({sizeof(buffer), available, budget});
                ssize_t n = libssh2_channel_read_ex(chan_, stream, buffer, count);
                if (n == LIBSSH2_ERROR_EAGAIN && (libssh2_session_block_directions(sess_) & LIBSSH2_SESSION_BLOCK_OUTBOUND))
                    n = await_io([&] { return libssh2_channel_read_ex(chan_, stream, buffer, count); }, false, false, true);
                if (n > 0) {
                    std::lock_guard<std::mutex> lock(rx_mtx_);
                    rx_.append(buffer, size_t(n)); budget -= size_t(n); changed = true;
                } else {
                    if (n != LIBSSH2_ERROR_EAGAIN && (n < 0 || libssh2_channel_eof(chan_))) {
                        eof_ = true; changed = true;
                    }
                    break;
                }
            }
        }
        if (changed && wake_) wake_();
        return changed;
    }

    void start_io() {
        libssh2_session_set_blocking(sess_, 0);
        io_wake_.open(); io_stopping_ = false; io_running_ = true;
        try { io_thread_ = std::thread([this] {
            current_io_engine() = this;
            for (;;) {
                std::function<void()> command;
                {
                    std::lock_guard<std::mutex> lock(command_mtx_);
                    if (!commands_.empty()) { command = std::move(commands_.front()); commands_.pop_front(); }
                }
                if (command) command(); // packaged_task 将异常交回等待者，不能让 future 悬挂。
                else if (io_stopping_) break;
                bool progress = false;
                try { progress = pump_terminal(); }
                catch (const std::exception&) { fail_transport("SSH I/O 执行失败，已断开连接"); }
                {
                    std::lock_guard<std::mutex> lock(command_mtx_);
                    if (!commands_.empty() || io_stopping_) continue;
                }
                if (progress) continue;
                bool full;
                { std::lock_guard<std::mutex> lock(rx_mtx_); full = rx_.size() == kQueueLimit; }
                wait_transport(LIBSSH2_SESSION_BLOCK_INBOUND, -1,
                    reader_enabled_ && !wire_failed_ && !eof_ && !full);
            }
            current_io_engine() = nullptr; io_running_ = false;
        }); } catch (...) { io_running_ = false; io_stopping_ = true; throw; }
    }

    void stop_io() {
        { std::lock_guard<std::mutex> lock(command_mtx_); io_stopping_ = true; }
        io_wake_.notify();
        if (io_thread_.joinable()) io_thread_.join();
    }

    std::mutex transport_mtx_;
    SSHC_SOCK transport_ = SSHC_BADSOCK;
    std::atomic<bool> stop_requested_{false};
    void close_transport(SSHC_SOCK descriptor) {
        std::lock_guard<std::mutex> lock(transport_mtx_);
        if (transport_ == descriptor) transport_ = SSHC_BADSOCK;
        sshc_closesock(descriptor);
    }
    LIBSSH2_SESSION* sess_ = nullptr;
    LIBSSH2_CHANNEL* chan_ = nullptr;
    LIBSSH2_SFTP*    sftp_ = nullptr;
    SSHC_SOCK        sock_ = SSHC_BADSOCK;
    EngineWake io_wake_;

    static constexpr size_t kQueueLimit = 4u << 20;
    std::string write_buffer_, io_error_;
    size_t write_offset_ = 0, queued_input_ = 0;
    mutable std::mutex mtx_;
    mutable std::mutex rx_mtx_;
    mutable std::mutex tx_mtx_;
    std::string      rx_;
    std::string      tx_;          // 待发送（键盘输入）
    std::thread io_thread_;
    std::atomic<bool> io_running_{false}, io_stopping_{false}, reader_enabled_{false};
    std::atomic<bool> sftp_ready_{false}, transfer_cancel_{false};
    std::mutex command_mtx_;
    std::deque<std::function<void()>> commands_;
    bool wire_failed_ = false;
    std::string wire_error_;
    using IoClock = std::chrono::steady_clock;
    IoClock::time_point operation_deadline_ = IoClock::time_point::max();
    IoClock::time_point cancel_deadline_ = IoClock::time_point::max();
    const std::atomic<bool>* transfer_token_ = nullptr;
    bool transfer_operation_ = false;

    std::atomic<bool> connected_{false};
    std::atomic<bool> shell_open_{false};
    // 远端 pty 认定的尺寸。用 atomic 是因为 UI 线程每帧都要读它比对，
    // 而 I/O owner 会更新这些值。
    std::atomic<int>  pty_cols_{0};
    std::atomic<uint64_t> pty_request_{0};
    std::atomic<int>  pty_rows_{0};
    std::atomic<bool> eof_{false};
    std::atomic<int>  fail_stage_{0};     // 上次 connect 失败在 FailStage 的哪一层
    WakeFn           wake_;
    KnownHosts       known_;
    std::string      fingerprint_, note_, host_, user_, cwd_;
    std::string      kbd_pass_;      // 给 keyboard-interactive 回调的密码
    int              port_ = 22;
    Transfer         xfer_;

    bool init_sftp(std::string* err) {
        sftp_ = await_io([&] { return libssh2_sftp_init(sess_); });
        if (!sftp_) return fail(err, "SFTP 初始化失败：" + err_text(sess_, libssh2_session_last_errno(sess_)));
        sftp_ready_ = true;
        return true;
    }

    std::string sftp_error() const {
        if (wire_failed_) return wire_error_;
        const int code = sess_ ? libssh2_session_last_errno(sess_) : LIBSSH2_ERROR_SOCKET_DISCONNECT;
        if (code < 0 && code != LIBSSH2_ERROR_SFTP_PROTOCOL) return err_text(sess_, code);
        if (!sftp_) return "SFTP 未就绪";
        const unsigned long e = libssh2_sftp_last_error(sftp_);
        switch (e) {
        case LIBSSH2_FX_NO_SUCH_FILE:    return "文件或目录不存在";
        case LIBSSH2_FX_PERMISSION_DENIED: return "权限不足";
        case LIBSSH2_FX_NO_SUCH_PATH:    return "路径不存在";
        case LIBSSH2_FX_FILE_ALREADY_EXISTS: return "已存在同名文件";
        case LIBSSH2_FX_DIR_NOT_EMPTY:   return "目录非空";
        case LIBSSH2_FX_WRITE_PROTECT:   return "只读挂载";
        default:                         return fmt("SFTP 错误 %lu", e);
        }
    }

    bool sftp_missing(int result) const {
        // 传输层失败不一定刷新 SFTP status，不能把上一次的“不存在”用于当前请求。
        if (result != LIBSSH2_ERROR_SFTP_PROTOCOL) return false;
        const auto status = libssh2_sftp_last_error(sftp_);
        return status == LIBSSH2_FX_NO_SUCH_FILE || status == LIBSSH2_FX_NO_SUCH_PATH;
    }

    bool commit_upload() {
        if (!sftp_) return false;
#if LIBSSH2_VERSION_NUM >= 0x010B01
        // SFTP v3 的标准 RENAME 不携带覆盖标志；OpenSSH 另提供原子替换扩展。
        const int rc = await_io([&] { return libssh2_sftp_posix_rename_ex(sftp_, xfer_.temporary.c_str(), xfer_.temporary.size(),
                                                   xfer_.remote.c_str(), xfer_.remote.size()); });
        if (rc == 0) return true;
        // 超时/断线可能已提交，不能再执行一次替换；仅“不支持”允许走标准路径。
        if (rc != LIBSSH2_FX_OP_UNSUPPORTED && !(rc == LIBSSH2_ERROR_SFTP_PROTOCOL
            && libssh2_sftp_last_error(sftp_) == LIBSSH2_FX_OP_UNSUPPORTED)) return false;
#endif
        return await_io([&] { return libssh2_sftp_rename_ex(sftp_, xfer_.temporary.c_str(), unsigned(xfer_.temporary.size()),
            xfer_.remote.c_str(), unsigned(xfer_.remote.size()),
            LIBSSH2_SFTP_RENAME_OVERWRITE | LIBSSH2_SFTP_RENAME_ATOMIC | LIBSSH2_SFTP_RENAME_NATIVE); }) == 0;
    }

    void finish_transfer() {
        if (xfer_.kind == Transfer::None || xfer_.finalized) return;
        if (xfer_.rh) {
            if (xfer_.kind == Transfer::Upload && xfer_.error.empty()) {
                auto attributes = xfer_.destination_attributes;
                LIBSSH2_SFTP_ATTRIBUTES staged{};
                if (await_io([&] { return libssh2_sftp_fstat(xfer_.rh, &staged); }) == 0) {
                    if ((staged.flags & LIBSSH2_SFTP_ATTR_UIDGID) && staged.uid == attributes.uid
                        && staged.gid == attributes.gid) attributes.flags &= ~LIBSSH2_SFTP_ATTR_UIDGID;
                    if ((staged.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS)
                        && (staged.permissions & 07777) == (attributes.permissions & 07777))
                        attributes.flags &= ~LIBSSH2_SFTP_ATTR_PERMISSIONS;
                }
                if (attributes.flags && await_io([&] { return libssh2_sftp_fstat_ex(xfer_.rh, &attributes, 1); }) != 0)
                    xfer_.error = "无法保留目标权限或属主，未替换目标文件";
            }
            const int rc = await_io([&] { return libssh2_sftp_close(xfer_.rh); });
            xfer_.rh = nullptr;
            if (rc != 0 && xfer_.error.empty()) xfer_.error = "远端文件关闭失败";
        }
        if (xfer_.fh) {
            if (xfer_.kind == Transfer::Download && xfer_.error.empty()) {
                bool synced = std::fflush(xfer_.fh) == 0;
#if defined(_WIN32)
                if (synced) synced = _commit(_fileno(xfer_.fh)) == 0;
#else
                if (synced) synced = fsync(fileno(xfer_.fh)) == 0;
#endif
                if (!synced) xfer_.error = "本地文件同步失败：" + xfer_.local;
            }
            const int rc = std::fclose(xfer_.fh);
            xfer_.fh = nullptr;
            if (rc != 0 && xfer_.error.empty()) xfer_.error = "本地文件关闭失败：" + xfer_.local;
        }
        if (xfer_.error.empty() && xfer_.total_known && xfer_.done != xfer_.total)
            xfer_.error = "传输长度不一致（源文件变化或传输中断）";
        // 此处是最后一个可取消边界。进入原子替换后只报告真实提交结果。
        if (xfer_.error.empty() && (wire_failed_ || transfer_cancel_ || (transfer_token_ && transfer_token_->load())))
            xfer_.error = wire_failed_ ? wire_error_ : "已取消";
        if (xfer_.temporary.empty()) { xfer_.finalized = true; return; }
        if (xfer_.error.empty()) {
            const bool committed = xfer_.kind == Transfer::Download
                ? replace_transfer_file(xfer_.temporary, xfer_.local)
                : commit_upload();
            if (!committed) xfer_.error = "无法确认目标文件替换结果，请核对目标文件；未继续覆盖";
        }
        if (!xfer_.error.empty()) {
            if (xfer_.kind == Transfer::Download) {
                std::error_code ignored;
                std::filesystem::remove(std::filesystem::u8path(xfer_.temporary), ignored);
                if (ignored) xfer_.error += "；本地临时文件需手动清理：" + xfer_.temporary;
            } else if (sftp_) {
                if (await_io([&] { return libssh2_sftp_unlink(sftp_, xfer_.temporary.c_str()); }) != 0)
                    xfer_.error += "；远端临时文件需手动清理：" + xfer_.temporary;
            }
        }
        xfer_.temporary.clear();
        xfer_.finalized = true;
    }

    static bool fail(std::string* err, const std::string& m) { if (err) *err = m; return false; }
    static void disarm(std::string* err) { if (err) err->clear(); }

    // ---- TCP 层的失败原因（跨平台错误码）----
    // 只报"连不上"没法行动：用户需要区分"网络不通"和"服务没开"。
    static constexpr int kTcpTimeoutMs = 8000;
#if defined(_WIN32)
    static constexpr int kSoTimedOut   = WSAETIMEDOUT;
    static constexpr int kSoRefused    = WSAECONNREFUSED;
    static constexpr int kSoHostUnreach= WSAEHOSTUNREACH;
    static constexpr int kSoNetUnreach = WSAENETUNREACH;
#else
    static constexpr int kSoTimedOut   = ETIMEDOUT;
    static constexpr int kSoRefused    = ECONNREFUSED;
    static constexpr int kSoHostUnreach= EHOSTUNREACH;
    static constexpr int kSoNetUnreach = ENETUNREACH;
#endif

    static int sock_last_error() {
#if defined(_WIN32)
        return WSAGetLastError();
#else
        return errno;
#endif
    }

    static std::string tcp_trouble_text(int so, bool tried, const std::string& host, int port) {
        const std::string addr = fmt("%s:%d", host.c_str(), port);
        if (!tried) return fmt("连不上 %s：本机连 socket 都建不起来", addr.c_str());
        if (so == kSoTimedOut)
            return fmt("%s 连接超时（等满 %d 秒没有任何回应）—— 网络不通、防火墙丢包，或对方关机。"
                       "可以先 ping 这台机器确认能通",
                       addr.c_str(), kTcpTimeoutMs / 1000);
        if (so == kSoRefused)
            return fmt("%s 拒绝连接 —— 网络是通的，但这台机器的 %d 端口没有服务在监听"
                       "（sshd 没启动、端口改过、或被防火墙 reject）",
                       addr.c_str(), port);
        if (so == kSoHostUnreach)
            return fmt("%s 主机不可达 —— 没有到这台机器的路由（VPN 没连？网段不对？）", addr.c_str());
        if (so == kSoNetUnreach)
            return fmt("%s 网络不可达 —— 本机没有到该网段的路由（检查 VPN / 默认网关）", addr.c_str());
        return fmt("连不上 %s（系统错误码 %d）", addr.c_str(), so);
    }

    static int userauth_keyboard(LIBSSH2_SESSION* sess, const std::string& user,
                                 const std::string& /*pass*/) {
        // 有些服务器（带 PAM/2FA 的）只提供 keyboard-interactive。
        // libssh2 的回调没有 userdata 参数 —— 密码是在 session_init_ex 时作为
        // abstract 挂上去的，回调里从 *abstract 取回来（见 connect 里的 kbd_pass_）。
        return libssh2_userauth_keyboard_interactive_ex(
            sess, user.c_str(), unsigned(user.size()),
            [](const char* /*name*/, int /*name_len*/, const char* /*instr*/, int /*instr_len*/,
               int num_prompts, const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
               LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses, void** abstract) {
                const std::string* pw = abstract ? static_cast<const std::string*>(*abstract) : nullptr;
                for (int i = 0; i < num_prompts; ++i) {
                    // echo 为真的提示是"服务器问你要什么"，密码只在隐藏输入时给
                    const std::string ans = prompts[i].echo ? std::string()
                                                            : (pw ? *pw : std::string());
                    responses[i].text = static_cast<char*>(std::malloc(ans.size() + 1));
                    if (responses[i].text) {
                        std::memcpy(responses[i].text, ans.c_str(), ans.size() + 1);
                        responses[i].length = unsigned(ans.size());
                    }
                }
            });
    }

    static bool set_nonblocking(SSHC_SOCK fd, bool on) {
#if defined(_WIN32)
        u_long v = on ? 1 : 0;
        return ioctlsocket(fd, FIONBIO, &v) == 0;
#else
        const int fl = fcntl(fd, F_GETFL, 0);
        return fl >= 0 && fcntl(fd, F_SETFL, on ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK)) == 0;
#endif
    }
    static bool connect_in_progress() {
#if defined(_WIN32)
        return WSAGetLastError() == WSAEWOULDBLOCK;
#else
        return errno == EINPROGRESS;
#endif
    }
    static void set_nodelay(SSHC_SOCK fd) {
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
    }
    bool wait_sock(SSHC_SOCK fd, bool writing, int ms) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (!stop_requested_) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                until - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) return false;
            fd_set set, errors; FD_ZERO(&set); FD_ZERO(&errors);
            FD_SET(fd, &set); FD_SET(fd, &errors);
            const long slice = long(std::min<int64_t>(remaining, 100));
            timeval tv{0, slice * 1000};
            const int r = ::select(int(fd) + 1, writing ? nullptr : &set, writing ? &set : nullptr, &errors, &tv);
            if (r != 0) return r > 0;
        }
        return false;
    }
};

// =============================================================================
//  3. Terminal —— VT100 / xterm 解析器 + 字符网格
//
//  只做"屏幕状态"，不碰像素：解析字节流 → 往网格里写字符/改光标/滚屏，
//  渲染交给调用方（ssh_client.cpp 里用 ui.draw_mono_text 按 (row,col) 画）。
//
//  为什么值得写一个真的解析器而不是"把 \x1b[..m 换成颜色后就 print"：
//  交互式程序（vim / top / htop / less / 各种 TUI 安装器）靠光标定位、
//  区域滚动、插入行、备用屏这些动作作画，缺一个就是满屏乱码。
// =============================================================================
struct Cell {
    uint32_t ch = ' ';
    uint32_t fg = 0xFFFFFFFFu;      // 0..255 = 调色板下标，否则 = 0x1000000|rgb
    uint32_t bg = 0xFFFFFFFEu;      // 0xFFFFFFFE = 默认底色
    uint8_t  attr = 0;
};

enum : uint8_t {
    A_BOLD   = 1,
    A_DIM    = 2,
    A_ITALIC = 4,
    A_UNDER  = 8,
    A_REV    = 16,
    A_HIDE   = 32,
};

inline constexpr uint32_t kDefaultFg = 0xFFFFFFFFu;
inline constexpr uint32_t kDefaultBg = 0xFFFFFFFEu;
inline constexpr uint32_t kRgbFlag   = 0x1000000u;

// 宽字符判定（wcwidth 的极简版）：0 = 组合符（不占位），2 = 全角，1 = 半角。
// 覆盖的是终端里真会出现的范围：CJK、假名、韩文、全角标点、emoji、组合符。
inline int wc_width(uint32_t cp) {
    if (cp == 0) return 0;
    if (cp < 32) return 0;
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE00 && cp <= 0xFE0F) ||
        (cp >= 0xFE20 && cp <= 0xFE2F) || cp == 0x200B || cp == 0x200D)
        return 0;
    if ((cp >= 0x1100 && cp <= 0x115F) ||
        (cp >= 0x2E80 && cp <= 0x303E) || (cp >= 0x3041 && cp <= 0x33FF) ||
        (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
        (cp >= 0xA000 && cp <= 0xA4CF) || (cp >= 0xA960 && cp <= 0xA97F) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE10 && cp <= 0xFE19) || (cp >= 0xFE30 && cp <= 0xFE6F) ||
        (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F300 && cp <= 0x1F64F) || (cp >= 0x1F900 && cp <= 0x1F9FF) ||
        (cp >= 0x20000 && cp <= 0x3FFFD))
        return 2;
    return 1;
}

class Terminal {
public:
    int cols = 80, rows = 24;
    size_t scrollback_max = 4000;

    Terminal() { reset(); }

    void reset();
    void resize(int new_cols, int new_rows);
    void feed(const char* data, size_t n);

    // 网格访问：屏幕行 0..rows-1；历史行用 history_row()
    const std::vector<Cell>& screen_row(int i) const { return grid_[size_t(i)]; }
    const std::vector<Cell>& history_row(int i) const { return hist_[size_t(i)]; }
    int  history_size() const { return int(hist_.size()); }
    int  total_lines() const { return int(hist_.size()) + rows; }

    // 任意"绝对行"（历史 + 屏幕统一编号）上的文本，供复制用
    std::string line_text(int abs_line) const;

    int  cursor_row() const { return cy_; }
    int  cursor_col() const { return cx_; }
    bool cursor_visible() const { return show_cursor_; }
    bool alt_screen() const { return alt_mode_; }

    // 选区（绝对行坐标：0..history_size()+rows-1）
    void sel_begin(int abs_line, int col);
    void sel_extend(int abs_line, int col);
    void sel_clear() { sel_active_ = false; }
    bool has_selection() const { return sel_active_; }
    void clear_selection() { sel_active_ = false; }
    // 归一化后的选区（起点在前）。渲染高亮要用，所以给出去。
    void sel_range(int* l0, int* c0, int* l1, int* c1) const {
        if (sel_a_line_ > sel_b_line_ || (sel_a_line_ == sel_b_line_ && sel_a_col_ > sel_b_col_)) {
            *l0 = sel_b_line_; *c0 = sel_b_col_; *l1 = sel_a_line_; *c1 = sel_a_col_;
        } else {
            *l0 = sel_a_line_; *c0 = sel_a_col_; *l1 = sel_b_line_; *c1 = sel_b_col_;
        }
    }
    std::string selected_text() const;

    // 待写回对端的字节（DSR/DA 之类的应答），调用方每帧 drain 一次
    std::string take_reply() { std::string s; s.swap(reply_); return s; }

private:
    std::vector<std::vector<Cell>> grid_;
    std::vector<std::vector<Cell>> alt_;
    std::deque<std::vector<Cell>>  hist_;
    int cy_ = 0, cx_ = 0;
    int top_ = 0, bot_ = 23;
    uint32_t fg_ = kDefaultFg, bg_ = kDefaultBg;
    uint8_t  attr_ = 0;
    bool     autowrap_ = true, show_cursor_ = true, app_cursor_ = false, alt_mode_ = false;
    bool     origin_ = false, insert_ = false, wrap_pending_ = false;

    int saved_x_ = 0, saved_y_ = 0;
    uint32_t saved_fg_ = kDefaultFg, saved_bg_ = kDefaultBg;
    uint8_t  saved_attr_ = 0;

    bool sel_active_ = false;
    int  sel_a_line_ = 0, sel_a_col_ = 0, sel_b_line_ = 0, sel_b_col_ = 0;

    std::string reply_;

    // 解析器状态
    enum State { S_GROUND, S_ESC, S_CSI, S_OSC, S_CHARSET, S_OSC_ESC };
    State   st_ = S_GROUND;
    std::string csi_;            // CSI 参数字节
    uint32_t utf8_cp_ = 0;
    int      utf8_need_ = 0;
    uint32_t last_cp_ = 0;       // 组合符要挂到上一个字符上

    std::vector<Cell>& row_ref(int i) { return grid_[size_t(i)]; }

    void blank_row(std::vector<Cell>* r);
    void clear_cells(int y, int x0, int x1);
    void scroll_up(int n);
    void scroll_down(int n);
    void newline();
    void put_cp(uint32_t cp);
    void move_to(int y, int x);
    void save_cursor();
    void restore_cursor();
    void erase_display(int mode);
    void erase_line(int mode);
    void sgr(const std::vector<int>& p);
    void csi_dispatch(char final, const std::vector<int>& p, char priv);
    void push_history();
    int  abs_to_screen(int abs_line) const { return abs_line - int(hist_.size()); }
};

inline void Terminal::blank_row(std::vector<Cell>* r) {
    r->assign(size_t(cols), Cell{});
}

inline void Terminal::reset() {
    grid_.assign(size_t(rows), std::vector<Cell>(size_t(cols)));
    alt_.assign(size_t(rows), std::vector<Cell>(size_t(cols)));
    for (auto& r : grid_) blank_row(&r);
    for (auto& r : alt_) blank_row(&r);
    hist_.clear();
    cy_ = cx_ = 0;
    top_ = 0;
    bot_ = rows - 1;
    fg_ = kDefaultFg;
    bg_ = kDefaultBg;
    attr_ = 0;
    autowrap_ = true;
    show_cursor_ = true;
    alt_mode_ = false;
    origin_ = false;
    insert_ = false;
    wrap_pending_ = false;
    st_ = S_GROUND;
    csi_.clear();
    utf8_need_ = 0;
    sel_active_ = false;
}

inline void Terminal::resize(int new_cols, int new_rows) {
    new_cols = std::max(2, new_cols);
    new_rows = std::max(2, new_rows);
    if (new_cols == cols && new_rows == rows) return;
    // 不做 reflow（那需要按逻辑行重排，成本远大于收益）：左上角锚定，
    // 宽了补空格、窄了截断。fsck/编译输出这类长行会被切，但滚动区不会错位。
    auto reflow = [&](std::vector<std::vector<Cell>>& g) {
        // 这里必须直接用 new_cols 造行，**不能**借 blank_row()：blank_row 是按成员
        // cols 填的，而 cols 要到函数末尾才更新 —— 结果新 grid 的每行还是旧宽度
        // （80），随后 put_cp 按 cx_ 写到第 81 格就踩坏堆，表现为 0xC0000374，
        // 或者"连上了但右边一大片是空的"。
        // 别写成 vector<vector<Cell>> out(n, vector<Cell>(m)) 那句声明式初始化
        // （most vexing parse），用 assign 最稳。
        std::vector<std::vector<Cell>> out;
        out.assign(size_t(new_rows), std::vector<Cell>(size_t(new_cols)));
        const int copy_rows = std::min(new_rows, int(g.size()));
        for (int y = 0; y < copy_rows; ++y) {
            const int copy_cols = std::min(new_cols, int(g[size_t(y)].size()));
            for (int x = 0; x < copy_cols; ++x) out[size_t(y)][size_t(x)] = g[size_t(y)][size_t(x)];
        }
        g.swap(out);
    };
    reflow(grid_);
    reflow(alt_);
    // 历史行的宽度要跟着变：resize 变宽会补 Cell{}，变窄直接截断
    for (auto& r : hist_) r.resize(size_t(new_cols));
    cols = new_cols;
    rows = new_rows;
    top_ = 0;
    bot_ = rows - 1;
    cy_ = std::min(cy_, rows - 1);
    cx_ = std::min(cx_, cols - 1);
    sel_active_ = false;
}

inline void Terminal::clear_cells(int y, int x0, int x1) {
    if (y < 0 || y >= rows) return;
    std::vector<Cell>& r = row_ref(y);
    x0 = std::max(0, x0);
    x1 = std::min(cols - 1, x1);
    for (int x = x0; x <= x1; ++x) {
        r[size_t(x)] = Cell{};
        if (bg_ != kDefaultBg) r[size_t(x)].bg = bg_;   // 有底色时按当前底色擦（和真终端一致）
    }
}

inline void Terminal::push_history() {
    if (alt_mode_) return;                // 备用屏不记历史
    hist_.push_back(grid_[size_t(top_)]);
    if (hist_.size() > scrollback_max) hist_.pop_front();
}

inline void Terminal::scroll_up(int n) {
    n = std::max(0, std::min(n, bot_ - top_ + 1));
    for (int i = 0; i < n; ++i) {
        // 只有"整屏滚动"（top==0 && bot==rows-1）才进历史；区域内滚动（vim 的状态行）不算
        const bool full = (top_ == 0 && bot_ == rows - 1);
        if (full) push_history();
        for (int y = top_; y < bot_; ++y) grid_[size_t(y)] = grid_[size_t(y + 1)];
        blank_row(&grid_[size_t(bot_)]);
        for (auto& c : grid_[size_t(bot_)]) if (bg_ != kDefaultBg) c.bg = bg_;
    }
}

inline void Terminal::scroll_down(int n) {
    n = std::max(0, std::min(n, bot_ - top_ + 1));
    for (int i = 0; i < n; ++i) {
        for (int y = bot_; y > top_; --y) grid_[size_t(y)] = grid_[size_t(y - 1)];
        blank_row(&grid_[size_t(top_)]);
        for (auto& c : grid_[size_t(top_)]) if (bg_ != kDefaultBg) c.bg = bg_;
    }
}

inline void Terminal::newline() {
    if (cy_ == bot_) scroll_up(1);
    else if (cy_ < rows - 1) ++cy_;
    wrap_pending_ = false;
}

inline void Terminal::move_to(int y, int x) {
    if (origin_) {
        const int h = bot_ - top_ + 1;
        cy_ = top_ + std::min(std::max(y, 0), h - 1);
        cx_ = std::min(std::max(x, 0), cols - 1);
    } else {
        cy_ = std::min(std::max(y, 0), rows - 1);
        cx_ = std::min(std::max(x, 0), cols - 1);
    }
    wrap_pending_ = false;
}

inline void Terminal::save_cursor() {
    saved_x_ = cx_; saved_y_ = cy_;
    saved_fg_ = fg_; saved_bg_ = bg_; saved_attr_ = attr_;
}

inline void Terminal::restore_cursor() {
    cx_ = std::min(saved_x_, cols - 1);
    cy_ = std::min(saved_y_, rows - 1);
    fg_ = saved_fg_; bg_ = saved_bg_; attr_ = saved_attr_;
    wrap_pending_ = false;
}

inline void Terminal::erase_display(int mode) {
    if (mode == 0) {
        clear_cells(cy_, cx_, cols - 1);
        for (int y = cy_ + 1; y < rows; ++y) clear_cells(y, 0, cols - 1);
    } else if (mode == 1) {
        for (int y = 0; y < cy_; ++y) clear_cells(y, 0, cols - 1);
        clear_cells(cy_, 0, cx_);
    } else if (mode == 2 || mode == 3) {
        for (int y = 0; y < rows; ++y) clear_cells(y, 0, cols - 1);
        if (mode == 3) hist_.clear();
    }
}

inline void Terminal::erase_line(int mode) {
    if (mode == 0) clear_cells(cy_, cx_, cols - 1);
    else if (mode == 1) clear_cells(cy_, 0, cx_);
    else if (mode == 2) clear_cells(cy_, 0, cols - 1);
}

inline void Terminal::put_cp(uint32_t cp) {
    if (cols <= 0 || rows <= 0) return;
    // 兜底夹紧：光标理论上不该越界，但远端的 CSI 参数可以很离谱（`\x1b[999;999H`），
    // 一旦真有漏夹的地方，这里宁可画在边上也不要写坏堆。
    if (cx_ < 0) cx_ = 0;
    if (cx_ >= cols) cx_ = cols - 1;
    if (cy_ < 0) cy_ = 0;
    if (cy_ >= rows) cy_ = rows - 1;
    const int w = wc_width(cp);
    if (w == 0) {
        // 组合符：并到上一个格子（存成 base + combining 的 UTF-8 串）
        if (last_cp_ && cx_ > 0) {
            std::vector<Cell>& r = row_ref(cy_);
            const int at = (cx_ > 0) ? (cx_ - 1) : 0;
            Cell& c = r[size_t(at)];
            if (c.ch < 0x110000 && c.ch != ' ' && c.ch >= 0x20) {
                // 追加到该格的字符序列（UTF-8 编码，渲染时 GDI 会正确处理）
                auto enc = [](uint32_t u, std::string& out) {
                    if (u < 0x80) out.push_back(char(u));
                    else if (u < 0x800) { out.push_back(char(0xC0 | (u >> 6))); out.push_back(char(0x80 | (u & 0x3F))); }
                    else if (u < 0x10000) { out.push_back(char(0xE0 | (u >> 12))); out.push_back(char(0x80 | ((u >> 6) & 0x3F))); out.push_back(char(0x80 | (u & 0x3F))); }
                    else { out.push_back(char(0xF0 | (u >> 18))); out.push_back(char(0x80 | ((u >> 12) & 0x3F))); out.push_back(char(0x80 | ((u >> 6) & 0x3F))); out.push_back(char(0x80 | (u & 0x3F))); }
                };
                (void)enc;
                c.attr |= 0x80;   // 标记"带组合符"，渲染时单独处理
            }
        }
        return;
    }
    if (wrap_pending_) {
        if (autowrap_) { cx_ = 0; newline(); }
        wrap_pending_ = false;
    }
    if (w == 2 && cx_ == cols - 1) {          // 全角字符不能跨行劈开
        if (autowrap_) { cx_ = 0; newline(); }
        else return;
    }
    if (insert_) {
        std::vector<Cell>& r = row_ref(cy_);
        for (int x = cols - 1; x > cx_; --x) r[size_t(x)] = r[size_t(x - 1)];
    }
    std::vector<Cell>& r = row_ref(cy_);
    Cell c;
    c.ch = cp;
    c.fg = fg_;
    c.bg = bg_;
    c.attr = attr_;
    r[size_t(cx_)] = c;
    if (w == 2 && cx_ + 1 < cols) {
        Cell cont;
        cont.ch = 0;                 // 0 = 全角字符的右半格占位
        cont.fg = fg_;
        cont.bg = bg_;
        cont.attr = attr_;
        r[size_t(cx_ + 1)] = cont;
        cx_ += 2;
    } else {
        cx_ += 1;
    }
    if (cx_ >= cols) {
        cx_ = cols - 1;
        wrap_pending_ = true;
    }
    last_cp_ = cp;
}

inline void Terminal::sgr(const std::vector<int>& p) {
    if (p.empty()) { fg_ = kDefaultFg; bg_ = kDefaultBg; attr_ = 0; return; }
    for (size_t i = 0; i < p.size(); ++i) {
        const int v = p[i];
        switch (v) {
        case 0: fg_ = kDefaultFg; bg_ = kDefaultBg; attr_ = 0; break;
        case 1: attr_ |= A_BOLD; break;
        case 2: attr_ |= A_DIM; break;
        case 3: attr_ |= A_ITALIC; break;
        case 4: attr_ |= A_UNDER; break;
        case 7: attr_ |= A_REV; break;
        case 8: attr_ |= A_HIDE; break;
        case 22: attr_ = uint8_t(attr_ & ~(A_BOLD | A_DIM)); break;
        case 23: attr_ = uint8_t(attr_ & ~A_ITALIC); break;
        case 24: attr_ = uint8_t(attr_ & ~A_UNDER); break;
        case 27: attr_ = uint8_t(attr_ & ~A_REV); break;
        case 28: attr_ = uint8_t(attr_ & ~A_HIDE); break;
        case 39: fg_ = kDefaultFg; break;
        case 49: bg_ = kDefaultBg; break;
        case 38: case 48: {
            uint32_t col = 0;
            if (i + 2 < p.size() && p[i + 1] == 5) {
                col = uint32_t(std::min(std::max(p[i + 2], 0), 255));
                i += 2;
            } else if (i + 4 < p.size() && p[i + 1] == 2) {
                const int r = std::min(std::max(p[i + 2], 0), 255);
                const int g = std::min(std::max(p[i + 3], 0), 255);
                const int b = std::min(std::max(p[i + 4], 0), 255);
                col = kRgbFlag | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
                i += 4;
            }
            if (v == 38) fg_ = col;
            else bg_ = col;
            break;
        }
        default:
            if (v >= 30 && v <= 37) fg_ = uint32_t(v - 30);
            else if (v >= 40 && v <= 47) bg_ = uint32_t(v - 40);
            else if (v >= 90 && v <= 97) fg_ = uint32_t(v - 90 + 8);
            else if (v >= 100 && v <= 107) bg_ = uint32_t(v - 100 + 8);
            break;
        }
    }
}

inline void Terminal::csi_dispatch(char final, const std::vector<int>& p, char priv) {
    auto arg = [&](size_t i, int def) { return i < p.size() && p[i] != 0 ? p[i] : def; };
    switch (final) {
    case 'A': move_to(cy_ - std::min(arg(0, 1), rows), cx_); break;
    case 'B': move_to(cy_ + std::min(arg(0, 1), rows), cx_); break;
    case 'C': move_to(cy_, cx_ + std::min(arg(0, 1), cols)); break;
    case 'D': move_to(cy_, cx_ - std::min(arg(0, 1), cols)); break;
    case 'E': move_to(cy_ + std::min(arg(0, 1), rows), 0); break;
    case 'F': move_to(cy_ - std::min(arg(0, 1), rows), 0); break;
    case 'G': case '`': move_to(cy_, std::min(arg(0, 1), cols) - 1); break;
    case 'd': move_to(std::min(arg(0, 1), rows) - 1, cx_); break;
    case 'H': case 'f': move_to(std::min(arg(0, 1), rows) - 1, std::min(arg(1, 1), cols) - 1); break;
    case 'J': erase_display(arg(0, 0)); break;
    case 'K': erase_line(arg(0, 0)); break;
    case 'L': {                                   // 插入行
        if (cy_ < top_ || cy_ > bot_) break;
        const int n = std::min(arg(0, 1), bot_ - cy_ + 1);
        for (int i = 0; i < n; ++i) {
            for (int y = bot_; y > cy_; --y) grid_[size_t(y)] = grid_[size_t(y - 1)];
            blank_row(&grid_[size_t(cy_)]);
        }
        break;
    }
    case 'M': {                                   // 删除行
        if (cy_ < top_ || cy_ > bot_) break;
        const int n = std::min(arg(0, 1), bot_ - cy_ + 1);
        for (int i = 0; i < n; ++i) {
            for (int y = cy_; y < bot_; ++y) grid_[size_t(y)] = grid_[size_t(y + 1)];
            blank_row(&grid_[size_t(bot_)]);
        }
        break;
    }
    case 'P': {                                   // 删字符
        const int n = std::min(arg(0, 1), cols - cx_);
        std::vector<Cell>& r = row_ref(cy_);
        for (int x = cx_; x < cols; ++x)
            r[size_t(x)] = (n < cols - x) ? r[size_t(x + n)] : Cell{};
        break;
    }
    case '@': {                                   // 插入字符
        const int n = std::min(arg(0, 1), cols - cx_);
        std::vector<Cell>& r = row_ref(cy_);
        for (int x = cols - 1; x >= cx_ + n; --x) r[size_t(x)] = r[size_t(x - n)];
        for (int x = cx_; x < cx_ + n && x < cols; ++x) r[size_t(x)] = Cell{};
        break;
    }
    case 'X': clear_cells(cy_, cx_, cx_ + std::min(arg(0, 1), cols - cx_) - 1); break;
    case 'S': scroll_up(arg(0, 1)); break;
    case 'T': scroll_down(arg(0, 1)); break;
    case 'm': sgr(p); break;
    case 'r': {
        const int t = arg(0, 1) - 1;
        const int b = arg(1, rows) - 1;
        if (t >= 0 && b < rows && t < b) { top_ = t; bot_ = b; move_to(0, 0); }
        break;
    }
    case 's': save_cursor(); break;
    case 'u': restore_cursor(); break;
    case 'h':
    case 'l': {
        const bool on = (final == 'h');
        for (int m : p) {
            switch (m) {
            case 25: show_cursor_ = on; break;
            case 7:  autowrap_ = on; break;
            case 1:  app_cursor_ = on; break;
            case 6:  origin_ = on; move_to(0, 0); break;
            case 4:  insert_ = on; break;
            case 1049: case 1047: case 47:
                if (on && !alt_mode_) { alt_mode_ = true; grid_.swap(alt_); for (auto& r : grid_) blank_row(&r); save_cursor(); move_to(0, 0); }
                else if (!on && alt_mode_) { alt_mode_ = false; grid_.swap(alt_); restore_cursor(); }
                break;
            default: break;
            }
        }
        (void)priv;
        break;
    }
    case 'n': {
        if (arg(0, 0) == 6) reply_ += fmt("\x1b[%d;%dR", cy_ + 1, cx_ + 1);
        else if (arg(0, 0) == 5) reply_ += "\x1b[0n";
        break;
    }
    case 'c': reply_ += "\x1b[?6c"; break;         // DA：报 VT102
    case 't': break;                                // 窗口操作：忽略
    case 'q': break;                                // 光标形状：忽略
    default: break;
    }
}

inline void Terminal::feed(const char* data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const unsigned char b = static_cast<unsigned char>(data[i]);

        // UTF-8 续字节优先处理（任何状态下都要先凑完整）
        if (utf8_need_ > 0) {
            if ((b & 0xC0) == 0x80) {
                utf8_cp_ = (utf8_cp_ << 6) | uint32_t(b & 0x3F);
                if (--utf8_need_ == 0) put_cp(utf8_cp_);
                continue;
            }
            utf8_need_ = 0;           // 非法序列：丢掉未完成的那个，把这个字节当新字符
        }

        switch (st_) {
        case S_OSC:
            if (b == 0x07) { st_ = S_GROUND; }
            else if (b == 0x1b) { st_ = S_OSC_ESC; }
            else if (b == 0x00) { /* 忽略 */ }
            continue;
        case S_OSC_ESC:
            // OSC 的结束符是 ESC \（ST）；ESC 后跟别的就当地面字符重新开始
            if (b == '\\') { st_ = S_GROUND; continue; }
            st_ = S_GROUND;
            break;
        case S_CHARSET:
            st_ = S_GROUND;
            continue;
        case S_ESC:
            switch (b) {
            case '[': st_ = S_CSI; csi_.clear(); continue;
            case ']': st_ = S_OSC; continue;
            case '(': case ')': case '*': case '+': st_ = S_CHARSET; continue;
            case '7': save_cursor(); st_ = S_GROUND; continue;
            case '8': restore_cursor(); st_ = S_GROUND; continue;
            case 'D': newline(); st_ = S_GROUND; continue;
            case 'E': cx_ = 0; newline(); st_ = S_GROUND; continue;
            case 'M':
                if (cy_ == top_) scroll_down(1);
                else if (cy_ > 0) --cy_;
                st_ = S_GROUND;
                continue;
            case 'H': st_ = S_GROUND; continue;      // 制表位：忽略
            case 'c': reset(); st_ = S_GROUND; continue;
            case '=': case '>': st_ = S_GROUND; continue;
            default: st_ = S_GROUND; continue;
            }
        case S_CSI: {
            if (b >= 0x40 && b <= 0x7e) {
                // 收尾：解析 ">?0123456789;:" 这些参数
                char priv = 0;
                std::vector<int> p;
                int cur = -1;
                for (char c : csi_) {
                    if (c == '?' || c == '>' || c == '<' || c == '=') { if (p.empty() && cur < 0) priv = c; }
                    else if (c >= '0' && c <= '9') {
                        const int value = cur < 0 ? 0 : cur;
                        const int digit = c - '0';
                        cur = value > (std::numeric_limits<int>::max() - digit) / 10
                            ? std::numeric_limits<int>::max() : value * 10 + digit;
                    }
                    else if (c == ';' || c == ':') { p.push_back(cur < 0 ? 0 : cur); cur = -1; }
                }
                if (cur >= 0 || !csi_.empty()) p.push_back(cur < 0 ? 0 : cur);
                csi_dispatch(char(b), p, priv);
                st_ = S_GROUND;
                csi_.clear();
                continue;
            }
            csi_.push_back(char(b));
            if (csi_.size() > 64) { st_ = S_GROUND; csi_.clear(); }
            continue;
        }
        case S_GROUND:
        default: break;
        }

        // 地面状态：C0 控制符 + 可打印字符
        if (b < 0x20) {
            switch (b) {
            case 0x07: break;                        // BEL：静音（可以在这换成系统提示音）
            case 0x08: if (cx_ > 0) { --cx_; wrap_pending_ = false; } break;
            case 0x09: {
                const int nx = ((cx_ / 8) + 1) * 8;
                cx_ = std::min(nx, cols - 1);
                wrap_pending_ = false;
                break;
            }
            case 0x0a: case 0x0b: case 0x0c: newline(); break;
            case 0x0d: cx_ = 0; wrap_pending_ = false; break;
            case 0x0e: case 0x0f: break;             // SO/SI：忽略（不是 CJK 双字节模式）
            case 0x1b: st_ = S_ESC; break;
            default: break;
            }
            continue;
        }
        if (b < 0x80) { put_cp(b); continue; }

        // UTF-8 起始字节
        if ((b & 0xE0) == 0xC0) { utf8_cp_ = b & 0x1F; utf8_need_ = 1; }
        else if ((b & 0xF0) == 0xE0) { utf8_cp_ = b & 0x0F; utf8_need_ = 2; }
        else if ((b & 0xF8) == 0xF0) { utf8_cp_ = b & 0x07; utf8_need_ = 3; }
        // 其它（0x80..0xBF 孤立续字节）直接丢
    }
}

inline std::string Terminal::line_text(int abs_line) const {
    const std::vector<Cell>* r = nullptr;
    if (abs_line < int(hist_.size())) r = &hist_[size_t(abs_line)];
    else {
        const int s = abs_line - int(hist_.size());
        if (s >= 0 && s < rows) r = &grid_[size_t(s)];
    }
    if (!r) return std::string();
    std::string out;
    // 行尾空格不进剪贴板，但保留行首缩进。
    // 注意记的是"最后一个非空格字符的**结尾**下标"而不是起点：中文/日文一个字
    // 编成 3 字节，按起点 +1 截断会把行尾那个字切成半个（现象是复制的最后
    // 一个字变成乱码，探针里 find("こんにちは世界") 直接失败）。
    size_t keep = 0;
    for (int x = 0; x < int(r->size()); ++x) {
        const Cell& c = (*r)[size_t(x)];
        if (c.ch == 0) continue;                   // 全角右半格
        if (c.ch < 0x80) out.push_back(char(c.ch));
        else if (c.ch < 0x800) { out.push_back(char(0xC0 | (c.ch >> 6))); out.push_back(char(0x80 | (c.ch & 0x3F))); }
        else if (c.ch < 0x10000) { out.push_back(char(0xE0 | (c.ch >> 12))); out.push_back(char(0x80 | ((c.ch >> 6) & 0x3F))); out.push_back(char(0x80 | (c.ch & 0x3F))); }
        else { out.push_back(char(0xF0 | (c.ch >> 18))); out.push_back(char(0x80 | ((c.ch >> 12) & 0x3F))); out.push_back(char(0x80 | ((c.ch >> 6) & 0x3F))); out.push_back(char(0x80 | (c.ch & 0x3F))); }
        if (c.ch != ' ') keep = out.size();
    }
    out.resize(keep);
    return out;
}

inline void Terminal::sel_begin(int abs_line, int col) {
    sel_active_ = true;
    sel_a_line_ = abs_line;
    sel_a_col_ = col;
    sel_b_line_ = abs_line;
    sel_b_col_ = col;
}

inline void Terminal::sel_extend(int abs_line, int col) {
    if (!sel_active_) { sel_begin(abs_line, col); return; }
    sel_b_line_ = abs_line;
    sel_b_col_ = col;
}

inline std::string Terminal::selected_text() const {
    if (!sel_active_) return std::string();
    int l0 = sel_a_line_, c0 = sel_a_col_, l1 = sel_b_line_, c1 = sel_b_col_;
    if (l0 > l1 || (l0 == l1 && c0 > c1)) { std::swap(l0, l1); std::swap(c0, c1); }
    std::string out;
    for (int l = l0; l <= l1; ++l) {
        // 按列裁（列 → 字节，够用即可：终端里绝大多数是单字节字符）
        auto slice = [&](int from_col, int to_col) {
            // 用网格逐格取更准，这里走网格
            const std::vector<Cell>* r = nullptr;
            if (l < int(hist_.size())) r = &hist_[size_t(l)];
            else {
                const int si = l - int(hist_.size());
                if (si >= 0 && si < rows) r = &grid_[size_t(si)];
            }
            if (!r) return std::string();
            std::string o;
            const int x0 = std::max(0, from_col);
            const int x1 = std::min(int(r->size()) - 1, to_col < 0 ? int(r->size()) - 1 : to_col);
            for (int x = x0; x <= x1; ++x) {
                const Cell& c = (*r)[size_t(x)];
                if (c.ch == 0) continue;
                if (c.ch < 0x80) o.push_back(char(c.ch));
                else if (c.ch < 0x800) { o.push_back(char(0xC0 | (c.ch >> 6))); o.push_back(char(0x80 | (c.ch & 0x3F))); }
                else if (c.ch < 0x10000) { o.push_back(char(0xE0 | (c.ch >> 12))); o.push_back(char(0x80 | ((c.ch >> 6) & 0x3F))); o.push_back(char(0x80 | (c.ch & 0x3F))); }
                else { o.push_back(char(0xF0 | (c.ch >> 18))); o.push_back(char(0x80 | ((c.ch >> 12) & 0x3F))); o.push_back(char(0x80 | ((c.ch >> 6) & 0x3F))); o.push_back(char(0x80 | (c.ch & 0x3F))); }
            }
            return o;
        };
        std::string piece;
        if (l == l0 && l == l1) piece = slice(c0, c1);
        else if (l == l0) piece = slice(c0, -1);
        else if (l == l1) piece = slice(0, c1);
        else piece = slice(0, -1);
        // 行尾补的空格别复制出去（UTF-8 续字节都 >= 0x80，退到 0x20 停是安全的）
        while (!piece.empty() && piece.back() == ' ') piece.pop_back();
        out += piece;
        if (l != l1) out += "\n";
    }
    return out;
}

}  // namespace sshc
