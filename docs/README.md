# aino 文档

> 单头文件跨平台 C++ GUI 库 —— 立即模式、零第三方依赖、事件驱动、空闲 0% CPU。

本目录是 aino 的完整使用文档。**组件逐项说明在 `04`~`08` 各篇**，
布局与滚动（含虚拟列表）在 `03`，大块文本的缓存策略在 `04`，编辑器撤销/选区在 `06`，
动画与变换在 `11`，样式与主题的完整属性表在 `02`，性能实测数据在 `10`。

---

## 目录

| 文件 | 内容 |
|---|---|
| [01-quickstart.md](01-quickstart.md) | 接入方式、编译命令、Hello World、主循环模型 |
| [02-style-and-theme.md](02-style-and-theme.md) | `Style` 全部字段与链式方法、`Theme` 全部 token、`Color`/`Rect` 全部方法 |
| [03-layout.md](03-layout.md) | `Stack` 布局器（`vbox`/`hbox`/`next`/`fill`/`part`）、滚动容器、`split_pane` 可拖动分栏 |
| [04-text.md](04-text.md) | 文本类：`label` `heading` `subheading` `muted_text` `code_text` `code_block` |
| [05-containers.md](05-containers.md) | 容器类：`panel` `card` `separator` `badge` `progress` `skeleton` |
| [06-interactive.md](06-interactive.md) | 交互类：`button` `icon_button` `nav_item` `input` `checkbox` `toggle` `slider` `tabs` `segmented` `select` `radio_group` `stepper` `textarea` `rating`；§16 另补 `toggle_button` `toggle_group` `toggle_group_multi` `button_group` `input_group` `input_otp` `field` `combobox` `calendar` |
| [07-display.md](07-display.md) | 展示类：`avatar` `spinner` `alert` `breadcrumb` `kbd` `accordion` `table` `sparkline` `bars`；§11 另补 `item` `empty_state` `pagination` |
| [08-overlays.md](08-overlays.md) | 浮层类：`tooltip` `menu` `dialog` `toast`；§5 另补 `popover` `hover_card` `alert_dialog` `drawer` `context_menu` `menubar` |
| [09-primitives.md](09-primitives.md) | 图元与度量：`fill_rect` `stroke_rect` `fill_circle` `draw_line` `draw_text` `drop_shadow` `text_width` `text_height` `alert_height` |
| [10-performance.md](10-performance.md) | 逐组件性能实测（静置 / 连续重绘 / 交互）与归因分析；§3 开头写明覆盖范围 |
| [11-motion.md](11-motion.md) | 动画（`tween` / `tween_color` / `fade` / `smooth` / 缓动）与 `Transform`（平移 / 缩放 / 旋转） |
| [12-shadcn-parity.md](12-shadcn-parity.md) | 与 shadcn/ui 的逐条对照（71 行）：哪些一对一、哪些只是名字不同、哪些没有 |

---

## 组件速查

