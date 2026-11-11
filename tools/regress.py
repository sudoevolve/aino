"""按需运行功能回归；默认执行核心交互检查，性能使用 perf_gallery.py。"""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PROBE = os.path.join(HERE, "probe")
PY = sys.executable
results = []


def run(cmd, cwd=None, env=None, timeout=300):
    return subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True, encoding="utf-8",
                          errors="replace", timeout=timeout)


def step(name, ok, detail=""):
    skip = detail.startswith("SKIP:")
    results.append((name, True if skip else ok, detail))
    mark = "SKIP" if skip else ("OK  " if ok else "FAIL")
    print("[%s] %-34s %s" % (mark, name, detail))
    return ok


def build():
    r = run([PY, os.path.join(HERE, "build.py")])
    return r.returncode == 0, (r.stdout or r.stderr)[-400:]


def build_probe(name):
    source = Path(PROBE) / (name + ".cpp")
    exe = source.with_suffix(".exe")
    dependencies = {Path(HERE) / script for script in ("cl_one.py", "msvc_build.py", "toolchain.py")}
    pending = [source]
    while pending:
        path = pending.pop().resolve()
        if path in dependencies:
            continue
        dependencies.add(path)
        for include in re.findall(r'^\s*#\s*include\s*"([^"]+)"',
                                  path.read_text(encoding="utf-8-sig"), re.M):
            candidate = (path.parent / include).resolve()
            if candidate.is_file():
                pending.append(candidate)
    if exe.is_file() and exe.stat().st_mtime >= max(p.stat().st_mtime for p in dependencies):
        return True, "已是最新"
    r = run([PY, str(Path(HERE) / "cl_one.py"), str(source), str(exe),
             "/utf-8", "user32.lib", "gdi32.lib", "msimg32.lib", "imm32.lib"], cwd=PROBE)
    return r.returncode == 0, ((r.stdout or "") + (r.stderr or ""))[-400:]


def run_probe(name, want_tail):
    exe = os.path.join(PROBE, name + ".exe")
    r = run([exe], cwd=PROBE)
    out = (r.stdout or "") + (r.stderr or "")
    ok = r.returncode == 0 and want_tail in out
    tail = [l for l in out.strip().splitlines() if l.strip()][-1:] or [""]
    return ok, tail[0][:90]


def ci_pixels(no_build=False):
    if not no_build:
        r = run([PY, os.path.join(HERE, "cl_one.py"),
                 os.path.join(ROOT, "tools", "ci_pixels.cpp"),
                 os.path.join(ROOT, "build", "ci_pixels.exe"),
                 "/utf-8", "user32.lib", "gdi32.lib", "msimg32.lib"], cwd=ROOT)
        if r.returncode != 0:
            return False, "编译失败: " + ((r.stdout or "") + (r.stderr or ""))[-160:]
    exe = os.path.join(ROOT, "build", "ci_pixels.exe")
    r = run([exe], cwd=ROOT)
    out = (r.stdout or "") + (r.stderr or "")
    fails = [l.strip() for l in out.splitlines() if "[ FAIL ]" in l]
    if fails:
        return False, fails[0][:150]
    tail = [l for l in out.splitlines() if l.startswith("===")][-1:] or [""]
    return r.returncode == 0, tail[0][:60]


def verify_gallery(extra=()):
    cmd = [PY, os.path.join(HERE, "verify_gallery.py")] + list(extra)
    r = run(cmd)
    out = (r.stdout or "") + (r.stderr or "")
    fails = [l.strip() for l in out.splitlines() if "[FAIL]" in l]
    if fails:
        return False, " | ".join(fails)[:160]
    line = [l for l in out.splitlines() if "共" in l and "失败" in l]
    return (r.returncode == 0), (line[0].strip() if line else out.strip()[-80:])


