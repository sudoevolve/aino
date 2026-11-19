# 16 Android 适配分析

> 状态：**分析文档，不含代码改动**，也未提交。
> 本机没有 NDK、没有真机，所以凡是"在设备上成立"的判断都标了凭据来源；
> 标「**需实测**」的条目是推理，不是结论。数字只要来自本仓库的测量，都写明出处。

---

## 0. 结论

**可行，而且比 X11 后端小。** 后端本体估计 **700–900 行**（Windows 1124 行、X11 1492 行、macOS 976 行），
因为 fontconfig 那一整套（`aino.hpp:12260`–`12600`，约 340 行）在 Android 上可以**整段删掉** —— 系统自带的
`AFontMatcher` 干的正是同一件事，而且做得更好（按文字 run 匹配，直接给出回退边界）。

真正的代价不在光栅化。光栅化是纯 CPU、纯内存，`Canvas` 一行都不用改。代价在库的三个隐含假设上：

| 假设 | 在 Windows/X11/macOS 成立 | 在 Android 不成立 |
|---|---|---|
| 窗口由程序创建 | `CreateWindowEx` / `XCreateWindow` / `NSWindow` | **窗口由 Activity 给**，时机在生命周期回调里 |
| 文字由系统 API 画 | GDI / FreeType+fontconfig / CoreText | NDK **只给"选字体"不给"画字形"**（API 29+） |
| 输入法由窗口系统驱动 | IMM32 / XIM / NSTextInputClient | 必须经过 Java 的 `InputConnection` |

这三条决定了工程量。分四阶段：**P0 能画能跑**（1–2 天）、**P1 能打字**（+2–3 天）、
**P2 触摸可用**（组件层改动，3–5 天）、**P3 打磨**（不封顶）。

---

## 1. 现有后端契约：能直接复用的部分比想象中多

一个平台后端要提供的东西只有这些，都在 `aino.hpp` 里：

| 组件 | 位置 | Android 上的处理 |
|---|---|---|
| 平台探测 | `48–58` | **必改**。`__linux__` 在 Android 上成立 → 现在会误进 X11 分支，链接期找不到 libX11 |
| 像素通道顺序 | `1677–1685` | **一行**。Android 窗口缓冲是 RGBA_8888 → 与 macOS 同分支（`CH_R=0`） |
| `Canvas` | `1704` | **零改动**。纯内存 + stride，`blend_row` / `raster_*` 全部照用 |
| `TextRenderer` 接口 | `2956` | 需实现（见 §2.2）。接口本身是干净的：`measure/ascent/line_height/draw/release` + 三个带默认实现的变体 |
| `WakeSignal` | `10602` | **零改动**。`pipe` + `poll` 在 Android 上都有；`ALooper_addFd` 可以直接挂这个管道 |
| `Window::Impl` | `10687` | **零改动**。`need_frame` / `self_repaint` / `anim_budget_ms` / `repaint_timeout_ms()` 这一套节流逻辑正是 Android 需要的 |
| 上屏 | Win `11128` `BitBlt`、X11 `13032` `XPutImage` | `ANativeWindow_lock` → 画进 `buffer.bits` → `unlockAndPost` |
| 后端方法 | 各平台 10 个：`poll/render/run/set_title/set_size/set_clipboard` + 构造、析构、`dpi_scale`、`wake` | 全部需要，但多数是空实现或一行 |

**上屏这块 Android 反而占便宜**：Windows 要 `mem DC → BitBlt`（一次全屏拷），X11 要 `XPutImage`
（一次全屏拷 + 一次 X 往返）。`ANativeWindow_lock` 直接把**生产者缓冲的指针**交给你，画完
`unlockAndPost` 就进 BufferQueue —— 省掉整次全屏 memcpy。代价是 `buffer.stride` 是**像素**不是字节
（`canvas.stride = buffer.stride * 4`），这个坑和桌面端 `DIB` 的 stride 是同一个性质，代码里已经有先例。

---

## 2. 三个真正的难点

### 2.1 窗口不是程序创建的

现有 API 是**我建窗、我跑循环**：

```cpp
Window win(cfg);              // 构造函数里 CreateWindowEx / XCreateWindow，并求出 scale
win.run([](Ui& ui) { ... });  // 阻塞循环
```

Android 上顺序反过来：进程先起来，Activity 活过来，系统在 `APP_CMD_INIT_WINDOW` 时把
`ANativeWindow*` 递给你，转屏/切后台时会 `APP_CMD_TERM_WINDOW` 收回去。构造函数里拿不到窗口。