**58 个组件 + 6 个图元**。下面的表列的是**有实测绘制耗时**的 38 个（0.3.0 之前就有的那批），
同一组件的参数组合不重复列 —— 性能台架另测了 `empty` / `text_wrap` / `skeleton_anim` /
`button_full` 这 4 个变体，所以 [10-performance.md](10-performance.md) 的表是 47 行。
0.3.0 新增的 20 个在[下一张表](#030-新增的-20-个组件)，它们还没有单独的耗时数字。
**「绘制 µs」列是实测的单帧组件绘制耗时**（460×300 逻辑窗口 / DPI 1.5，
详见 [10-performance.md](10-performance.md)）。

参考基准：一条 17 字的中文 `label` = **156 µs**，一个空窗口 = **0.2 µs**（上屏另计 290 µs）。

| 分组 | 组件 | 一句话 | 绘制 µs |
|---|---|---|---|
| 文本 | `label` | 正文，沿用主题前景色 | 156 |
| 文本 | `heading` | 标题，24px / 粗体 | 169 |
| 文本 | `subheading` | 副标题，18px / 600 | 168 |
| 文本 | `muted_text` | 次要说明，用 `muted_foreground` | 155 |
| 文本 | `code_text` | 小号文本 + 灰底衬 | 110 |
| 容器 | `panel` | 卡片底 + 描边 | 119 |
| 容器 | `card` | `panel` 的别名 | 116 |
| 容器 | `separator` | 1px 细线（自动判横竖） | **7.5** |
| 容器 | `badge` | 胶囊小标签 | 155 |
| 容器 | `progress` | 进度条，`value` 0..1 | **9.8** |
| 容器 | `skeleton` | 占位骨架，默认**静态** | **12.9** |
| 交互 | `button` | 按钮，6 种语义变体 | 137 |
| 交互 | `icon_button` | 正方形图标按钮 | 105 |
| 交互 | `nav_item` | 侧边栏条目（左对齐 + 选中态） | 160 |
| 交互 | `input` | 单行输入，光标闪烁 | 169 |
| 交互 | `checkbox` | 复选框 + 标签 | 165 |
| 交互 | `toggle` | 开关 | **16.6** |
| 交互 | `slider` | 滑杆，拖动改值 | **38.6** |
| 交互 | `tabs` | 页签 | 218 |
| 交互 | `segmented` | 分段控件（胶囊；`tabs` 是下划线） | 213 |
| 交互 | `select` | 下拉选择 | 172 |
| 交互 | `radio_group` | 单选组 | 215 |
| 交互 | `stepper` | 数值步进器 `[−] 值 [+]` | 118 |
| 交互 | `textarea` | 多行文本，光标闪烁 | 283 |
| 交互 | `rating` | 星级评分（自绘五角星） | 102 |
| 展示 | `avatar` | 圆形头像，取首字 | 148 |
| 展示 | `spinner` | 旋转指示器（**悬停才转**） | 66 |
| 展示 | `alert` | 提示块，标题 + 正文折行 | 288 |
| 展示 | `breadcrumb` | 面包屑 | 216 |
| 展示 | `kbd` | 键帽提示 | 101 |
| 展示 | `accordion` | 折叠面板 | 154 |
| 展示 | `table` | 表格（表头 + 行） | **603** |
| 展示 | `sparkline` | 折线走势 | **868** |
| 展示 | `bars` | 柱状图 | 129 |
| 浮层 | `tooltip` | 悬停气泡 | 9 ⚠️ |
| 浮层 | `menu` | 下拉菜单 | 165 |
| 浮层 | `dialog` | 模态框（遮罩 + 面板） | **2773** |
| 浮层 | `toast` | 轻提示条 | 411 |
| 图元 | `fill_rect` | 圆角矩形填充（自带 AA） | 51 |
| 图元 | `stroke_rect` | 圆角描边 | — |
| 图元 | `fill_circle` | 圆 | 96 |
| 图元 | `draw_line` | 线段 | 24 |
| 图元 | `draw_text` | 文本（无容器装饰） | 170 |
| 图元 | `drop_shadow` | 距离场阴影 | **616** |

> ⚠️ `tooltip` 的 9 µs 是**未显示态**（只做命中测试）。显示态约 300~400 µs，
> 与 `toast` 同量级 —— 测试脚本无法用 `PostMessage` 伪造持续悬停，
> 原因见 [10-performance.md](10-performance.md) 第 6.1 节。

### 0.3.0 新增的 20 个组件

这一批是照着 shadcn/ui 补的，**没有进性能台架**（所以没有耗时列），但每个都有专门章节。
名字被占用的那三个（`Switch` / `Toggle` / `Empty`）在
[12-shadcn-parity.md](12-shadcn-parity.md) 开头单独解释过。

| 分组 | 组件 | 一句话 | 章节 |
|---|---|---|---|
| 文本 | `code_block` | 等宽 + 行号 + C++ 着色；不滚动不折行 | [04 §6](04-text.md) |
| 交互 | `toggle_button` | 按下态按钮（shadcn 的 Toggle，**不是开关**） | [06 §16.1](06-interactive.md) |
| 交互 | `toggle_group` | 单选组，再点可取消 | [06 §16.2](06-interactive.md) |
| 交互 | `toggle_group_multi` | 多选组，`uint32_t` 位掩码（最多 32 项） | [06 §16.3](06-interactive.md) |
| 交互 | `button_group` | 连成一片的按钮，无持久选中态 | [06 §16.4](06-interactive.md) |
| 交互 | `input_group` | 带前后缀的输入框（`¥` / `元`） | [06 §16.5](06-interactive.md) |
| 交互 | `input_otp` | 分格验证码，只收数字 | [06 §16.6](06-interactive.md) |
| 交互 | `field` | 标签 + 控件槽 + 说明/错误，配 `field_height()` | [06 §16.7](06-interactive.md) |
| 交互 | `combobox` | 可搜索的下拉（输入即过滤） | [06 §16.8](06-interactive.md) |
| 交互 | `calendar` | 月历，纯整数日期运算 | [06 §16.9](06-interactive.md) |
| 展示 | `item` | 标题 + 描述 + 尾部，整行可点 | [07 §11.1](07-display.md) |
| 展示 | `empty_state` | 虚线框 + 标题 + 描述 | [07 §11.2](07-display.md) |
| 展示 | `pagination` | 页码器，槽位数恒定 7 个 | [07 §11.3](07-display.md) |
| 浮层 | `popover` | 点触发器弹任意内容，内容可交互 | [08 §5.1](08-overlays.md) |
| 浮层 | `hover_card` | 悬停出卡片，不抢点击 | [08 §5.2](08-overlays.md) |
| 浮层 | `alert_dialog` | 内建确认/取消，返回"点没点确认" | [08 §5.3](08-overlays.md) |
| 浮层 | `drawer` | 四边滑出，**只淡入不移位** | [08 §5.4](08-overlays.md) |
| 浮层 | `context_menu` | 区域内右键菜单 | [08 §5.5](08-overlays.md) |
| 浮层 | `menubar` | 横排菜单栏，划到隔壁自动切 | [08 §5.6](08-overlays.md) |
| 布局 | `split_pane` | 可拖动两栏，视觉 1px / 热区 7px | [03 §8](03-layout.md) |

---

## 三条贯穿全库的约定

读任何一篇之前，先记住这三条，能省掉大半困惑。

### 1. 返回值只有两种语义

```cpp
bool button(...);          // true = 本帧被点击
bool input(...);           // true = 值发生了变化（不是"被点了一下"）
bool tabs(...);            // true = 选中项变了
```

判断依据是「**用户意图是否改变了一个持久值**」：

| 组件 | 返回值含义 |
|---|---|
| `button` `icon_button` `nav_item` | 本帧被点击（按下并在框内松开） |
| `checkbox` `toggle` `slider` `radio_group` `stepper` `rating` `textarea` | 值变了 |
| `tabs` `segmented` `select` | 选中索引变了 |
| `accordion` | 展开态变了 |
| `menu` | 选中了某一项（同时写入 `*clicked_index`） |
| `dialog` | 当前是否处于打开态（不是"刚刚关闭"） |
| `item` `button_group` | 本帧被点击（`button_group` 同时写入 `*clicked_index`） |
| `toggle_button` `toggle_group` `toggle_group_multi` | 值变了 |
| `combobox` `calendar` `pagination` | 选中索引 / 日期 / 页码变了 |
| `alert_dialog` | **本帧是否点了确认**（不是"是否打开"） |
| `context_menu` | 选中了某一项（同时写入 `*clicked_index`） |
| `popover` | 当前是否处于打开态 |
| `drawer` | 当前是否处于打开态（点"关闭"那一帧仍然是 true） |
| `menubar` | 选中了哪个顶级菜单的哪一项（`MenuPick`，没选到就 `valid()` 为假） |
| `split_pane` | 分隔线位置变了（拖动中每帧都可能是 true） |
| `field` `empty_state` `code_block` `hover_card` `toast` `tooltip` | 无返回值 |

所以不要写 `if (ui.input(&email)) { 重新校验(); }` 之外的东西 ——
它每敲一个字都会返回 true，这是设计如此。

### 2. `Style` 是可选修饰器，不设即沿用主题

`Style` 里所有颜色默认是 `Color::none()`（全 0，含 alpha=0），
所有尺寸默认是 `-1.0f`。组件读到一个「未设置」的字段就用主题默认值。

```cpp
ui.button("保存");                                  // 全用主题默认
ui.button("保存", box, Style().primary());          // 显式指定变体
ui.button("保存", box, Style().primary().corner(6));// 变体 + 覆盖圆角
```

`Color::none()` 与 `Color::black()` 不同：前者是「没设置」，后者是「黑色」。

### 3. 浮层在帧末绘制，调用方不需要手工分层

`tooltip` / `menu` / `dialog` / `toast` 都是：**交互在帧内完成，绘制推迟到帧末**。
所以它们天然盖在所有内容之上，你在页面任何位置调用都不会被后面的内容盖住。

代价是：`dialog` 里如果要画自己的按钮，必须紧接着 `dialog()` 之后画 ——
那时已经在遮罩层之上，不会再被遮罩压暗。

---

## 空闲 0% CPU 的前提

aino 的主循环阻塞在 `WaitMessage` 上，**没有事件就不渲染**。
所以下面这些写法会让 CPU 从 0% 变成几十 %，且**画面上完全看不出来**：

| 会让 CPU 起飞的原因 | 正确做法 |
|---|---|
| 在帧回调里无条件 `ui.repaint()` | 只在真的需要下一帧时请求 |
| 在帧回调里每帧 `win.set_theme(...)` | `set_theme` 会比较值，但每帧构造主题仍是浪费，改成变化时调 |
| 给 `spinner` 或 `skeleton` 写 `Style().animate()` | 两者默认**只在鼠标悬停时**动；`animate()` 是"无条件常驻"的显式出口，非必要别写 |
| `input` / `textarea` 处于聚焦态 | 光标闪烁天生要帧，这是设计内开销；调 `set_anim_fps` 可降 |

实测：**台架上 47 项（46 个组件 + 空基线）全部**在静置 3 秒内 **0 帧 / 0.00% CPU**（`spinner`、`skeleton`
也不例外）。只有聚焦的输入框和显式 `.animate()` 的组件会持续要帧。

---

## 平台支持

| 平台 | 后端 | 状态 |
|---|---|---|
| Windows | GDI（`BitBlt` + 自绘 AA 光栅化） | **已验证**（Win11 / MSVC 与 MinGW g++，47 项回归全绿） |
| Linux | X11 + Xft（缺 libxft-dev 时退回 XFontSet 点阵） | **CI 通过**：编译 + xvfb 开窗跑满 20 帧 + **30 项像素断言** |
| macOS | Cocoa / CoreText / CoreGraphics | **CI 通过**：编译 + 开窗跑满 20 帧 + **30 项像素断言** |

上表不是「能编译就算过」：

- `tools/ci_smoke.cpp` 必须真的建出窗口、跑满 20 帧、自己退出
  （退出码 2/3 分别代表开不了窗 / 帧没跑满）；
- `tools/ci_pixels.cpp` 必须让 30 项断言全过，它**直接读后备缓冲**
  （`Window::Impl::canvas.px`，三个后端都往同一块缓冲里画），不依赖任何窗口抓图 API。

### 跨平台像素断言怎么做到「同一个标准、三种字形」

`tools/verify_gallery.py` 那 18 项是在 Windows 真桌面上用 `PrintWindow` 抓窗口，
抓的是「整窗长什么样」，换个平台就没法比 —— 三个平台的字形光栅完全不同
（GDI 灰度 AA / Xft / CoreText），同一段字的像素必然对不上。

它补的是离屏探针补不上的那一层：探针的 `impl->canvas` 是自己塞的，绕过了
`WM_PAINT`、DPI 缩放和 `BitBlt`。所以这 18 项验的是「导航右侧真有一条 1px 分界线、
换页之后页头真的换了字、对话框的遮罩真的连左侧导航一起压暗、Esc 之后遮罩真的撤掉」
—— 这些只有真窗口能验。

所以 `ci_pixels.cpp` 只断言**与字形无关**的性质：

| 断言类型 | 抓什么 bug |
|---|---|
| 实心填充的内部像素精确等于填充色（容差 0） | 颜色通道顺序搞反（BGRA / RGBA）、alpha 合成错 |
| 抗锯齿必须产生过渡像素（圆心/半径刻意取非整数） | 边缘硬切、AA 区间算错 |
| 裁剪区外一个像素都不许动（四条边分别验） | 裁剪没还原、溢出到框外 |
| 阴影峰值不超过传入 alpha（195±8 @ alpha=60 落纯白） | 历史 bug：叠 N 层把 46 累加到 148 |
| 文字画出了墨 + 墨迹不出框 + 与 `text_width` 同量级 | 字体没加载（全白）、量宽和画字用的不是同一套度量 |
| 控件绘制范围 == 传入 box | DPI 缩放乘两次、布局偏移 |
| 纯 clear 后整块缓冲 130 点采样全 == 背景色 | stride 算错、新缓冲没清零（「缩放变黑」） |

代价是它**证明不了**「字形排得对不对」——那仍然要人眼在真机上看。
Windows 侧另有 18 项整窗断言兜这一层。

### gallery 自己的两条工具钩子

gallery 是唯一同时用到全部控件的程序，所以它另外挂了两个只在跑工具时才生效的开关：

| 环境变量 | 作用 | 谁在用 |
|---|---|---|
| `AINO_LAYOUT_REPORT=<路径>` | 首帧把每一页每一条示例在**测量模式**下空跑一遍，写出「声明高度 / 实测高度 / 差值」，末尾一行 `# over=0 under=N total=M` | `regress.py` 的「卡片高度够」一项 |
| `AINO_START_PAGE=<序号或名字>` | 直接开在指定页 | `tools/shot_pages.py` 逐页截图 |

`over=0` 的含义是「没有任何一条示例的内容超出它自己的卡片下边框」。这条断言之所以
能成立，是因为卡片高度不再是手填的数字，而是 `max(声明高度, 实测高度)` —— 见
[03 布局](03-layout.md) 的 `begin_measure` / `end_measure`。`shot_pages.py` 不属于回归：
它只落图，判断仍然靠人眼，因为「好不好看」没有像素判据。

### 两个非 Windows 后端的真实现状

- **Linux / X11**：CI 的像素断言一上线就抓到两条真 bug，都已修：
  ① **文字一个像素都不显示** —— 几何图形是在**客户端 XImage** 里光栅化的、一帧结束才
  `XPutImage` 上屏，而文字走 `Xutf8DrawString` 直接画在**窗口**上，于是先出现、再被
  整块覆盖。改成画到 Pixmap → `XGetImage` 取回 → 逐像素 `blend` 进缓冲；
  ② **字体挑到退化字体** —— XLFD 有 14 个字段、第 7 个才是 `pixel_size`（原来写在第 8 个
  `point_size` 位），精确匹配从来没命中过，一路落到 `"*"`，在 runner 上挑到 1px 宽的
  字体（`text_width("Hg") = 2`）。现在有候选链 + 「量一下 `Hg` 宽不宽」的体检。
  尚未验证：键盘/鼠标事件映射、XIM 输入法组字、剪贴板（`set_clipboard` 只声明所有权）、
  HiDPI、中文（裸 runner 没有中文字体，汉字会缺字）。
  **文字渲染已改为 Xft**（0.4.4）：核心 XLFD 字体只有 1bit 点阵、没有 12/13/14 这种
  连续字号，Ubuntu 桌面上请求 13px 会被服务端拿 12px 位图缩放上去 —— 中文挑全角点阵、
  拉丁字距被拉成等宽、字重完全拉不开，整屏像马赛克。Xft 走 **运行时 `dlopen("libXft.so.2")`**
  + `dlsym`，不依赖 `libxft-dev`、不依赖 `-lXft`（普通 Ubuntu 桌面只有运行时包，让用户
  去装开发包等于没修）；加载不到才退回点阵。
  `ci_pixels` 里那条「文字框里有几档灰度」的断言就是钉这件事的：1 档 = 点阵，≥4 档才
  算抗锯齿。
  再遇到文字问题时开 `AINO_X11_TRACE=1`：会把每个字体候选、体检宽度、Xft 字体指针、
  每次绘制的 pixmap 尺寸打到 stderr（并立即 fflush —— 崩溃时管道里的 stderr 是全缓冲的，
  不 flush 等于什么都没打）。
- **macOS / Cocoa**：曾经一个 `.cpp` 都编不过；2026-10-20 用 CI 当编译器，一路修到能跑，
  并跑通了 30 项像素断言（说明 CoreText 画字、裁剪、阴影这条链是真的对）：
  ① `CocoaState` 补上 `pending_image` / `key_ctrl` / `key_shift` / `key_alt` 四个字段；
  ② arm64 不允许裸调 `objc_msgSend`，加 `oc_send<R>(...)` 模板统一转型；
  ③ 系统头的 QuickDraw `::Rect` / `::Style` 与 `aino::Rect` 撞名，包含前用宏改名；
  ④ 原来用 `setTag:` 存 `CocoaState*` —— NSView 没有这个 selector，真机一进来就抛异常，
  改成 `view → state` 的哈希表；⑤ runloop mode 名字从 `kCFRunLoopDefaultMode` 改回
  `NSDefaultRunLoopMode`。
  仍缺：修饰键没有任何代码写值（`ctrl_of()` 在 macOS 上恒为 false）、剪贴板两个方法
  直接返回空串、以及「`CGImage` 交给 `drawRect:` 之后上屏是否正确」没人核对
  （像素断言读的是 CGContext 的缓冲，在 `drawRect:` 的上游）。

链接依赖：Windows 只需 `user32` + `gdi32`；Linux 是 `-lX11 -ldl`（libXft 走运行时
`dlopen`，不要开发包、也不要 `-lXft`）；
macOS 只需 `-framework Cocoa -framework CoreText -framework CoreGraphics`。

---

作者：sudoevolve（[sudoevolve@gmail.com](mailto:sudoevolve@gmail.com)）· 许可证 [Apache-2.0](../LICENSE)
