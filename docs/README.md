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
| [03-layout.md](03-layout.md) | `Stack` 布局器（`vbox`/`hbox`/`next`/`fill`/`part`）与滚动容器 |
| [04-text.md](04-text.md) | 文本类：`label` `heading` `subheading` `muted_text` `code_text` |
| [05-containers.md](05-containers.md) | 容器类：`panel` `card` `separator` `badge` `progress` `skeleton` |
| [06-interactive.md](06-interactive.md) | 交互类：`button` `icon_button` `nav_item` `input` `checkbox` `toggle` `slider` `tabs` `segmented` `select` `radio_group` `stepper` `textarea` `rating` |
| [07-display.md](07-display.md) | 展示类：`avatar` `spinner` `alert` `breadcrumb` `kbd` `accordion` `table` `sparkline` `bars` |
| [08-overlays.md](08-overlays.md) | 浮层类：`tooltip` `menu` `dialog` `toast` |
| [09-primitives.md](09-primitives.md) | 图元与度量：`fill_rect` `stroke_rect` `fill_circle` `draw_line` `draw_text` `drop_shadow` `text_width` `text_height` `alert_height` |
| [10-performance.md](10-performance.md) | 逐组件性能实测（静置 / 连续重绘 / 交互）与归因分析 |
| [11-motion.md](11-motion.md) | 动画（`tween` / `tween_color` / `fade` / `smooth` / 缓动）与 `Transform`（平移 / 缩放 / 旋转） |

---

## 组件速查

**38 个组件 + 6 个图元**，按用途分五组。下表只列「一个有名字的组件」，
同一组件的参数组合不重复列（性能台架另测了 `empty` / `text_wrap` /
`skeleton_anim` / `button_full` 这 4 个变体，所以 [10-performance.md](10-performance.md)
的表是 47 行）。
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
| 交互 | `segmented` | 分段控件（等价 `tabs`） | 213 |
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

实测：**47 个组件全部**在静置 3 秒内 **0 帧 / 0.00% CPU**（`spinner`、`skeleton`
也不例外）。只有聚焦的输入框和显式 `.animate()` 的组件会持续要帧。

---

## 平台支持

| 平台 | 后端 | 状态 |
|---|---|---|
| Windows | GDI（`BitBlt` + 自绘 AA 光栅化） | **已验证**（Win11 / MSVC 与 MinGW g++） |
| Linux | X11 + XFontSet 系统字体 | **CI 通过**：编译 + xvfb 下开窗跑满 20 帧 |
| macOS | Cocoa / CoreText / CoreGraphics | **CI 通过**：编译 + 开窗跑满 20 帧（arm64 runner） |

上表不是「能编译就算过」—— `tools/ci_smoke.cpp` 必须真的建出窗口、跑满 20 帧、
自己退出（退出码 2/3 分别代表开不了窗 / 帧没跑满）。

**仍然没有验证的东西**（CI 跑不出来，得真机点一点）：像素与文字排版是否正确、
鼠标键盘与组合键、IME 组字、剪贴板、窗口缩放与 DPI。所以准确说法是
「三平台都能编译、都能开窗画帧」，不是「三平台表现一致」。

两个非 Windows 后端的具体状态（静读源码 + CI 实跑）：

- **Linux / X11**：后端结构是完整的（Display / XImage 后备缓冲 / XIM 输入 / XFontSet 画字）。
  已发现的问题：① `X11TextRenderer::draw` 里改完 `XSetClipRectangles` 后用
  `XSetClipMask(..., None)` 收尾，外层裁剪不会还原，嵌套裁剪会漏；
  ② 全程没有 `setlocale(LC_ALL, "")`，`XCreateFontSet` 在部分环境会直接失败（文字全不显示）。
  CI 只证明「跑得起来」，这两个问题它看不见。
- **macOS / Cocoa**：曾经一个 `.cpp` 都编不过；2026-10-20 用 CI 当编译器，一路修到能跑：
  ① `CocoaState` 补上 `pending_image` / `key_ctrl` / `key_shift` / `key_alt` 四个字段；
  ② arm64 不允许裸调 `objc_msgSend`，加 `oc_send<R>(...)` 模板统一转型；
  ③ 系统头的 QuickDraw `::Rect` / `::Style` 与 `aino::Rect` 撞名，包含前用宏改名；
  ④ 原来用 `setTag:` 存 `CocoaState*` —— NSView 没有这个 selector，真机一进来就抛异常，
  改成 `view → state` 的哈希表；⑤ runloop mode 名字从 `kCFRunLoopDefaultMode` 改回
  `NSDefaultRunLoopMode`。
  仍缺：修饰键没有任何代码写值（`ctrl_of()` 在 macOS 上恒为 false）、剪贴板两个方法
  直接返回空串、`drawRect:` 的像素正确性没人核对过。

链接依赖：Windows 只需 `user32` + `gdi32`；Linux 只需 `-lX11`；
macOS 只需 `-framework Cocoa -framework CoreText -framework CoreGraphics`。

---

作者：sudoevolve（[sudoevolve@gmail.com](mailto:sudoevolve@gmail.com)）· 许可证 [Apache-2.0](../LICENSE)
