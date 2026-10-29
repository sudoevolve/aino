"""开发用的本地 SSH 服务器（paramiko）：给 ssh_client 做端到端验证。

除了标准 SSH 登录、PTY shell、SFTP 子系统，它还提供几条**专门用来验终端解析器**
的命令：
    colors      16/256 色、粗体、下划线、反显
    unicode     CJK 全角对齐（中英混排、制表符框线）
    alt         备用屏进出（vim/top 走的就是这条路径）
    big         出 200 行，验回滚
    cursor      光标定位 + 清行/清屏
SFTP 的根目录是 --root 指定的临时目录，随便上传下载都不碰真实文件。

用法：
    python tools/dev_ssh_server.py --port 2222 --user demo --pass demo --root build/sshtest
"""
import argparse
import logging
import os
import socket
import sys
import threading
import time

import paramiko

CHANNEL_DATA = 0
KEY = None
ROOT = None
ENV_SEEN = {}          # 全局收集：哪个客户端发过哪些 env（验证 COLORTERM 用）
PTY_SIZE = {}          # 客户端申请的 pty 尺寸（验"最后一行会不会被裁掉"）


# ---------------------------------------------------------------- 会话接口
class Server(paramiko.ServerInterface):
    def __init__(self):
        self.event = threading.Event()
        self.env = {}                   # 客户端发来的 env 请求（COLORTERM / TERM ...）

    def check_channel_env_request(self, channel, name, value):
        # paramiko 默认实现直接返回 False 且不落库。验"客户端到底有没有把 COLORTERM
        # 送到远端"就只能靠这里记下来 —— 这件事没法从客户端侧断言。
        self.env[name] = value
        ENV_SEEN.update({name: value})
        return True

    def check_auth_password(self, username, password):
        if username == ARGS.user and password == ARGS.password:
            return paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED

    def get_allowed_auths(self, username):
        return "password,keyboard-interactive"

    def check_channel_request(self, kind, chanid):
        if kind == "session":
            return paramiko.OPEN_SUCCEEDED
        return paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    def check_channel_pty_request(self, channel, term, width, height, pixelwidth, pixelheight, modes):
        PTY_SIZE.update({"cols": width, "rows": height, "term": term})
        channel.term = term
        channel.cols = width
        channel.rows = height
        return True

    def check_channel_window_change_request(self, channel, width, height, pixelwidth, pixelheight):
        channel.cols = width
        channel.rows = height
        # 必须跟着更新：客户端跑起来以后是靠 window-change 把尺寸从 80x24 改到真实大小的，
        # 只记 pty-req 的话会永远看到 80x24 —— 那正是"界面 122x44、远端以为 80x24"的表现。
        PTY_SIZE.update({"cols": width, "rows": height})
        return True

    def check_channel_shell_request(self, channel):
        channel._aino_shell = True          # accept 循环靠它认出"这是 shell 通道"
        self.event.set()
        return True

    def check_channel_exec_request(self, channel, command):
        channel._aino_shell = True
        channel._aino_exec = command
        self.event.set()
        return True

    def check_channel_subsystem_request(self, channel, name):
        # ⚠️ 关键：paramiko **只有这个默认实现**才会把 transport 上注册的 subsystem
        # handler 实例化并 start()（见 paramiko/server.py ServerInterface）。
        # 一开始我在这里只 return True 表示"允许"，以为 handler 会自己跑 ——
        # 结果 SFTPServer 从来没启动，客户端读版本号永远等不到：
        #   libssh2: "Timeout waiting for response from SFTP subsystem"
        #   paramiko 客户端: 直接挂住
        #   GUI: 点一下远程目录，整个界面卡死（在 UI 线程上等 20 秒）
        if name != "sftp":
            return False
        if ARGS.no_sftp:
            # 模拟"服务器没开 SFTP"：客户端必须立刻失败但终端照常可用。
            # 这条路径以前会让界面卡 20 秒、终端一片空白，留着当回归用。
            return False
        transport = channel.get_transport()
        handler_class, args, kwargs = transport._get_subsystem_handler(name)
        if handler_class is None:
            return False
        channel._aino_subsys = name
        handler = handler_class(channel, name, self, *args, **kwargs)
        handler.start()
        self.event.set()
        return True


