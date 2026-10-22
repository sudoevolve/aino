# 01 · 快速开始

## 1. 接入

aino 是**单个头文件**，没有构建系统、没有第三方库、没有资源文件。
把 `aino.hpp` 拷进你的工程，然后在**一个** `.cpp` 里定义实现宏：

```cpp
// main.cpp —— 实现只能出现在一个编译单元里
#define AINO_IMPLEMENTATION
#include "aino.hpp"
```

其它文件只需要 `#include "aino.hpp"`（**不要**再定义 `AINO_IMPLEMENTATION`，
否则会重复定义）。也支持只用一个文件，直接在上面那个文件里写完整个程序。

`AINO_IMPLEMENTATION` 必须出现在**第一次** include 之前。写成这样是错的：

```cpp
#include "aino.hpp"          // ✗ 已经 include 过了
#define AINO_IMPLEMENTATION  // ✗ 太晚，实现不会被编译
#include "aino.hpp"
```

## 2. 编译

| 工具链 | 命令 |
|---|---|
| MSVC | `cl /std:c++17 /utf-8 /EHsc /O2 /MT main.cpp user32.lib gdi32.lib` |
| MinGW | `g++ -std=c++17 -O2 -static-libgcc -static-libstdc++ main.cpp -o app.exe -lgdi32 -luser32` |
| GCC/X11 | `g++ -std=c++17 -O2 main.cpp -lX11` |
| Clang/macOS | `clang++ -std=c++17 -O2 main.cpp -framework Cocoa -framework CoreText -framework CoreGraphics` |

要求 **C++17**（用到 `std::function`、`std::string_view` 为可选、结构化绑定）
和 **`std::uint8_t` 等定宽类型**。

### 分发：怎么做到「只有一个 exe，不带 DLL」

这不是 aino 的问题，是**编译器运行时的链接方式**。默认参数下 exe 会动态导入编译器的
运行库，而它们只活在开发机上（这里是 CLion 自带 MinGW 的 `bin` 目录），目标机一份都没有：

| 工具链 | 默认参数下动态导入 | 目标机典型报错 |
|---|---|---|
| MinGW / g++ | `libgcc_s_seh-1.dll`、`libstdc++-6.dll` | 找不到 libstdc++-6.dll |
| MSVC（默认 `/MD`） | `vcruntime140.dll`、`msvcp140.dll` | 缺少 VCRUNTIME140.dll |

自己确认一眼：MSVC 用 `dumpbin /dependents app.exe`，MinGW 用
`objdump -p app.exe | findstr "DLL Name"`。**只有 KERNEL32 / USER32 / GDI32 /
msvcrt 是系统自带的**，其余都得自己解决。

**做法一句话：静态链接运行时。**

| 工具链 | 加什么 | 结果 |
|---|---|---|
| MSVC | `/MT`（别和 `/MD` 混用） | 一个 exe，零 DLL |
| MinGW / g++ | `-static`（配 `-s` 去符号） | 一个 exe，零 DLL |

实测（本仓库 gallery，w64devkit GCC 16.2.0）：

```sh
g++ -std=c++17 -O2 -mwindows -s -static gallery.cpp -o app.exe -luser32 -lgdi32
objdump -p app.exe | findstr "DLL Name"
#   GDI32.dll  KERNEL32.dll  msvcrt.dll  USER32.dll     ← 全是系统自带
#   493 KB，就这一个文件，拷到任何 Win10/11 上直接双击
```

**但 `-static` 能不能成功，取决于工具链的静态运行时是否完整。**
CLion 自带的那套 MinGW 15.2.0 是坏的：`libwinpthread.a` 引用 `__intrinsic_setjmpex`、
`libmsvcrt.a` 引用 `__ms_vsnprintf`，两个符号它自己都没提供，`-static` 直接链接失败。
试过 `-lucrt`、`-Wl,--whole-archive`、`--allow-multiple-definition`、`-lmsvcrt-os`
全都不管用 —— 只能换工具链（w64devkit / MSYS2 / winlibs 都行），或者退回下面这条：

