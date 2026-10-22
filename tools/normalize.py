"""编码归一化：.hpp/.cpp 带 UTF-8 BOM，所有源码/文档/脚本纯 CRLF。

Write / Edit 工具写出的是 LF（且不写 BOM），MSVC 会按 GBK 读中文注释。
每次改完跑一遍这个脚本，比记着"手写工具会改坏"可靠。
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOM = b'\xef\xbb\xbf'
NEED_BOM = ('.hpp', '.cpp')
TEXT = ('.hpp', '.cpp', '.md', '.py', '.txt')


def fix(path):
    ext = os.path.splitext(path)[1].lower()
    if ext not in TEXT:
        return None
    raw = open(path, 'rb').read()
    had_bom = raw.startswith(BOM)
    body = raw[len(BOM):] if had_bom else raw
    try:
        txt = body.decode('utf-8')
    except UnicodeDecodeError:
        return '%s 不是合法 UTF-8，跳过' % path
    crlf = txt.count('\r\n')
    txt = txt.replace('\r\n', '\n').replace('\r', '\n')
    if ext in NEED_BOM:
        out = BOM + txt.replace('\n', '\r\n').encode('utf-8')
    elif had_bom:
        # 非 C++ 文件不强制要 BOM，但原来有的别剥掉 —— 剥掉就是一条无意义的 diff
        out = BOM + txt.replace('\n', '\r\n').encode('utf-8')
    else:
        out = txt.replace('\n', '\r\n').encode('utf-8')
    if out == raw:
        return None
    open(path, 'wb').write(out)
    return '%s  %s -> CRLF%s' % (os.path.relpath(path, ROOT),
                                 'CRLF' if crlf else 'LF',
                                 ' +BOM' if ext in NEED_BOM and not had_bom else '')


def main():
    args = sys.argv[1:]
    files = []
    if args:
        files = [a if os.path.isabs(a) else os.path.join(ROOT, a) for a in args]
    else:
        for d, _, fs in os.walk(ROOT):
            if any(p in d for p in ('.git', 'build', '_aino_old')):
                continue
            files += [os.path.join(d, f) for f in fs]
    notes = [n for n in (fix(f) for f in files) if n]
    for n in notes:
        print(n)
    print('归一化 %d 个文件' % len(notes))
    # 复核：.hpp/.cpp 必须 BOM + 全 CRLF
    bad = []
    for f in files:
        ext = os.path.splitext(f)[1].lower()
        if ext not in TEXT:
            continue
        b = open(f, 'rb').read()
        if ext in NEED_BOM and not b.startswith(BOM):
            bad.append(f + '（缺 BOM）')
        if b.count(b'\n') != b.count(b'\r\n'):
            bad.append(f + '（混行尾）')
    if bad:
        print('仍有问题：')
        for x in bad:
            print('  ' + x)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