三个选项：

| 方案 | 做法 | 代价 |
|---|---|---|
| A. 构造函数自旋等窗口 | `Window(cfg)` 里 `ALooper_pollOnce` 转到 `INIT_WINDOW` 出现 | 事件循环的第一次迭代发生在构造函数里，`app` 指针得先由全局/线程局部拿到；转屏时窗口会变成新的 `ANativeWindow`，`Impl::native` 要重建，容易出悬空 `Canvas.px` |
| B. 显式挂载（推荐） | `WindowConfig::native_surface` 留空 = 自建；Android 上由胶水层在 `INIT_WINDOW` 时构造 `Window(cfg_with_surface)` | 加一个字段 + 构造函数里一个分支。**不破坏现有 API**（默认值 nullptr，其它平台永远走老路） |
| C. 单独一套 Android 入口 | `aino::android::App`，与 `Window` 平行 | 两套生命周期模型，长期分裂 |

B 是对的。它把"窗口从哪来"变成**数据**而不是控制流，`TERM_WINDOW` 时把 `native_surface` 置空、
`Canvas.px` 置空即可（现有 `render()` 已经有 `if (!s || !s->img) return;` 这类守卫，照抄）。

生命周期映射：

| Android 事件 | 动作 |
|---|---|
| `APP_CMD_INIT_WINDOW` | 构造或重挂载；`ANativeWindow_setBuffersGeometry` + 求 scale |
| `APP_CMD_TERM_WINDOW` | 停循环、`Canvas.px = nullptr`、释放 `ANativeWindow` |
| `APP_CMD_WINDOW_RESIZED` / `CONFIG_CHANGED` | 重设 geometry 与 `client_w/h`（**不要**重建 `Ui`，否则输入框内容和滚动位置全丢） |
| `APP_CMD_GAINED_FOCUS` / `LOST_FOCUS` | 对应 `KEY_SHIFT` 之类的修饰键清零（现在还多一个：`LOST_FOCUS` 要收软键盘） |
| `APP_CMD_PAUSE` / `RESUME` | 停 / 恢复循环；暂停时**必须**真的停帧，否则后台耗电 |

入口也不同：桌面是 `int main()`，Android 是 `void android_main(android_app*)`。
最小改动是头文件里给一个 `AINO_ANDROID_MAIN` 宏，把 `int main()` 包成 `android_main`
（转屏时 `android_main` 会被重新调用，得做成可重入的）。

### 2.2 文字：NDK 只给"选字体"，不给"画字形"

这是最容易被误判的一处。NDK 确实有字体 API —— `<android/font.h>`、`<android/font_matcher.h>`、
`<android/system_fonts.h>`，**API 29+**：

```c
AFontMatcher* m = AFontMatcher_create();
AFontMatcher_setStyle(m, AFONT_WEIGHT_NORMAL, false);
uint32_t run = 0;
AFont* f = AFontMatcher_match(m, "sans-serif", utf16_text, len, &run);
const char* path = AFont_getFontFilePath(f);   // /system/fonts/NotoSansCJK-Regular.ttc
size_t idx = AFont_getCollectionIndex(f);      // .ttc 里的第几个字面
```

它给你的是**字体文件路径 + 字面序号**，**没有** `AFont_rasterize`。字形位图仍然要自己来。

所以 Android 文字 = **`AFontMatcher`（选字体）+ FreeType（光栅化）**，和 X11 后端的结构一模一样，
只是把 fontconfig 换成 Android 的 matcher。这一步是净收益：

- `AFontMatcher_match` 接受一段 UTF-16 文字，返回 `runLength` —— 它直接告诉你"这一段能用这个字体画"。
  X11 那边为了拼回退链，自己写了 `FcFontSort` + charset 覆盖判定（`12439–12500`，约 60 行）；
  Android 上这段全删。
- 中文/日文/韩文/emoji 的回退是系统配置好的，不用猜 `NotoSansCJK` 和 `NotoColorEmoji` 谁先谁后。

**FreeType 从哪来**：X11 后端是 `dlopen("libfreetype.so.6")`（`12205`）。Android 上这条路**大概率走不通**：
Android 7 起应用进程有 linker namespace 隔离，只有 `public.libraries.txt` 里的库能 `dlopen`，
`libft2.so` 是平台私有库。**需实测**（个别 ROM 可能放行）。所以：