```sh
# 半静态：甩掉 libgcc/libstdc++，但还会剩一个 libwinpthread-1.dll（72 KB）
g++ -std=c++17 -O2 -mwindows -s -static-libgcc -static-libstdc++ gallery.cpp -o app.exe -luser32 -lgdi32
```

为什么甩不掉 winpthread：libstdc++ 的静态库（`guard.o`、`eh_alloc.o`）和 libgcc 的
`emutls.o`（`thread_local`）都要 `pthread_mutex_lock`。所以「究竟能不能只出一个 exe」，
答案在工具链手上，不在 aino 的代码里。

`tools/build.py` 已经写好这段逻辑：先试 `-static`，失败才退回半静态并把
`libwinpthread-1.dll` 拷到 exe 旁边。

**工具链怎么找**：`tools/toolchain.py` 按这个顺序探测，**不需要改系统 PATH**——

1. 环境变量 `AINO_GXX`（想指定编译器就 `set AINO_GXX=D://gcc//bin//g++.exe`）
2. 常见便携安装位置：`%USERPROFILE%\Downloads\w64devkit\w64devkit\bin`、
   `C://w64devkit//bin`、`C://msys64//mingw64//bin`、`C://msys2//mingw64//bin`
3. `PATH` 里的 `g++`（最后兜底）

`tools/build.py` 会按这个顺序**逐个试 `-static`，第一个成功的就用**。所以哪怕
`PATH` 里排第一的是那套残废 MinGW，只要机器上还有一套健康的，产物就是单文件。

CLion 走的是 CMake，编译器由 Toolchain 决定，不受上面这套探测影响：
`Settings → Build, Execution, Deployment → Toolchains` 新建一个 MinGW，
指向 `...\w64devkit\w64devkit\bin` 即可（CMakeLists 那边有配置期探测，
工具链不支持 `-static` 时会自动退回半静态，不会一构建就报链接错误）。

### 两个必须注意的编码细节

aino 自己的注释是中文 UTF-8 **带 BOM**。这两个反斜杠问题要分开处理：

1. **源码读取**：MSVC 在没有 BOM 时按系统代码页（简中 = GBK）读源码，
   中文注释的多字节序列会把后面的代码「吃掉」，报出一堆行号对不上的怪错
   （典型症状：`uint8_t 缺少类型说明符`）。
   **解决办法**：加 `/utf-8`（推荐），或保证源文件带 UTF-8 BOM。

2. **字面量编码**：没有 `/utf-8` 时，你代码里的中文字面量会被编成 GBK，
   而库内部按 UTF-8 解。`to_wide()` 做了兼容（严格 UTF-8 解码失败就回退
   `CP_ACP`），所以大多数串能正确显示；但 GBK 里存在「本身就是合法 UTF-8」
   的歧义字节（例如「状态」= `D7 B4 CC AC`），那种情况任何启发式都救不回来。
   **结论：MSVC 下请务必加 `/utf-8`。**

## 3. 最小可运行程序

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
        ui.panel({24, 24, 400, 240});

        aino::Stack col = ui.vbox({24, 24, 400, 240}, 10.0f, 18.0f);
        ui.heading("设置", col.next(34.0f), aino::Style().left());
        ui.input(&email, col.next(36.0f), aino::Style().hint("you@example.com"));
        ui.checkbox(&notify, "邮件通知", col.next(22.0f));
        if (ui.button("保存", col.next(36.0f), aino::Style().primary())) {
            // 点了保存
        }
    });
    return 0;
}
```

运行后：窗口静止时 CPU 恒为 0%，鼠标移上去按钮才变色 —— 这是事件驱动的结果，
不是「低帧率空转」。

## 4. 主循环模型

`Window::run(frame_cb)` 是完整的主循环，内部做四件事：

```
poll 消息 → 需要重绘? → begin_frame → frame_cb(ui) → end_frame → BitBlt 上屏
                ↑
          没有事件时阻塞在 WaitMessage（这就是 0% CPU 的来源）
