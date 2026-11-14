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
| `pomodoro.cpp` | `aino_pomodoro` | 番茄钟、侧栏布局、专注与休息计时 |
| `primitives_demo.cpp` | `aino_primitives_demo` | 图形、阴影、裁剪、变换、系统图标和背景模糊 |
| `function_viewer.cpp` | `aino_function_viewer` | 函数曲线、自定义表达式、采样缓存 |
| `ssh_client.cpp` | `aino_ssh` | 可选 SSH/SFTP 客户端 |

Visual Studio 多配置生成器的程序在 `build/Release/`，Ninja/Makefiles 通常在 `build/`。
可只构建一个目标：

```sh
cmake --build build --config Release --target aino_primitives_demo
```

Windows 回归构建入口 `python tools/build.py` 默认构建 Release 画廊，并将程序复制至
`build/aino_gallery.exe`。也支持 `--src examples/primitives_demo.cpp --out aino_primitives_demo.exe`。
脚本不会关闭正在运行的示例；输出文件被占用时请关闭对应窗口后再构建。

番茄钟默认暂停，支持专注、短休息、长休息，以及开始 / 暂停 / 继续 / 重置。
暂停后可调整各模式时长；完成后手动开始下一轮，完成次数只统计本次运行的专注。
计时使用 `steady_clock`，运行时后台每秒调用 `wake()`，暂停后停止定时唤醒；
窗口遮挡或帧延迟不会累计计时误差。工具栏按钮切换浅深外观，`AINO_DARK=1` 可深色启动。

函数显示器默认暂停，点击“动画”或在公式输入框外按空格开始 / 暂停。
滚轮缩放、拖动平移、双击或 `R` 复位视图；编辑公式时不会触发绘图快捷键。
曲线、导数和标量场网格在公式、参数、时间或视图变化时重新采样，移动准星及切换主题复用缓存。
缓存减少计算，不缓存整幅图像；暂停时鼠标交互仍会绘制当前画面。

### 可选 SSH 示例

SSH 额外依赖 libssh2。MSVC 配置使用 `AINO_SSH_DEPS` 指向的预编译 libssh2 + mbedTLS
目录，默认 `build/third_party`；依赖使用 `/MD`，所以 SSH 与普通示例的 CRT 配置不同。
其它工具链通过 `find_path` / `find_library` 查找 libssh2 开发库及 Threads。
缺少依赖时仅跳过 SSH。可以用 `-DAINO_BUILD_SSH=OFF` 禁用该目标。

指定私钥路径时，密码字段用作私钥口令；未指定私钥时用于密码/键盘交互认证。
未加密私钥允许空口令。主机密钥记录位于 `~/.aino_ssh/known_hosts`，首次连接采用 TOFU；
记录不匹配、无法读取或无法持久化新记录都会拒绝连接。该格式为本示例专用，不能直接替换 OpenSSH 的文件。

连接配置、下载目录、内置 SSH 配置编辑器和主机记录使用同目录临时文件加原子替换，
写入、同步或替换失败会返回错误，保留旧文件及待保存草稿。原子替换不承诺断电恢复，
也不合并多个进程同时修改同一配置的内容。上传从已打开文件查询 64 位长度，避免 `ftell` 的 2 GB 边界。
记住的密码仍是可逆混淆；原子保存不改变其保密性，系统凭据存储尚未接入。

### 构建和运行回归

```sh
cmake -S . -B build/test -DCMAKE_BUILD_TYPE=Release -DAINO_BUILD_TESTS=ON -DAINO_BUILD_SSH=OFF
cmake --build build/test --config Release --parallel 2
ctest --test-dir build/test -C Release --output-on-failure --parallel 2
```

测试默认不构建。`headless` 标签是不创建原生窗口的数值/离屏/文件测试，`native` 标签需要桌面；
可用 `ctest --test-dir build/test -C Release -L headless` 单独运行前者。
Linux 的完整测试使用 `xvfb-run -a ctest --test-dir build/test --output-on-failure`。
原生窗口测试串行运行，并设有超时；每个测试的输出独立写入构建目录的 `test-output/`。
Windows GDI 夹具只在 Windows 编译，名称中出现 `macos` 不表示已验证 Cocoa 后端。
多编译单元链接契约、线程唤醒、编辑器交互、光栅等价性和画廊基准均纳入 CTest。