- 正常路径：**静态链一份 FreeType**。这是 Android 后端唯一的新增构建依赖，
  `arm64-v8a` strip 后约 400–600 KiB。它只影响 Android 构建，不影响"单头文件、零依赖"在其他平台的含义。
- 备选：`FT_Load_Char` 只用到很小一部分 API，理论上可以裁到只有 `truetype` + `smooth` 两个模块。

**颜色 emoji** 另说：`FT_LOAD_COLOR` 拿到的位图是 BGRA，`Canvas` 能直接合（通道顺序正好一致），
但尺寸是 64×64 起步、需要单独一条缓存路径。P0 先画豆腐块，P3 再说。

**为什么不走 JNI `android.graphics.Paint`**：一次 `drawText` 到一个临时 `Bitmap` 再读回来，
单次 20–100 µs 量级（JNI 往返 + Bitmap 分配 + 像素回读）。设置页一屏 200 段文字，
就是 4–20 ms —— 这一项就把整帧预算吃光了。排除。

### 2.3 输入法：零 Java 的承诺在这里破

Windows 走 IMM32，X11 走 XIM，Cocoa 走 `NSTextInputClient` —— 都是"窗口系统顺手给的"。
Android 的软键盘不是：它要给系统一个 `InputConnection`，而这个东西**只有 Java 能实现**。

NDK 侧**没有任何 IME API**。现成的桥只有两个，都要 Java 参与：

| 方案 | 形态 | 代价 |
|---|---|---|
| `native_app_glue` + 自己写 | 一个 60–80 行的 Java 类继承 `BaseInputConnection`，JNI 回传 `commitText` / `setComposingText` / `sendKeyEvent` | 代码量不大，但要处理组字串、选区、预测输入、键盘弹出时 `adjustResize` |
| **`androidx.games:game-textinput`** | 官方 AGDK 组件：Java 侧一个 `InputEnabledTextView`，C 侧 `GameTextInput_init/setInputConnection/setEventCallback/processEvent/showIme/hideIme` | 一个 AAR 依赖 + Prefab 接入；但组字串、选区、组字区（拼音的"未上屏"部分）都由它处理 |

推荐第二个。它的状态结构里正好有 `composing_region` 和 `selection`，能直接对上库里已有的契约：
`Ui::Impl::ime_owner`（`4065`，表示"本帧有输入框接管，组字串自己画"）和 `ime_comp`（`4061`，组字串内容）。
Windows 后端在 `11272` 一带处理组字串，Android 照着填即可。

**这一项的代价要写在明处**：Android 后端不再是"纯 C++ 单头文件"。要么接受 Java 文件，
要么接受"没有软键盘"（外接键盘、扫码枪、遥控器场景仍可用）。这不是能绕过去的取舍。

---

## 3. 两个看着像、实际有坑的地方

### 3.1 触摸与滚动：库是鼠标/滚轮模型

`Input`（`struct Input`）里有 `down/pressed/released/double_click/wheel`，滚轮是**离散增量**。
Android 没有滚轮，只有：

- **拖动滚动**：手指按下 → 移动 → 抬起，中间还要有惯性（fling）。库里没有这个模型。
  最小可行做法是后端把拖动增量合成成 `wheel`（每 40 逻辑像素 = 1 格），让现有 `begin_scroll/end_scroll`
  继续工作。缺点是没有惯性，手感像"拖窗口滚动条"。
- 正确做法是在滚动容器里加一条触摸手势路径（按下 8px 内不滚，超出后跟手，抬手按速度衰减）。
  这是**组件层**改动，和平台无关 —— 桌面触屏也应该受益。
- 命中目标：现有行高 64/76 够大，但 `-`/`+` 这类小按钮在 3x 屏上仍然只有 20 逻辑像素宽。
  AGENTS.md 没有触摸尺寸规范，得补一条（≥44 逻辑像素）。
- **悬停语义失效**：`hover_skip`（`10718`）那套"扫过同一控件不重绘"依赖鼠标停留，
  手指按住就是按住。触摸下应改成 `pressed` 态驱动重绘，`hover` 一律视为假。

单指映射到 `mouse`，`ACTION_POINTER_DOWN/UP` 丢弃（不做双指缩放），这是 P2 的范围。

### 3.2 性能预算：像素数是最大的敌人