PROBES = (
    ('gallery_layout_probe', 'verdict: PASS', '画廊布局报告：跨平台测量 / 越界 / 重叠 / CI 格式'),
    ('pomodoro_probe', '26 passed, 0 failed', '番茄钟：实际时钟 / 暂停续计 / 模式导航 / 时长 / 外观'),
    ('example_ui_probe', '57 passed, 0 failed', '函数示例：侧栏裁剪 / 采样缓存 / 无效公式 / 输入快捷键'),
    ('macos_gallery_probe', 'verdict: PASS', 'macOS 主题联动 / 实际示例交互 / 响应式布局'),
    ('layoutprobe', '通过 27 项，失败 0 项', 'Stack 布局计算'),
    ('newcomp_probe', '87 passed, 0 failed', '新增组件行为 + 像素'),
    ('raster_equiv', '通过 1602 项，失败 0 项', '光栅化 vs 参考实现'),
    ('shadow_text_probe', '通过 24 项，失败 0 项', '阴影衰减 + 文本换行/裁剪/量高'),
    ('arc_probe', '通过 10 项，失败 0 项', '圆弧无接缝 + 渐变 + 端帽'),
    ('hover_anim_probe', '[hover] verdict: PASS', 'spinner/skeleton 只悬停才动'),
    ('ime_probe', '通过 6 项，失败 0 项', 'IME 组合窗/候选窗定位'),
    ('bigfeat_probe', '通过 47 项，失败 0 项', '折行缓存/滚动/撤销/补间/变换'),
    ('edit_probe', '通过 12 项，失败 0 项', '编辑器按键链路（撤销/重做/选区）'),
    ('resize_probe', '通过 19 项，失败 0 项', '缩放后缓冲一致（无黑帧/无拉伸）'),
    ('overlay_probe', '通过 37 项，失败 0 项', '浮层不穿透（点击/hover/光标/滚轮/键盘/层级）'),
    ('gate_order_probe', 'PASS（通过 8 项，失败 0 项）', '浮层层级：后画的控件不得穿透弹层'),
    ('clip_probe', '[clip] verdict: PASS', '剪贴板 C/X/V（不崩 + 复制/剪切/粘贴语义）'),
    ('scrollbar_probe', '[scrollbar] verdict: PASS', '滚动条拖动/点轨道（不再是纯装饰）'),
    ('ime_comp_probe', '[imecomp] verdict: PASS', 'IME 组字串画进输入框（不是只在组合窗）'),
    ('widget_probe', '通过 79 项，失败 0 项', '0.3.x 新控件行为（Toggle/OTP/Combobox/分页/月历/浮层/分割/菜单栏）'),
    ('interaction_probe', '通过 94 项，失败 0 项', '存量控件交互（含页签关闭 / 溢出翻页）'),
    ('measure_probe', '通过 9 项，失败 0 项', '测量模式不落像素 + 嵌套滚动归属'),
    ('click_probe', '通过 11 项，失败 0 项', '点击归属（裁剪外不吃点击 / 一次按下只归一个）'),
    ('cfg_probe', '通过 16 项，失败 0 项', 'WindowConfig（字体名/尺寸约束/居中/可缩放）'),
    ('hover_fade_probe', '通过 14 项，失败 0 项', 'hover 淡入淡出无暗闪（透明底只推 alpha）'),
)

CORE = {"macos_gallery_probe", "interaction_probe", "widget_probe", "overlay_probe",
        "click_probe", "scrollbar_probe", "hover_fade_probe"}


def child_window_probe():
    r = run([PY, str(Path(HERE) / "child_window.py")], timeout=90)
    out = ((r.stdout or "") + (r.stderr or "")).strip()
    return r.returncode == 0, out.splitlines()[-1] if out else "无输出"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    group = ap.add_mutually_exclusive_group()
    group.add_argument("--full", action="store_true", help="全部探针及实窗检查")
    group.add_argument("--only", help="逗号分隔的探针名称")
    ap.add_argument("--no-build", action="store_true", help="直接使用已有产物")
    ap.add_argument("--list", action="store_true", help="列出可选探针")
    args = ap.parse_args()
    results.clear()
    if args.list:
        for name, _, label in PROBES:
            print(name + ": " + label)
        return 0
    wanted = set(args.only.split(",")) if args.only else (set(n for n, _, _ in PROBES) if args.full else CORE)
    unknown = wanted - {n for n, _, _ in PROBES}
    if unknown:
        ap.error("未知探针: " + ", ".join(sorted(unknown)))
    selected = [p for p in PROBES if p[0] in wanted]
    if args.full and not args.no_build:
        ok, detail = build()
        step("构建画廊", ok, detail if not ok else "")
        if not ok:
            return 1
    for name, tail, label in selected:
        if not args.no_build:
            ok, detail = build_probe(name)
            if not ok:
                step("编译 " + name, False, detail)
                continue
        ok, detail = run_probe(name, tail)
        step(label, ok, detail)
    if args.full:
        for name, check in (("画廊实窗", verify_gallery),
                            ("跨平台图元", lambda: ci_pixels(args.no_build)),
                            ("子窗口", child_window_probe)):
            ok, detail = check()
            step(name, ok, detail)
    failures = sum(not ok for _, ok, _ in results)
    print("\n共 %d 项，失败 %d 项" % (len(results), failures))
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
