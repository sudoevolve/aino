# 开发与验证工具

核心库和普通示例不依赖 Python。优先使用仓库根目录的 CMake 构建；
工具脚本按用途运行，避免将所有历史排障程序视为同一种测试。

核心回归的统一入口为 `-DAINO_BUILD_TESTS=ON` 和 CTest，用法见 `docs/wiki/01-quickstart.md`。
日常仅编译受影响目标；以下自动断言和网络探针在发布验收时运行。
多编译单元链接探针需由 CMake 同时编译实现与调用者，不能作为单文件用 `cl_one.py` 构建。

| 类别 | 入口 | 说明 |
| --- | --- | --- |
| 构建 | `build.py`、`cl_one.py`、`msvc_build.py`、`toolchain.py` | Windows 辅助入口；需自行安装编译器；`AINO_GXX` 可显式指定 g++ |
| Wayland 协议 | `generate_wayland_protocols.py` | 从系统 XML 更新头文件内 ABI 与许可；只在升级协议时运行，普通构建不需要 |
| 功能回归 | `regress.py`、`ci_smoke.cpp`、`ci_pixels.cpp` | Python 回归入口主要面向 Windows；CI 冒烟及像素程序适配各原生后端 |
| 布局/截图 | `verify_gallery.py`、`verify_render.py` | Windows 桌面及实际像素验证；不能用测量结果替代截图 |
| 性能 | `perf_gallery.py`、`perf_bench.py` | 进程 CPU、内存和真实交互采样；测量口径见 `docs/wiki/10-performance.md` |
| 专项探针 | `probe/` | 自动断言、复现和手工网络诊断并存；网络/GUI 探针需要相应环境 |
| SSH 发布验收 | `ssh_release_check.py` | 临时目录、临时 HOME、回环服务器，验证真实 SSH/SFTP；仅测试需要 Paramiko 4.0.0 |
| 合成等价性 | `probe/shadow_mask_probe.cpp`、`probe/icon_mask_probe.cpp` | 投影掩膜缓存（≤1 LSB）与图标整行合成（逐字节）的前后对照，已进 CTest |

Python 脚本建议使用 Python 3.10+。进程采样额外安装 `python -m pip install -r tools/requirements.txt`；
其它平台 API 由脚本按用途调用，不能据此假设脚本跨平台可运行。

新增或修改代码遵循根目录 `.editorconfig` 和 `.clang-format`；
格式化仅限实际修改的代码段，不执行全仓重排。UI 开发还须遵循根目录 `AGENTS.md`。
`build/` 下的探针输出、截图和日志均为本地产物；确认程序有无独立验证价值后再清理源码。

## 探针的三类归属

`probe/` 里的源码不都是同一种东西，改动前先确认属于哪一类：

1. **进 CTest 的自动断言**：由 `cmake/Tests.cmake` 的 `aino_add_test()` 注册（如 `state_lifetime`、
   `interaction`、`overlay`、`widget`、`raster_equiv`、`shadow_mask`、`icon_mask`、`resize_flash`）。
   发布验收必须保持全绿，日常改动只编译受影响夹具。
2. **性能台架**：`perf_comp.cpp`、`spin_probe.cpp`、`blur_bench.cpp`、`bench.cpp` 一类，
   手动运行、看数字，不做断言（时间阈值在 CI 上不可靠）。
3. **历史复现与手工诊断**：未注册的 `ime_*`、`term_*`、`font_*`、`dc_repro`、`twin_probe` 等
   共 20 余个，用来复现当时的具体现象（IME 闪烁、终端换行、字体回退），
   **不参与构建、不进 CI**。它们的价值是"下次遇到同样的现象时有一份可跑的现场"，
   不是回归门禁；改动核心代码时不需要运行它们。`term_wrap`、`term_scroll` 已在启用 SSH 时
   注册进 CTest，属于第 1 类，不能按文件名前缀归入历史程序。

新增探针时请说明属于哪一类，并避免把第 3 类当成第 1 类来依赖。

SSH 的 `example_ui_probe.cpp --local-transfer <端口>` 是显式网络验收入口，覆盖后台上传下载、
目录与传输交错、最新 PTY 尺寸、取消唤醒、错误清理和提交后取消；默认离线运行不执行这些场景。
本地服务器与隔离目录方法见 [快速开始](../docs/wiki/01-quickstart.md)。慢网与提交回复丢失仍需独立验收。

发布验收构建 `AINO_BUILD_TESTS=ON`、`AINO_BUILD_SSH=ON` 后执行：

```sh
python -m pip install paramiko==4.0.0
python tools/ssh_release_check.py --engine build/test/aino_test_ssh_protocol
```

Windows 的路径按生成器加入 `Release/` 或使用 Ninja 的输出目录，附加
`--ui <aino_test_ssh_ui.exe 路径>` 验证客户端工作线程。脚本为子进程设置隔离目录，
结束后关闭服务器并回收临时文件，不读取用户连接配置。POSIX 还会在高于 FD_SETSIZE 的 socket 上连接。
Windows 测试服务器需要 Python 3.13+ 的文件描述符 chmod 支持；发布 CI 固定使用 Python 3.14。

官网/Wiki 使用 `python tools/build_site.py` 打包到 `build/site/`，只依赖 Python 标准库。
本地预览直接启动静态服务器，不使用回归工具；打包与 GitHub Pages 发布方法见 [网站说明](../site/README.md)。

品牌资源使用 `python tools/build_icons.py` 从 `assets/aino_icon.svg` 内嵌原图生成 PNG 与多尺寸 ICO。
仅这个资源生成步骤需要 Pillow（`python -m pip install Pillow`），普通构建使用已提交资源。
PNG 与 ICO 使用一致的可见轮廓和留白；ICO 包含 16、20、24、32、40、48、64、96、128、256 像素图层。
Windows CMake 示例和 `build_ssh.py app` 都嵌入该 ICO；替换资源后需重新构建可执行文件。
