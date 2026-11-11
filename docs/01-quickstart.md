# 01 · 快速开始

## 1. 接入

核心库是单个 `aino.hpp`，接入应用不要求使用 CMake，也不需要第三方 UI、图标库或资源文件。
在且仅在一个编译单元首次包含头文件前定义实现宏；其它编译单元只包含头文件。

```cpp
#define AINO_IMPLEMENTATION
#include "aino.hpp"
```

要求 C++17。源码和传给库的字符串统一使用 UTF-8，MSVC 请加 `/utf-8`。

## 2. 构建仓库示例

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

CMake 自动扫描 `examples/*.cpp`，每个文件生成 `aino_<文件名>` 目标。
新增或删除顶层 `.cpp` 后，构建会触发重新配置；Visual Studio 可能需要重新加载后再构建一次。
也可先运行 `cmake -S . -B build` 立即更新目标，再构建。`.hpp` 不生成可执行文件。
每个 `.cpp` 应有独立入口，并在该文件中定义 `AINO_IMPLEMENTATION`。

| 源文件 | 目标 | 内容 |
|---|---|---|
| `gallery.cpp` | `aino_gallery` | 15 分类、43 张卡片、搜索和素材子窗口 |
| `sidebar_demo.cpp` | `aino_sidebar_demo` | 侧栏导航与应用状态 |
| `zoom_demo.cpp` | `aino_zoom_demo` | 用户操作触发的缩放 |
| `primitives_demo.cpp` | `aino_primitives_demo` | 图形、阴影、裁剪、变换、系统图标和背景模糊 |
| `function_viewer.cpp` | `aino_function_viewer` | 函数曲线、自定义表达式 |
| `ssh_client.cpp` | `aino_ssh` | 可选 SSH/SFTP 客户端 |

Visual Studio 多配置生成器的程序在 `build/Release/`，Ninja/Makefiles 通常在 `build/`。
可只构建一个目标：

```sh
cmake --build build --config Release --target aino_primitives_demo
```

Windows 回归构建入口 `python tools/build.py` 默认构建 Release 画廊，并将程序复制至
`build/aino_gallery.exe`。也支持 `--src examples/primitives_demo.cpp --out aino_primitives_demo.exe`。
脚本不会关闭正在运行的示例；输出文件被占用时请关闭对应窗口后再构建。

### 可选 SSH 示例

SSH 额外依赖 libssh2。MSVC 配置使用 `AINO_SSH_DEPS` 指向的预编译 libssh2 + mbedTLS
目录，默认 `build/third_party`；依赖使用 `/MD`，所以 SSH 与普通示例的 CRT 配置不同。
其它工具链通过 `find_path` / `find_library` 查找 libssh2 开发库及 Threads。
缺少依赖时仅跳过 SSH。可以用 `-DAINO_BUILD_SSH=OFF` 禁用该目标。

MSVC x64 的依赖准备：将 libssh2 1.11.1 和 mbedTLS 3.6.2 源码分别解压到
`build/third_party/libssh2-1.11.1` 与 `build/third_party/mbedtls-3.6.2`，
确认其中分别包含 `include/libssh2.h` 和 `library/*.c`。然后在仓库根目录的
**x64 Native Tools / Developer PowerShell** 中执行（需 CMake 和 Ninja）：

```powershell
python tools/build_mbedtls_static.py
$deps = (Resolve-Path build/third_party).Path
cmake -S "$deps/libssh2-1.11.1" -B "$deps/l2mbed" -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON `
  -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DENABLE_ZLIB_COMPRESSION=OFF `
  -DCRYPTO_BACKEND=mbedTLS -DCMAKE_POLICY_DEFAULT_CMP0091=NEW `
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL `
  "-DMBEDTLS_INCLUDE_DIR=$deps/mbedtls-3.6.2/include" `
  "-DMBEDCRYPTO_LIBRARY=$deps/mbedtls-3.6.2/mbedtls_all.lib"
cmake --build "$deps/l2mbed" --config Release
cmake -S . -B build
cmake --build build --config Release --target aino_ssh
```

`build/` 是构建目录，不属于核心库分发；仓库不自动下载 SSH 依赖。安装好依赖后，
`python tools/build_ssh.py app` 也可构建客户端，`term` 构建终端解析探针，`ui` 构建界面探针。
`--asan` 使用当前 MSVC 工具链的 ASan x64 运行库，并复制到 EXE 旁。
libssh2 与 mbedTLS 静态链接不等于 CRT 静态链接：SSH 使用 `/MD`，目标系统仍需对应的
VC++ 运行库；不能仅凭本机启动成功就认定可在未安装运行库的机器上独立运行。

## 3. 单独编译应用

| 工具链 | 命令 |
|---|---|
| MSVC | `cl /std:c++17 /utf-8 /EHsc /O2 /MT main.cpp user32.lib gdi32.lib` |
| MinGW | `g++ -std=c++17 -O2 -mwindows -s -static main.cpp -o app.exe -lgdi32 -luser32` |
| GCC/X11 | `g++ -std=c++17 -O2 main.cpp -o app -lX11 -ldl` |
| Clang/macOS | `clang++ -std=c++17 -O2 main.cpp -o app -framework Cocoa -framework CoreText -framework CoreGraphics` |

Windows 普通示例默认静态链接编译器运行时，仍使用系统 DLL。
`-static` 需要工具链提供完整静态运行时；不能全静态链接时可用
`-static-libgcc -static-libstdc++`，并按导入表随附所需的 `libwinpthread-1.dll`。
`AINO_STATIC_EXE=OFF` 可关闭 CMake 的全静态运行时请求。

Linux 文字后端运行时加载 FreeType/fontconfig，无需链接其开发库；缺失时回退到 XFontSet。
中文还需安装 CJK 字体。Debian/Ubuntu 可安装：

```sh
sudo apt install libfreetype6 libfontconfig1 fonts-noto-cjk
```

`AINO_X11_TRACE=1 ./app` 可查看字体加载与兜底信息。

## 4. 最小程序

```cpp
#define AINO_IMPLEMENTATION
#include "aino.hpp"
#include <string>