`.github/workflows/regression.yml` 在 push / PR 上运行三个平台的构建和 CTest；
原有发布工作流仍保留专用字体和截图检查。CI 配置存在不等于本地已经跑过其它平台。

客户端的连接、文件操作和传输都在后台执行。切换标签或返回启动页后，传输继续，
各标签的终端输出仍会解析；连接等待不再驱动持续动画。
“打开配置文件”调用系统编辑器，打开当前用户的 `.ssh/config`；VSCode 指定配置导入的
连接会打开对应来源文件。没有文件关联时 Windows 使用系统记事本。保存后点击“重新导入连接”
刷新列表；该操作不会中断现有会话。客户端不再内置 SSH 配置文本编辑器。
会话标签栏复用 `ui.tabs()`，支持状态标记、逐项关闭及溢出翻页；新建按钮返回连接首页。
传输状态最多约每 33 ms 通知界面一次，底部“取消”在当前传输块的网络调用返回后生效。
调整窗口尺寸只提交最新 PTY 尺寸，由后台 reader 发送。
取消或失败可能保留已写入的部分文件；目前不提供传输队列、断点续传或递归目录下载。
上传仍使用本地路径目录内唯一的文件；目录含多个文件时会提示，尚无上传文件选择器。

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
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded `
  "-DMBEDTLS_INCLUDE_DIR=$deps/mbedtls-3.6.2/include" `
  "-DMBEDCRYPTO_LIBRARY=$deps/mbedtls-3.6.2/mbedtls_all.lib"
cmake --build "$deps/l2mbed" --config Release
cmake -S . -B build
cmake --build build --config Release --target aino_ssh
```

`build/` 是构建目录，不属于核心库分发；仓库不自动下载 SSH 依赖。安装好依赖后，
`python tools/build_ssh.py app` 也可构建客户端，`term` 构建终端解析探针，`ui` 构建界面探针。
`--asan` 使用当前 MSVC 工具链的 ASan x64 运行库，并复制到 EXE 旁。
发布构建的 libssh2、mbedTLS 与客户端统一使用 `/MT`；已有 `/MD` 依赖必须按上述命令
重新构建，不能混用 CRT。`--asan` 仍需要对应的 ASan 诊断运行库，不作为单文件发布产物。

MinGW 使用 libssh2 的 WinCNG 后端时，面向 Windows 10 及以上系统须显式启用
`-DENABLE_ECDSA_WINCNG=ON`，以编入 ECDH 密钥交换。仅有源码中的算法优先级列表
不会为依赖库增加算法；未启用时可能能编译、开窗，却无法与目标 SSH 服务器协商。
例如对已解压的 libssh2 1.11.1，使用 MinGW 工具链配置：

```powershell
cmake -S "$deps/libssh2-1.11.1" -B "$deps/l2wincng" -G "MinGW Makefiles" `
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON `
  -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DENABLE_ZLIB_COMPRESSION=OFF `
  -DCRYPTO_BACKEND=WinCNG -DENABLE_ECDSA_WINCNG=ON
cmake --build "$deps/l2wincng" --parallel 2
```

主项目使用 `AINO_LIBSSH2_INCLUDE_DIR` 和 `AINO_LIBSSH2_LIBRARY` 指向该库的头文件目录和
静态库文件。启用 SSH 和测试时，CTest 的 `ssh_crypto` 会检查实际链接库是否具备
Curve25519/ECDH，不连接服务器，也不访问用户配置或凭据。此项通过仍不能代替真实握手和认证。

Windows 单文件发布可使用提供完整静态运行时的 MinGW（例如已安装的 w64devkit）。
在新的构建目录中指定工具链，依赖库也用同一工具链构建；仅修改当前 PowerShell 的 PATH：