class _SftpServer(paramiko.SFTPServer):
    """给 SFTP 通道打标记。

    踩过的坑：一开始把标记写在 Server.check_channel_subsystem_request 里，
    但 transport 上注册了 subsystem handler 之后 paramiko 只调 handler、不调
    那个方法 —— 标记永远是 None，于是 accept 循环把 SFTP 通道当成 shell 通道，
    往里面写欢迎横幅。客户端那边的现象就非常迷惑：
      libssh2: "Timeout waiting for response from SFTP subsystem"（版本那 9 个字节读到的是 banner）
      paramiko: SFTPError: Garbage packet received
      自家客户端：点一下目录卡住，文件列表空白
    """

    def __init__(self, channel, name, server, sftp_si, **kwargs):
        channel._aino_subsys = name
        paramiko.SFTPServer.__init__(self, channel, name, server,
                                     sftp_si=sftp_si, **kwargs)


class _SftpIface(paramiko.SFTPServerInterface):
    """只允许在 ROOT 里面折腾（测试服务器也别真把整盘暴露出去）。"""

    def _safe(self, path):
        p = os.path.realpath(os.path.join(ROOT, path.lstrip("/\\")))
        if not p.startswith(os.path.realpath(ROOT)):
            return None
        return p

    def list_folder(self, path):
        p = self._safe(path)
        if not p:
            return paramiko.SFTP_PERMISSION_DENIED
        try:
            out = []
            for name in os.listdir(p):
                full = os.path.join(p, name)
                st = os.stat(full)
                attr = paramiko.SFTPAttributes.from_stat(st)
                attr.filename = name
                out.append(attr)
            return out
        except OSError as e:
            return paramiko.SFTPServer.convert_errno(e.errno)

    def stat(self, path):
        p = self._safe(path)
        if not p:
            return paramiko.SFTP_PERMISSION_DENIED
        try:
            return paramiko.SFTPAttributes.from_stat(os.stat(p))
        except OSError as e:
            return paramiko.SFTPServer.convert_errno(e.errno)

    def lstat(self, path):
        return self.stat(path)

    def open(self, path, flags, attr):
        p = self._safe(path)
        if not p:
            return paramiko.SFTP_PERMISSION_DENIED
        mode = "r+b" if os.path.exists(p) else "wb"
        if flags & os.O_APPEND:
            mode = "ab"
        try:
            f = open(p, mode)
        except OSError as e:
            return paramiko.SFTPServer.convert_errno(e.errno)
        fobj = paramiko.SFTPHandle(flags)
        fobj.filename = p
        fobj.readfile = f
        fobj.writefile = f
        return fobj

    def remove(self, path):
        p = self._safe(path)
        try:
            os.remove(p)
            return paramiko.SFTP_OK
        except OSError as e:
            return paramiko.SFTPServer.convert_errno(e.errno)

    def rename(self, oldpath, newpath):
        a, b = self._safe(oldpath), self._safe(newpath)
        try:
            os.replace(a, b)
            return paramiko.SFTP_OK
        except OSError as e:
            return paramiko.SFTPServer.convert_errno(e.errno)

    def mkdir(self, path, attr):
        p = self._safe(path)
        try:
            os.mkdir(p)
            return paramiko.SFTP_OK
        except OSError as e:
            return paramiko.SFTPServer.convert_errno(e.errno)

    def rmdir(self, path):
        p = self._safe(path)
        try:
            os.rmdir(p)
            return paramiko.SFTP_OK
        except OSError as e:
            return paramiko.SFTPServer.convert_errno(e.errno)

    def canonicalize(self, path):
        return "/" + os.path.relpath(self._safe(path) or ROOT, ROOT).replace("\\", "/").lstrip("./")


# ---------------------------------------------------------------- 假 shell
BANNER = (
    "\x1b[1;36maino SSH 测试会话\x1b[0m\r\n"
    "输入 help 看命令：colors / unicode / alt / big / cursor / replay <file> / clear / exit\r\n"
)


def cmd_colors(chan):
    s = ""
    s += "\x1b[1m16 色（前景）\x1b[0m\r\n"
    for i in range(8):
        s += f"\x1b[3{i}m██ {i}\x1b[0m "
    s += "\r\n"
    for i in range(8):
        s += f"\x1b[9{i}m██ {i + 8}\x1b[0m "
    s += "\r\n\r\n\x1b[1m256 色（每隔 13 取一个）\x1b[0m\r\n"
    for i in range(16, 256, 13):
        s += f"\x1b[48;5;{i}m  {i:<3} \x1b[0m"
    s += "\r\n\r\n\x1b[1m真彩色 + 属性\x1b[0m\r\n"
    s += "\x1b[38;2;255;120;40mR255 G120 B40\x1b[0m  "
    s += "\x1b[4m下划线\x1b[0m  \x1b[7m反显\x1b[0m  \x1b[3m斜体\x1b[0m\r\n"
    chan.sendall(s.encode())


