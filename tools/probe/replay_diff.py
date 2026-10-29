# 用 pyte 当参考实现，二分出"自家解析器从哪一字节开始跑偏"。
#
#   python tools/probe/replay_diff.py <bin> <cols> <rows>
#
# 为什么要这个：hx 在 GUI 里只画出两行，肉眼分不清是"远端没发"还是"我们解析丢了"。
# pyte 是成熟实现，两边喂同一段字节，谁先跑偏、偏在哪条转义序列，一目了然。
import re
import subprocess
import sys

import pyte

EXE = r"C:\Users\admin\Documents\GitHub\aino\tools\probe\term_replay.exe"
ROW_RE = re.compile(r"R(\d+) [^ ]+ [^ ]+ \d+ \|(.*)\|$")


def ours_at(bin_path, cols, rows, n):
    """喂前 n 字节，返回屏幕各行文本。"""
    p = subprocess.run([EXE, bin_path, str(cols), str(rows), str(max(1, n)), str(n)],
                       capture_output=True, text=True, errors="replace")
    if p.returncode != 0:
        raise SystemExit("replay failed: " + p.stdout + p.stderr)
    cur = {}
    for line in p.stdout.splitlines():
        if line.startswith("### "):
            cur = {}                       # 只看最后一个检查点，不能跨检查点累积
            continue
        m = ROW_RE.match(line)
        if m:
            cur[int(m.group(1))] = m.group(2).rstrip()
    return [cur.get(i, "") for i in range(rows)]


def ref_at(data, cols, rows, n):
    sc = pyte.Screen(cols, rows)
    pyte.ByteStream(sc).feed(data[:n])
    return [sc.display[i].rstrip() for i in range(rows)]


def main():
    bin_path, cols, rows = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    data = open(bin_path, "rb").read()
    print("字节数", len(data), "屏幕", f"{cols}x{rows}")

    prev_ok = 0
    first_bad = None
    # 粗扫：每 8 字节一个检查点，找第一个不一致的位置
    for n in range(8, len(data) + 1, 8):
        n = min(n, len(data))
        a, b = ours_at(bin_path, cols, rows, n), ref_at(data, cols, rows, n)
        if a != b:
            first_bad = n
            break
        prev_ok = n
    if first_bad is None:
        print("全程一致（%d 个检查点）" % (len(data) // 8 + 1))
        return
    print(f"粗扫命中 n={first_bad}（上次一致 n={prev_ok}），开始逐字节细扫")

    # 细扫：从 prev_ok 起逐字节
    for n in range(prev_ok, first_bad + 1):
        a = ours_at(bin_path, cols, rows, n)
        b = ref_at(data, cols, rows, n)
        if a != b:
            print(f"\n>>> 首次跑偏：喂到第 {n} 字节")
            print("新增字节:", repr(data[prev_ok:n]))
            for i, (x, y) in enumerate(zip(a, b)):
                if x != y:
                    print(f"  row{i:02d} 我方={x!r}")
                    print(f"         pyte={y!r}")
            print("\n上下文:", repr(data[max(0, n - 80):n + 40]))
            return
        prev_ok = n
    print("细扫没复现（差异只在粗扫的采样点上，可能是检查点对齐问题）")


if __name__ == "__main__":
    main()