```powershell
$mingw = 'C:/Tools/w64devkit/bin'
$env:PATH = "$mingw;" + $env:PATH
$deps = (Resolve-Path build/third_party).Path
cmake -S "$deps/libssh2-1.11.1" -B "$deps/l2wincng-static" -G 'MinGW Makefiles' `
  "-DCMAKE_C_COMPILER=$mingw/gcc.exe" "-DCMAKE_MAKE_PROGRAM=$mingw/mingw32-make.exe" `
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON `
  -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DENABLE_ZLIB_COMPRESSION=OFF `
  -DCRYPTO_BACKEND=WinCNG -DENABLE_ECDSA_WINCNG=ON
cmake --build "$deps/l2wincng-static" --parallel 3
cmake -S . -B build/ssh-static -G 'MinGW Makefiles' `
  "-DCMAKE_CXX_COMPILER=$mingw/g++.exe" "-DCMAKE_MAKE_PROGRAM=$mingw/mingw32-make.exe" `
  -DCMAKE_BUILD_TYPE=Release -DAINO_STATIC_EXE=ON -DAINO_BUILD_SSH=ON -DAINO_BUILD_TESTS=ON `
  "-DAINO_LIBSSH2_INCLUDE_DIR=$deps/libssh2-1.11.1/include" `
  "-DAINO_LIBSSH2_LIBRARY=$deps/l2wincng-static/src/libssh2.a"
cmake --build build/ssh-static --parallel 3
ctest --test-dir build/ssh-static --output-on-failure
```

`AINO_STATIC_EXE=ON` 在静态运行时不完整时直接报错，不再自动回退为 EXE 加 DLL。
MinGW 的 SSH 产物在构建后及 `ssh_static_runtime` 测试中检查实际导入表，只允许 Windows
系统 DLL；`libwinpthread`、libstdc++、libgcc 或第三方加密 DLL 都会使检查失败。

后台传输的定向检查使用仓库自带的本地服务器。测试工具另需 Python `paramiko`，
仅用于检查，不是客户端运行依赖。在终端 A 启动：

```powershell
python tools/dev_ssh_server.py --port 2228 --root build/ssh-session-server
```

在终端 B 编译并运行；`ssh_ui_probe` 自行创建并检查专用临时目录，退出时清理：

```powershell
python tools/build_ssh.py ui
.\build\ssh_ui_probe.exe --local-transfer 2228
```

该检查只连接 `127.0.0.1`，使用 `demo/demo`；测试后在终端 A 按 `Ctrl+C` 停止服务器。
客户端需要独立的配置目录时，可显式设置 `AINO_SSH_HOME`，它同时重定向连接配置、
主机密钥记录、下载目录偏好和 `.ssh/config`；未设置时仍使用当前用户的目录。
仅修改 `USERPROFILE` 无法覆盖 Windows 系统账户 API 返回的主目录，不应用于测试隔离。

## 3. 单独编译应用

| 工具链 | 命令 |
|---|---|
| MSVC | `cl /std:c++17 /utf-8 /EHsc /O2 /MT main.cpp user32.lib gdi32.lib` |
| MinGW | `g++ -std=c++17 -O2 -mwindows -s -static main.cpp -o app.exe -lgdi32 -luser32` |
| GCC/X11 | `g++ -std=c++17 -O2 main.cpp -o app -lX11 -ldl` |
| Clang/macOS | `clang++ -std=c++17 -O2 main.cpp -o app -framework Cocoa -framework CoreText -framework CoreGraphics` |

Windows 示例默认静态链接编译器运行时，仍使用系统 DLL。
`-static` 需要工具链提供完整静态运行时；CMake 不会静默回退。
仅当明确接受 DLL 部署时使用 `AINO_STATIC_EXE=OFF`；MinGW 此时采用
`-static-libgcc -static-libstdc++`，并随附工具链提供的 `libwinpthread-1.dll`。

Linux 文字后端运行时加载 FreeType/fontconfig，无需链接其开发库；缺失时回退到 XFontSet。
中文还需安装 CJK 字体。Debian/Ubuntu 可安装：

```sh
sudo apt install libfreetype6 libfontconfig1 fonts-noto-cjk
```

`AINO_X11_TRACE=1 ./app` 可查看字体加载与兜底信息。

X11（包括 Wayland 会话中的 XWayland）默认从 XSETTINGS 的 `Xft/DPI`、
`Gdk/WindowScalingFactor` 以及 X resources 的 `Xft.dpi` 读取桌面缩放，设置变化后更新。
可用 `AINO_SCALE=1.5 ./app` 显式指定倍率；优先级高于 `GDK_SCALE` 和桌面设置，
有效范围为 0.5–4。桌面 DPI 与整数倍率不会重复相乘，`GDK_DPI_SCALE` 不用于整窗缩放。
`cfg.high_dpi=false` 时 X11 固定使用 1 倍，忽略上述设置。
窗口大小、尺寸约束、`set_size()` 和输入坐标统一使用逻辑像素。
这是 X11/XWayland 后端，尚未实现原生 Wayland 或逐显示器的独立缩放。

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
