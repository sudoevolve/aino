"""读出对端 sshd 在 KEXINIT 里广播的算法清单。

为什么需要它：握手报 "Unable to exchange encryption keys" 时，只有把
"服务器支持什么" 和 "本地 libssh2 支持什么" 摊开对照，才知道交集是不是空的。
光靠猜算法名一试是试不完的（curve25519 / ecdh / group14 / gex 全撞墙）。

用法：
    python tools/probe/ssh_algos.py 192.168.12.131 22
"""
import socket
import struct
import sys

NAMES = ["kex", "hostkey", "enc_c2s", "enc_s2c", "mac_c2s", "mac_s2c",
         "comp_c2s", "comp_s2c", "lang_c2s", "lang_s2c"]


def read_n(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError("connection closed")
        buf += chunk
    return buf


def read_line(sock):
    buf = b""
    while not buf.endswith(b"\n"):
        c = sock.recv(1)
        if not c:
            raise EOFError("connection closed")
        buf += c
    return buf


def parse_namelist(payload, off):
    (ln,) = struct.unpack(">I", payload[off:off + 4])
    off += 4
    raw = payload[off:off + ln].decode("utf-8", "replace")
    off += ln
    return (raw.split(",") if raw else []), off


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 22

    s = socket.create_connection((host, port), timeout=8)
    banner = read_line(s)
    print("server banner:", banner.decode("utf-8", "replace").strip())
    s.sendall(b"SSH-2.0-aino_probe\r\n")

    # 第一个包就是 KEXINIT（明文）
    hdr = read_n(s, 5)
    plen = struct.unpack(">I", hdr[:4])[0]
    pad = hdr[4]
    payload = read_n(s, plen - 1)          # plen 含 padding_length 这一个字节
    body = payload
    if body[0] != 20:
        print("不是 KEXINIT，msg =", body[0])
        return
    off = 1 + 16                            # msg type + cookie
    print()
    for name in NAMES:
        vals, off = parse_namelist(body, off)
        print(f"{name:9s} ({len(vals)}):")
        for v in vals:
            print("   ", v)
    s.close()


if __name__ == "__main__":
    main()