```

**帧回调只在需要重绘时被调用**，所以可以放心地在里面重建整个 UI ——
不需要判断「哪部分脏了」。

需要自己控制循环时用单步接口：

```cpp
while (win.poll()) {
    // poll 处理一批消息，返回是否还活着
    if (/* 你需要重绘 */) win.render();   // 走一遍完整帧
}
```

### 什么时候会触发一帧

| 触发源 | 说明 |
|---|---|
| 鼠标 / 键盘 / 滚轮事件 | 立即响应，**不做节流**（输入延迟优先） |
| 窗口尺寸变化、主题切换 | 立即 |
| `ui.repaint()` | 组件或调用方自请求的帧，按 `set_anim_fps` **节流** |
| 定时重绘（无） | 没有隐藏的心跳，静止就是静止 |

`ui.repaint()` 与输入帧的区别很重要：输入帧不节流（保证手感），
自请求帧按 `anim_fps`（默认 60）节流（保证不烧 CPU）。

### 拖边框缩放时，帧不是主循环画的

这一条容易踩，也是"缩放窗口变黑"的成因，单独说清楚。

拖窗口边框走的是 `DefWindowProc` 的非客户区逻辑：user32 在它内部开一个**模态循环**
（`WM_ENTERSIZEMOVE` … `WM_EXITSIZEMOVE`），整个拖动过程里 `run()` 的主循环
一行都跑不到。而 `WM_SIZE` 会重建后备缓冲（新建的 DIB 全是 0），
如果这时紧跟着的 `WM_PAINT` 把这块没画过的缓冲贴上去，屏幕就是**纯黑**，一直黑到松手。

所以库内部有两条硬约束（`win_proc` 里）：

| 约束 | 位置 | 作用 |
|---|---|---|
| `bb_valid` 为假时**不许 BitBlt** | `WM_PAINT` | 绝不把没渲染过的缓冲贴上屏 |
| 缓冲有效时 `WM_PAINT` **只贴不重画** | `WM_PAINT` | 系统叫我们重画（被遮挡又露出）时只是补上画面，不重新构建 UI |
| `WM_SIZE` 里**同步补一帧** | `WM_SIZE` | 拖动过程中尺寸每变一次就重画一次，跟手且不黑 |

这就是为什么 `run()` 会把 `render()` 绑到 `Impl::render_fn` —— 消息路径需要一个
"现在给我画一帧"的入口。回归断言在 `tools/probe/resize_probe.cpp`：
连续改尺寸，每次改完直接读后备缓冲的像素，必须是「按新尺寸画过的画面」，
不是黑帧、也不是拉伸后的旧画面。

## 5. 窗口控制

### 5.1 创建时配置（WindowConfig）

三参数构造等价于只填了尺寸和标题。图标、字体、尺寸约束这些走 `WindowConfig`：

```cpp
aino::WindowConfig cfg;
cfg.width           = 1120;          // 逻辑像素
cfg.height          = 720;
cfg.title           = "aino 示例";
cfg.icon_path       = "app.ico";     // 见下方平台差异
cfg.font_path       = "fonts/Inter-Regular.ttf";   // 字体**文件**，库自己读家族名
cfg.font_family_mono= "Consolas";
cfg.min_width       = 900;           // 0 = 不限制
cfg.min_height      = 600;
cfg.max_width       = 1600;
cfg.max_height      = 1200;
cfg.resizable       = true;          // false → 去掉厚边框与最大化
cfg.center          = true;          // 首次显示居中到工作区
cfg.high_dpi        = true;          // false → 不声明 DPI 感知（进程级，只有首个窗口生效）

aino::Window win(cfg);
```

C++20 下可以直接用指定初始化：

```cpp
aino::Window win({ .width = 1120, .height = 720, .title = "aino",
                   .icon_path = "app.ico", .min_width = 900 });
