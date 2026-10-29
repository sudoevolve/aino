"""把 term_hx.exe 抓到的原始字节流用 pyte（参考实现）回放，再和自家解析器逐行比对。

存在的意义：hx 花屏/少画这种事，靠肉眼瞄截图只能看到"不对"，看不到"哪一行不对"。
两份屏幕快照一对齐，差异区间就直接定位到某个 CSI/SGR 处理分支。

用法：
    python tools/probe/hx_diff.py                 # 比较 build/hx_raw.bin 与 build/hx_ours.txt
    python tools/probe/hx_diff.py --show-raw      # 顺便把原始流里的控制序列打出来
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def replay_pyte(data, cols, rows):
    import pyte
    screen = pyte.Screen(cols, rows)
    stream = pyte.ByteStream(screen)
    stream.feed(data)
    return ["".join(screen.buffer[y][x].data for x in range(cols)).rstrip() for y in range(rows)], screen


def parse_ours(path):
    rows = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"^\[(\d\d)\] \|(.*)\|$", line.rstrip("\n"))
        if m:
            rows[int(m.group(1))] = m.group(2).rstrip()
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", default=os.path.join(ROOT, "build", "hx_raw.bin"))
    ap.add_argument("--ours", default=os.path.join(ROOT, "build", "hx_ours.txt"))
    ap.add_argument("--cols", type=int, default=150)
    ap.add_argument("--rows", type=int, default=45)
    ap.add_argument("--show-raw", action="store_true")
    a = ap.parse_args()

    data = open(a.raw, "rb").read()
    print("raw bytes:", len(data))
    ref, screen = replay_pyte(data, a.cols, a.rows)
    ours = parse_ours(a.ours)

    print("\n=== pyte (参考实现) 屏幕 ===")
    for y, s in enumerate(ref):
        if s.strip() or y >= a.rows - 4:
            print("[%02d] |%s|" % (y, s))

    print("\n=== pyte 光标 / 模式 ===")
    print("cursor:", screen.cursor.x, screen.cursor.y, "alt:", getattr(screen, "alternate", None))

    if a.show_raw:
        print("\n=== 原始流里的非打印序列（前 200 条）===")
        seqs = re.findall(rb"\x1b\[[0-9;?]*[a-zA-Z]|\x1b[()][B0]|\x1b[=>78]", data)
        for s in seqs[:200]:
            print(repr(s))

    # 只比对"pyte 认为有内容"的行，避免双方空白行表示差异导致噪音
    print("\n=== 差异（pyte 有内容 / 我方有内容）===")
    bad = 0
    for y in range(a.rows):
        r = ref[y].strip()
        o = ours.get(y, "").strip()
        if r == o:
            continue
        if not r and not o:
            continue
        bad += 1
        print("[%02d] pyte=%r" % (y, r[:120]))
        print("     ours=%r" % (o[:120],))
    if bad == 0:
        print("(无差异)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