def _disp_width(s):
    """按东亚宽度算显示列数（全角 2 列）。"""
    import unicodedata
    return sum(2 if unicodedata.east_asian_width(c) in "WF" else 1 for c in s)


def _pad_to(s, width):
    """按**显示宽度**补空格。

    别用 f"{s:<10}"：python 的格式宽度数的是字符数，"中文"算 2 个字符、
    但在终端里占 4 列 —— 补出来的框线就歪了，看着像终端没对齐。
    """
    return s + " " * max(0, width - _disp_width(s))


def cmd_unicode(chan):
    rows = [
        ("纯 ASCII", "hello world 12345"),
        ("中文", "你好，世界！SSH 终端"),
        ("混排", "文件: 报告_2026.txt 大小 1.2MB"),
        ("日文", "こんにちは世界"),
        ("韩文", "안녕하세요"),
        ("emoji", "状态: ✅ 🚀 🔧"),
    ]
    # 框线宽度必须和数据行严格对上，不然"右边界不直"是数据写错了，不是终端的问题。
    # 数据行 "│ " + 10 + " │ " + 40 + "│" 的列布局：
    #   0=│  1=空格  2..11=名字  12=空格  13=│  14=空格  15..54=值  55=│
    name_w, val_w = 10, 40
    s = "\x1b[1m全角对齐检查（右边界应当是一条直线）\x1b[0m\r\n"
    s += "┌" + "─" * (name_w + 2) + "┬" + "─" * (val_w + 1) + "┐\r\n"
    for k, v in rows:
        # 两列都按显示宽度补空格（全角算 2），列才对得上
        s += f"│ {_pad_to(k, name_w)} │ {_pad_to(v, val_w)}│\r\n"
    s += "└" + "─" * (name_w + 2) + "┴" + "─" * (val_w + 1) + "┘\r\n"
    chan.sendall(s.encode())


def cmd_alt(chan):
    chan.sendall(b"\x1b[?1049h\x1b[2J\x1b[H")
    chan.sendall("这是备用屏。\r\n按任意键返回之前的画面……\r\n".encode())
    # 等一个字节
    chan.recv(1)
    chan.sendall(b"\x1b[?1049l")


def cmd_big(chan):
    for i in range(200):
        chan.sendall(f"第 {i + 1:03d} 行：滚动缓冲测试 the quick brown fox jumps over the lazy dog\r\n".encode())


def cmd_cursor(chan):
    s = "\x1b[2J\x1b[H"
    s += "光标定位：(5,10) 写 A，(10,20) 写 B\r\n"
    s += "\x1b[5;10HA" + "\x1b[10;20HB"
    s += "\x1b[13;1H" + "\x1b[K" + "这一行先写满再被 K 清掉→"
    chan.sendall(s.encode())


def key_echo_loop(chan):
    """按键回显模式：远端把收到的每个按键序列回显成一个可读 token。

    为什么要它：“按住方向键要连发”这件事只能靠**数次数**来验，截图看不出来。
    原来靠远端 `cat -v` + `stty -icanon`，但默认终端是 canonical 模式，0x7f 会被
    行规程当 ERASE 吃掉，测退格永远是 0 次 —— 那是测试环境的问题，不是客户端的。
    这里直接吃掉原始字节，一个序列打一个 token，谁都赖不掉。
    """
    chan.sendall("\x1b[2J\x1b[HKEY-ECHO: 每收到一个按键就打一个 token\r\n".encode())
    buf = b""
    while True:
        try:
            d = chan.recv(1024)
        except Exception:
            return
        if not d:
            return
        buf += d
        while buf:
            name, used = _parse_one_key(buf)
            if used == 0:
                break                       # 半个序列，等后续字节
            buf = buf[used:]
            chan.sendall(("K:%s\r\n" % name).encode())


_KEYS = {
    b"\x1b[A": "UP", b"\x1bOA": "UP",
    b"\x1b[B": "DOWN", b"\x1bOB": "DOWN",
    b"\x1b[C": "RIGHT", b"\x1bOC": "RIGHT",
    b"\x1b[D": "LEFT", b"\x1bOD": "LEFT",
    b"\x1b[H": "HOME", b"\x1bOH": "HOME", b"\x1b[1~": "HOME",
    b"\x1b[F": "END", b"\x1bOF": "END", b"\x1b[4~": "END",
    b"\x1b[3~": "DEL", b"\x1b[5~": "PGUP", b"\x1b[6~": "PGDN",
    b"\x7f": "BS", b"\x08": "BS",
    b"\r": "ENTER", b"\t": "TAB", b"\x1b": "ESC",
}