```

| 字段 | 平台差异 |
|---|---|
| `icon_path` | Windows 读 `.ico`（`LoadImage` + `WM_SETICON`，大/小两个尺寸都设）；macOS 读 `.png`/`.icns` 并设成**应用图标**（窗口本身没有图标位）；X11 忽略 |
| `font_path` | Windows `AddFontResourceExW(FR_PRIVATE)`；macOS `CTFontManagerRegisterFontsForURL`（进程作用域）；X11 忽略（核心 X 字体没有"加载字体文件"这条路），此时按 `font_family` 挑已装字体 |
| `min_*` / `max_*` | Windows `WM_GETMINMAXINFO`；X11 `XSizeHints`（`PMinSize`/`PMaxSize`）；macOS `setContentMinSize:` / `setContentMaxSize:` |
| `resizable=false` | Windows 去掉 `WS_THICKFRAME`+`WS_MAXIMIZEBOX`；X11 把 min 与 max 设成同值（ICCCM 没有"不可缩放"这个位，只能这么表达）；macOS 去掉 `NSWindowStyleMaskResizable` |
| `center` | 三家都按**工作区**（排除任务栏/Dock）居中 |

`font_path` 不要求你填家族名：库自己解 sfnt 的 `name` 表（`.ttf` / `.otf` 结构相同），
读出 `nameID=1` 的家族名再排版。走这条路是因为 Windows 的 `GetFontResourceInfoW`
只有 MSVC 的头文件里有声明，mingw 直接编不过 —— 自己解析反而三平台通用。

### 5.2 创建之后

```cpp
aino::Window win(1120, 720, "标题");

win.ok();                 // 创建是否成功（失败原因见平台后端）
win.ui();                 // 拿到 Ui 实例（run 之外手动驱动时用）
win.set_theme(aino::Theme::dark());   // 值比较，没变化不会触发重绘
win.set_title("新标题");
win.set_size(1280, 800);  // 逻辑尺寸，会按 DPI 换算成物理像素
win.width(); win.height(); win.dpi_scale();
win.set_anim_fps(30.0f);  // 常驻动画的重绘频率上限，默认 60
win.close();              // 请求退出（run 会返回）
win.request_repaint();    // 从别的线程唤醒主循环（只置标志）
```

`set_theme` 内部用 `Theme::same_as()` 比较全部色值与度量，
**没变化就不请求重绘**。这条设计是必需的：在帧回调里每帧调 `set_theme`
是很自然的写法（切主题后让本帧就用新配色），没有值比较的话主循环
永远回不到 `WaitMessage`，空闲会吃满一个核。

## 6. 主题切换

```cpp
aino::Theme th = dark_mode ? aino::Theme::dark() : aino::Theme::light();
win.set_theme(th);   // 每帧调也安全（内部做值比较）

win.run([&](aino::Ui& ui) {
    const aino::Theme& t = ui.theme();   // 帧内读取当前主题
    ui.fill_rect({0, 0, ui.width(), ui.height()}, t.background);
    // ...
});
```

自定义主题就是改 token 后传进去：

```cpp
aino::Theme t = aino::Theme::light();
t.primary = aino::Color::hex(0x2563EB);   // 换成蓝色主色
t.radius  = 4.0f;                         // 全局更硬朗的圆角
win.set_theme(t);
```

全部 token 见 [02-style-and-theme.md](02-style-and-theme.md)。

## 7. 常见错误

| 现象 | 原因 |
|---|---|
| 中文变成乱码 / 方框 | MSVC 没加 `/utf-8` |
| `uint8_t 缺少类型说明符` 一堆怪错 | 源文件丢了 BOM 且没加 `/utf-8` |
| 链接报 `LNK2001` / 找不到实现 | 忘了在某个 TU 里 `#define AINO_IMPLEMENTATION` |
| 重复定义 | 多个 TU 都定义了 `AINO_IMPLEMENTATION` |
| 空闲 CPU 20~100% | 帧回调里无条件 `repaint()` / 每帧 `set_theme()` / 用了动画 skeleton |
| 文字被裁一半 | 给的高度小于 `ui.text_height(...)`，容器高度要量不要拍 |
| 点按钮没反应 | 按钮的 `box.x/y` **每帧都在变** —— 组件 id 由 `kind + box.x + box.y + 文本` 算出，位置一动 id 就换，按下与松开会落到不同 id 上 |
| 组件状态错乱（hover 落到别人身上） | 两个组件相邻且 `box.x/y` 相同（例如都画在 `{0,0}` 上），id 撞了 |