库按**物理像素**光栅。设置页在桌面上是 1180×760 逻辑像素；在 360dp 宽的手机上，逻辑尺寸只有 360，
但 3x 屏意味着 **360×3 = 1080 物理像素**，2x 屏也是 720。整屏 1080×2400 = **2.59 M 像素**，
桌面参考尺寸 1180×760 = 0.90 M，**2.9 倍**。

把仓库里已有的测量数字套上去（都是桌面 x86、单核）：

| 项目 | 出处 | 桌面实测 | 折算到 1080×2400 |
|---|---|---|---|
| 全屏上屏（拷贝 + 提交） | `docs/10` §2「690×450 = 290µs」 | 0.94 ns/px | **≈ 2.4 ms/帧**（若直接画进锁定缓冲，可省掉拷贝部分） |
| 阴影（逐像素直算） | `docs/10` §4.3「6.5 ns/px + 51µs」 | 6.5 ns/px | 900×600 弹窗 blur 90 → 包围盒 963k px → **≈ 6.3 ms**（掩膜缓存命中后 ≈ 1/3） |
| 圆角矩形填充 | `docs/10` §4.2「1.6 ns/px」 | 1.6 ns/px | 一屏卡片 ≈ 1.5 M px → **≈ 2.4 ms** |

手机上再乘一个"ARM 大核 vs 桌面核心"的系数 —— 这个系数我**没有实测数据，不编造**。
但方向是明确的：**桌面上就已经贴着 16.6 ms 的边，手机只会更差。**

所以 Android 后端不能原样全屏重绘。三个层次的办法，按性价比排：

1. **渲染密度上限**（推荐，改一处）。`ANativeWindow_setBuffersGeometry(w/k, h/k)` 让 SurfaceFlinger
   把低分辨率缓冲放大到屏幕 —— 系统替你做了缩放，一分钱不花。k=2 时像素数 /4。
   代价是文字发虚（3x 屏用 1.5x 渲染，细笔画糊）。所以要加一个 `WindowConfig::render_scale`
   （0 = 跟随设备，0.5/0.667 可选），并且**只对内容缩放、不对布局缩放** —— 逻辑坐标不变，只是 `Canvas` 变小。
   这个能力对桌面高 DPI 屏同样有用，不是 Android 专属补丁。
2. **脏矩形**（改动大）。现在 `x11_present`（`13032`）整块 `XPutImage`、Windows 整块 `BitBlt`，
   没有脏矩形机制。加"本帧改动包围盒"需要贯穿 `Canvas` 的每个写入点，风险大、回归面广。
   **不建议在 Android 适配里顺手做**，它是独立的一次改造。
3. **少画**。悬停节流（`18% → 1.6%`，`docs/10` §6）已经做了；手机上主要是阴影 ——
   `pop_shadow` level≥2 的 90 blur 在手机上应该降到 level 1 或干脆不用（iOS 的 sheet 也没有大投影）。

---

## 4. 已排除的方案

| 方案 | 排除理由 |
|---|---|
| JNI `android.graphics.Paint` 逐段画字 | 单次 20–100 µs，一屏 200 段就 4–20 ms（§2.2） |
| 内嵌 Skia | 二进制 +3–8 MiB，构建复杂度超过整个库；与"零依赖"直接冲突 |
| EGL + OpenGL ES 做后端 | 光栅逻辑要重写成 shader 或"上传纹理再画 quad"。前者是重写整个库，后者只是把 CPU 的像素搬到 GPU —— 何必。除非将来做脏矩形/合成，EGL 才有意义 |
| 在 Android 上跑 X11（Termux:X11 之类） | 依赖外部 X server APK，不是"支持 Android"，是"绕开 Android" |
| 用 `libft2.so`（平台私有库）代替静态 FreeType | Android 7+ namespace 隔离，私有库不可 `dlopen`（**需实测**，个别 ROM 例外）。即使能跑也不该依赖未公开 ABI |
| 复用 X11 后端代码 | X11 那 1492 行里约 900 行是字体/窗口/输入，Android 一样都用不上；能复用的只有 `Canvas` 和 `WakeSignal`，而它们在平台无关区 |

---

## 5. 分阶段路线

### P0 能画能跑（1–2 天）

- `AINO_PLATFORM_ANDROID` 宏（`48`），通道顺序走 RGBA 分支（`1677`）
- `WindowConfig::native_surface` + 构造分支；`ANativeWindow_setBuffersGeometry`
- `poll/render/run` 三个方法：`ALooper_pollOnce` + `ALooper_addFd(wake pipe)` + `lock/unlockAndPost`
- 文字：FreeType 静态 + `AFontMatcher` 选字体，先只做**单字体、无回退**（ASCII + 中文各挑一个）
- 验收：`primitives_demo` 在真机上画出图元与文字，静置 CPU 0%