def _parse_one_key(buf):
    """从 buf 头部取一个按键，返回 (名字, 消耗字节数)；数据不够返回 ("", 0)。"""
    for seq, name in _KEYS.items():
        if buf.startswith(seq):
            # 前缀更长的优先（\\x1b 是 \\x1b[A 的前缀，必须等够字节再判）
            longer = [s for s in _KEYS if len(s) > len(seq) and s.startswith(seq)]
            if any(len(buf) < len(s) and s.startswith(buf) for s in longer):
                return "", 0
            return name, len(seq)
    if buf[:1] == b"\x1b":
        # 未知转义：等 CSI 的终止字节（0x40..0x7e）再整段吃掉
        for i, b in enumerate(buf[1:], start=1):
            if 0x40 <= b <= 0x7E:
                return "ESC?%r" % buf[:i + 1], i + 1
        if len(buf) < 8:
            return "", 0
        return "ESC?%r" % buf[:1], 1
    b = buf[0]
    if b == 0x03:
        return "CTRL-C", 1
    if 32 <= b < 127:
        return chr(b), 1
    return "X%02X" % b, 1


def cmd_lastrow(chan):
    """把标记打到 **pty 的最后一行**，用来验底部是否被裁。

    为什么要专门测：终端窗口最后一行最容易出问题 —— 行数按像素算多算一行，
    那一行就被窗口下沿切掉一半，而"被切掉一半"的字符在截图里很像"没显示"。
    helix 的命令行提示、vim 的 : 提示都正好在这最后一行。
    """
    rows = PTY_SIZE.get("rows", 24)
    cols = PTY_SIZE.get("cols", 80)
    chan.sendall(b"\x1b[2J\x1b[H")
    for r in (1, rows // 2, rows - 1, rows):
        chan.sendall(("\x1b[%d;1Hrow %d/%d" % (r, r, rows)).encode())
    # 最后一行用高对比底色铺满，边角有没有被切一眼看出
    chan.sendall(("\x1b[%d;1H\x1b[41;97m" % rows).encode())
    chan.sendall(("LAST-ROW %d/%d " % (rows, rows)).encode() + b"-" * max(0, cols - 20))
    chan.sendall(b"\x1b[0m")
    chan.sendall(("\x1b[%d;1H" % rows).encode())


def cmd_env(chan):
    """打印本次连接里客户端发来的环境变量请求。"""
    if not ENV_SEEN:
        chan.sendall("(客户端没有发任何 env 请求)\r\n".encode())
        return
    for k in sorted(ENV_SEEN):
        # paramiko 给的是 bytes，直接 %s 会打出 b'COLORTERM' 这种壳，看着像 bug
        kk = k.decode() if isinstance(k, bytes) else k
        vv = ENV_SEEN[k]
        vv = vv.decode() if isinstance(vv, bytes) else vv
        chan.sendall(("%s=%s\r\n" % (kk, vv)).encode())


def cmd_replay(chan, path):
    """把一段**真实抓下来的字节流**原样灌给客户端。

    这是复现线上渲染问题最靠谱的办法：远程那台机器不一定随时能连，
    但只要当初存了 raw bin，就能在本地把一模一样的画面重新放出来。
    分块发送（而不是一次性 sendall）是为了贴近真实链路的分包行为 ——
    跨包断在转义序列中间，本身就是一类 bug 的成因。
    """
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        chan.sendall(f"replay 打不开 {path}: {e}\r\n".encode())
        return
    chan.sendall(b"\x1b[2J\x1b[H")
    for i in range(0, len(data), 512):
        chan.sendall(data[i:i + 512])


def shell_loop(chan):
    buf = ""
    chan.sendall(BANNER.encode())
    prompt = "\x1b[32m$\x1b[0m "
    chan.sendall(prompt.encode())
    while True:
        try:
            data = chan.recv(4096)
        except Exception:
            return
        if not data:
            return
        text = data.decode("utf-8", "replace")
        for ch in text:
            if ch == "\r" or ch == "\n":
                chan.sendall(b"\r\n")
                cmd = buf.strip()
                buf = ""
                if cmd in ("exit", "quit", "logout"):
                    chan.sendall("再见\r\n".encode())
                    chan.close()
                    return
                elif cmd == "help":
                    chan.sendall("colors  unicode  alt  big  cursor  keys  env  lastrow  replay <file>  clear  exit\r\n".encode())
                elif cmd == "colors":
                    cmd_colors(chan)
                elif cmd == "unicode":
                    cmd_unicode(chan)
                elif cmd == "alt":
                    cmd_alt(chan)
                elif cmd == "big":
                    cmd_big(chan)
                elif cmd == "cursor":
                    cmd_cursor(chan)
                elif cmd == "lastrow":
                    cmd_lastrow(chan)
                    continue                    # 同上，别把提示符糊到最后一行上
                elif cmd == "env":
                    cmd_env(chan)
                elif cmd.startswith("replay "):
                    cmd_replay(chan, cmd[7:].strip())
                    continue                    # replay 自己画完了，别再补一个提示符
                elif cmd == "keys":
                    key_echo_loop(chan)
                    return
                elif cmd == "clear":
                    chan.sendall(b"\x1b[2J\x1b[H")
                elif cmd == "pwd":
                    chan.sendall((ROOT.replace("\\", "/") + "\r\n").encode())
                elif cmd == "ls":
                    chan.sendall(("\r\n".join(sorted(os.listdir(ROOT))) + "\r\n").encode())
                elif cmd:
                    chan.sendall(f"未知命令：{cmd}\r\n".encode())
                chan.sendall(prompt.encode())
            elif ch in ("\x7f", "\b"):
                if buf:
                    buf = buf[:-1]
                    chan.sendall(b"\b \b")
            elif ch == "\x03":
                buf = ""
                chan.sendall(b"^C\r\n")
                chan.sendall(prompt.encode())
            elif ch >= " ":
                buf += ch
                chan.sendall(ch.encode())


def serve_shell(chan):
    """一个 shell 通道的业务线程。"""
    try:
        shell_loop(chan)
    except Exception as e:
        print("shell 异常:", e)
    finally:
        try:
            chan.close()
        except Exception:
            pass


def handle_client(client):
    transport = paramiko.Transport(client)
    transport.add_server_key(KEY)
    transport.set_subsystem_handler("sftp", _SftpServer, sftp_si=_SftpIface)
    server = Server()
    try:
        transport.start_server(server=server)
    except Exception as e:
        print("握手失败:", e)
        return
    # 一条连接上通常会开**两个**通道：一个 shell、一个 sftp。必须反复 accept，
    # 否则第二个通道永远没人应答（客户端表现：libssh2_sftp_init 干等，界面卡死）。
    while transport.is_active():
        try:
            chan = transport.accept(1.0)
        except Exception:
            break
        if chan is None:
            continue
        # 通道是 CHANNEL_OPEN 一落地就 accept 出来的，客户端的 shell/subsystem
        # 请求要晚一个包才到。这里等一下再判类型 —— 判早了就会把 banner 写进
        # SFTP 通道（见 _SftpServer 的注释）。
        for _ in range(200):                       # 最多 2 秒
            if getattr(chan, "_aino_subsys", None) or getattr(chan, "_aino_shell", False):
                break
            if chan.closed:
                break
            time.sleep(0.01)
        if getattr(chan, "_aino_subsys", None):
            # paramiko 已经为这个通道起好了 SFTPServer 线程，这里别再碰它，
            # 否则两边同时读写同一个 channel 会互相踩。
            continue
        if getattr(chan, "_aino_shell", False) or not chan.closed:
            threading.Thread(target=serve_shell, args=(chan,), daemon=True).start()


def main():
    global ARGS, KEY, ROOT
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=2222)
    ap.add_argument("--user", default="demo")
    ap.add_argument("--pass", dest="password", default="demo")
    ap.add_argument("--root", default="build/sshtest")
    ap.add_argument("--no-sftp", action="store_true",
                    help="拒绝 sftp 子系统（回归用：验客户端降级是否干净）")
    ARGS = ap.parse_args()
    if os.environ.get("SSHDEBUG"):
        logging.basicConfig(filename="build/sshtest_debug.log", level=logging.DEBUG,
                            format="%(asctime)s %(message)s")
    ROOT = os.path.abspath(ARGS.root)
    os.makedirs(ROOT, exist_ok=True)

    keyfile = os.path.join(ROOT, "_host_rsa")
    if os.path.exists(keyfile):
        KEY = paramiko.RSAKey(filename=keyfile)
    else:
        KEY = paramiko.RSAKey.generate(2048)
        KEY.write_private_key_file(keyfile)
    print(f"ROOT = {ROOT}")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.1", ARGS.port))
    sock.listen(16)
    print(f"SSH 测试服务器已启动: 127.0.0.1:{ARGS.port}  用户 {ARGS.user} / {ARGS.password}")
    sys.stdout.flush()
    while True:
        client, addr = sock.accept()
        threading.Thread(target=handle_client, args=(client,), daemon=True).start()


if __name__ == "__main__":
    main()
