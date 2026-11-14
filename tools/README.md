# 开发与验证工具

核心库和普通示例不依赖 Python。优先使用仓库根目录的 CMake 构建；
工具脚本按用途运行，避免将所有历史排障程序视为同一种测试。

| 类别 | 入口 | 说明 |
| --- | --- | --- |
| 构建 | `build.py`、`cl_one.py`、`msvc_build.py`、`toolchain.py` | Windows 辅助入口；需自行安装编译器；`AINO_GXX` 可显式指定 g++ |
| 功能回归 | `regress.py`、`ci_smoke.cpp`、`ci_pixels.cpp` | Python 回归入口主要面向 Windows；CI 冒烟及像素程序支持三个后端 |
| 布局/截图 | `verify_gallery.py`、`verify_render.py` | Windows 桌面及实际像素验证；不能用测量结果替代截图 |
| 性能 | `perf_gallery.py`、`perf_bench.py` | 进程 CPU、内存和真实交互采样；测量口径见 `docs/10-performance.md` |
| 专项探针 | `probe/` | 自动断言、复现和手工网络诊断并存；网络/GUI 探针需要相应环境 |

Python 脚本建议使用 Python 3.10+。进程采样额外安装 `python -m pip install -r tools/requirements.txt`；
其它平台 API 由脚本按用途调用，不能据此假设脚本跨平台可运行。

新增或修改代码遵循根目录 `.editorconfig` 和 `.clang-format`；
格式化仅限实际修改的代码段，不执行全仓重排。UI 开发还须遵循根目录 `AGENTS.md`。
`build/` 下的探针输出、截图和日志均为本地产物；确认程序有无独立验证价值后再清理源码。
