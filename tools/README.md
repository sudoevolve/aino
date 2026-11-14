# 开发与验证工具

核心库和普通示例不依赖 Python。优先使用仓库根目录的 CMake 构建；
工具脚本按用途运行，避免将所有历史排障程序视为同一种测试。

核心回归的统一入口为 `-DAINO_BUILD_TESTS=ON` 和 CTest，用法见 `docs/01-quickstart.md`。
多编译单元链接探针需由 CMake 同时编译实现与调用者，不能作为单文件用 `cl_one.py` 构建。

| 类别 | 入口 | 说明 |
| --- | --- | --- |
| 构建 | `build.py`、`cl_one.py`、`msvc_build.py`、`toolchain.py` | Windows 辅助入口；需自行安装编译器；`AINO_GXX` 可显式指定 g++ |
| 功能回归 | `regress.py`、`ci_smoke.cpp`、`ci_pixels.cpp` | Python 回归入口主要面向 Windows；CI 冒烟及像素程序支持三个后端 |
| 布局/截图 | `verify_gallery.py`、`verify_render.py` | Windows 桌面及实际像素验证；不能用测量结果替代截图 |
| 性能 | `perf_gallery.py`、`perf_bench.py` | 进程 CPU、内存和真实交互采样；测量口径见 `docs/10-performance.md` |
| 专项探针 | `probe/` | 自动断言、复现和手工网络诊断并存；网络/GUI 探针需要相应环境 |
| 合成等价性 | `probe/shadow_mask_probe.cpp`、`probe/icon_mask_probe.cpp` | 投影掩膜缓存（≤1 LSB）与图标整行合成（逐字节）的前后对照，已进 CTest |

Python 脚本建议使用 Python 3.10+。进程采样额外安装 `python -m pip install -r tools/requirements.txt`；
其它平台 API 由脚本按用途调用，不能据此假设脚本跨平台可运行。

新增或修改代码遵循根目录 `.editorconfig` 和 `.clang-format`；
格式化仅限实际修改的代码段，不执行全仓重排。UI 开发还须遵循根目录 `AGENTS.md`。
`build/` 下的探针输出、截图和日志均为本地产物；确认程序有无独立验证价值后再清理源码。

## 探针的三类归属

`probe/` 里的源码不都是同一种东西，改动前先确认属于哪一类：

1. **进 CTest 的自动断言**：由 `cmake/Tests.cmake` 的 `aino_add_test()` 注册（如 `state_lifetime`、
   `interaction`、`overlay`、`widget`、`raster_equiv`、`shadow_mask`、`icon_mask`）。
   这类改动必须保持全绿。
2. **性能台架**：`perf_comp.cpp`、`spin_probe.cpp`、`blur_bench.cpp`、`bench.cpp` 一类，
   手动运行、看数字，不做断言（时间阈值在 CI 上不可靠）。
3. **历史复现与手工诊断**：`ime_*`、`term_*`、`font_*`、`dc_repro`、`twin_probe` 等
   共 20 余个，用来复现当时的具体现象（IME 闪烁、终端换行、字体回退），
   **不参与构建、不进 CI**。它们的价值是"下次遇到同样的现象时有一份可跑的现场"，
   不是回归门禁；改动核心代码时不需要运行它们。

新增探针时请说明属于哪一类，并避免把第 3 类当成第 1 类来依赖。