int main() {
    aino::Window win(880, 560, "aino 示例");
    if (!win.ok()) return 1;
    std::string email;
    bool notify = true;
    win.run([&](aino::Ui& ui) {
        ui.clear(ui.theme().window_bg);
        const aino::Rect panel(24, 24, 400, 220);
        ui.panel(panel);
        auto col = ui.vbox(panel, 10, 18);
        ui.heading("设置", col.next(30));
        ui.input(&email, col.next(24), aino::Style().hint("you@example.com"));
        ui.checkbox(&notify, "邮件通知", col.next(20));
        if (ui.button("保存", col.next(24))) win.set_title("设置已保存");
    });
    return 0;
}
```

## 5. 窗口与主循环

`win.run(frame_cb)` 处理事件、建立帧、调用绘制回调并上屏；无事件和动画时阻塞等待。
Windows 使用 `WaitMessage`，其它后端使用对应平台事件循环。
帧回调内不提供通用重绘请求。控件状态变化由当前输入帧处理；帧回调外的一次更新可调用
`ui.request_next_frame()`。自定义动画在需要推进时调用 `ui.request_animation_frame()`，
只续一帧；全局连续动画使用 `ui.set_animating(true)`，动画结束时必须关闭。
`ui.request_next_frame()` 在帧回调期间返回 `false`，避免回调无条件续帧。
`win.set_anim_fps(30)` 限制动画自请求帧的频率，不限制输入响应。

```cpp
aino::WindowConfig cfg;
cfg.width = 1120; cfg.height = 720;
cfg.min_width = 900; cfg.min_height = 600;
cfg.title = "aino";
cfg.resizable = true;
cfg.center = true;
cfg.high_dpi = true;
aino::Window win(cfg);
win.set_title("新标题");
win.set_size(1280, 800);
win.set_anim_fps(30);
```

`cfg.icon_path`：Windows 读取 `.ico`；macOS 设置应用图标；X11 忽略。
`cfg.font_path`：Windows/macOS 可注册字体文件；X11 按字体家族与 fontconfig 查找。
`font_family_mono` 用于等宽文本通道。`min_*` / `max_*` 均为逻辑尺寸。
`win.close()` 请求关闭，后台线程更新后调用 `win.wake()` 唤醒循环；跨线程共享应用状态仍需同步。
X11 后端从后台线程调用 `wake()` 前，必须在创建任何窗口前调用 `XInitThreads()`；
否则应由 UI 线程调用。
多窗口示例的创建与回填目前在 Windows 实测，不能据此认定 Cocoa 支持后台线程建窗。

常规应用使用 `run` 管理帧生命周期；不要在帧回调里阻塞等待或执行网络操作。

## 6. 主题与强调色

```cpp
aino::Theme t = aino::Theme::dark();
t.set_tint(t.purple);
win.set_theme(t);
```

用 `set_tint()` 同步原生 `tint`、兼容 `primary`、前景、焦点环和选中行颜色。
只改 `primary` 不能更新所有控件。切换浅深色时，从新主题工厂创建主题，再应用保存的强调色选择。
`set_theme()` 比较所有主题字段，值相同不会持续请求重绘。
全部字段与尺寸见 [02](02-style-and-theme.md)。

## 7. 常见问题

| 现象 | 检查 |
|---|---|
| 中文乱码 | 源码保存 UTF-8，MSVC 加 `/utf-8` |
| 链接重复定义 | 只在一个编译单元定义实现宏 |
| 缺少实现 | 实现宏必须在首次包含头文件前定义 |
| 图标按钮被拉长 | 传入正方形 `Rect`；按钮不自动改变布局宽高 |
| 滚动时状态变化 | 使用库滚动作用域，让组件 ID 按内容坐标生成；自定义动画使用稳定 ID |
| 空闲持续绘制 | 检查显式动画帧请求、`set_animating(true)`、聚焦光标、spinner 与应用计时器 |
| 文字被截断 | 用 `text_height()` 分配高度，绘制和测量使用相同 Style |