### P1 能打字（2–3 天）

- `game-textinput` 接入（Java 侧 `InputEnabledTextView` + C 侧回调）
- 组字串填 `ime_comp` + `ime_owner`；键盘弹出/收起与光标位置走 `Ui::Impl::ime_pos` 回调（`4056`，Windows/X11 后端在 `10794`/`10799` 接的同一根线）
- 按键映射：`AKeyEvent` 的 keycode（`AKEYCODE_DEL`、`AKEYCODE_DPAD_*`、`AKEYCODE_ENTER`）
  → `KEY_*` 枚举。注意 Android 的 `BACK` 要和 `Ui` 的 Esc 语义对齐
- 验收：`gallery` 的输入框能打中文（拼音组字上屏）、能退格、能 Ctrl+A/V/C

### P2 触摸可用（3–5 天，组件层）

- 触摸滚动手势 + 惯性（`begin_scroll` 那套之上）
- 触摸下的 `pressed`/`hover` 语义修正；`hover_skip` 在触摸设备上默认关
- 命中目标最小尺寸；安全区 insets（状态栏/手势条）
- 验收：不接键盘，用手指走完 `sidebar_demo` 的每个开关和 `ssh_client` 的列表滚动

### P3 打磨（不封顶）

- 颜色 emoji（`FT_LOAD_COLOR`）、字重映射（`AFontMatcher_setStyle`）、`render_scale` 的自动选择
- 剪贴板：`JNI ClipboardManager`（Android 10+ 后台读剪贴板受限，只能在前台读）
- 多点触控（双指缩放 `function_viewer` 的公式视图）、长按菜单（= 右键）
- `set_title` / `set_size` 直接 no-op（系统控制）

---

## 6. 与三条承诺的冲突（诚实清单）

| 承诺 | 在 Android 上 |
|---|---|
| **单头文件** | 主体仍然是。但要么加一个 Java 文件（IME），要么放弃软键盘。`android_main` 与 manifest 也必须由使用方提供 |
| **零依赖** | 破一次：FreeType 静态库（只影响 Android 构建）。IME 若用 `game-textinput` 再加一个 AAR |
| **空闲 0% CPU** | **能守住**。`ALooper_pollOnce` 带 timeout 时阻塞在 kernel，`need_frame` 为假就不出帧 —— 和现在 `x11` 的 `wake_signal.wait()`（`13500`）结构一致。注意 `APP_CMD_PAUSE` 必须真停帧 |
| **频繁交互低 CPU** | 打折。桌面 2.9× 像素差的账，靠 `render_scale` 补回来一部分 |

---

## 7. 真机验收清单（NDK 到位后照着跑）

1. `arm64-v8a` 与 `armeabi-v7a` 都能编（`AFont*` 是 API 29 的公开 NDK 接口，两个 ABI 都有）
2. 静置 60 秒，`dumpsys cpuinfo` 里本进程 CPU < 0.5%
3. 转屏三次：不崩、不闪白、输入框内容还在
4. 切后台 30 秒再回来：画面正常、CPU 归零
5. 打中文：拼音组字上屏正确、候选框不遮挡光标
6. 手指滚动长列表：跟手、有惯性、不误触
7. 1080×2400 设备上量一帧耗时（`AINO_FRAME_LOG=1`），确认 `render_scale` 该取多少
8. `ctest` 里的 49 项里，`raster_equiv` / `shadow_mask` / `icon_mask` 应当**不需要改动就能过**
   （纯 CPU、无窗口）—— 如果过不了，说明改动碰到了平台无关区，那是设计错了

---

## 8. 要新增/修改的 API（最小集）

```cpp
struct WindowConfig {
    // ...
    void* native_surface = nullptr;   // 非空 = 使用外部提供的窗口；Android 必填，其它平台忽略
    float render_scale   = 0.0f;      // 0 = 跟随设备；<1 = 低分辨率渲染后由系统放大
};
```

两个字段，默认值都保持"和现在完全一样"。不需要新的类、新的入口函数、新的回调签名。
这也是我判断这次适配"不会伤到现有架构"的依据 —— 如果最后发现要动到 `Ui` 或 `Canvas` 的接口，
说明方案选错了，应该回头重选。
