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
| [05-containers.md](05-containers.md) | 容器类：`panel` `card` `separator` `badge` `progress` `progress_ring` `skeleton` |
| [06-interactive.md](06-interactive.md) | 按钮、导航、输入、选择、数值、分组与复合表单、搜索框、颜色池 |
| [07-display.md](07-display.md) | 头像、活动指示、提示、路径、快捷键、折叠标题、表格、趋势图、条目、空状态、分页、图片占位 |
| [08-overlays.md](08-overlays.md) | 浮层类：`tooltip` `menu` `dialog` `toast`；另含 `sheet` `popover` `hover_card` `alert_dialog` `drawer` `context_menu` `menubar` |
| [09-primitives.md](09-primitives.md) | 图元与度量：几何、连续圆角、渐变、阴影、背景模糊、系统图标、文字与度量 |
| [10-performance.md](10-performance.md) | 2026-10-17 历史性能基准（静置 / 连续重绘 / 交互）；§3 开头写明覆盖范围 |
| [11-motion.md](11-motion.md) | 动画（`tween` / `tween_color` / `fade` / `smooth` / 缓动）与 `Transform`（平移 / 缩放 / 旋转） |
| [12-component-index.md](12-component-index.md) | 当前公开 API、Gallery 组合示例、兼容接口边界与检查工具 |

---

## 当前示例

| 源文件 | 内容 |
|---|---|
| `gallery.cpp` | 15 分类、43 卡片、搜索、主题与强调色、独立素材子窗口 |
| `primitives_demo.cpp` | 圆角、线宽、透明度；几何、阴影、裁剪、变换、系统图标与模糊 |
| `sidebar_demo.cpp` | 导航与通知偏好 |
| `pomodoro.cpp` | 番茄钟：侧栏与工具栏、专注 / 休息、暂停续计、时长和外观设置 |
| `function_viewer.cpp` | 曲线与表达式、可滚动侧栏、默认暂停、采样缓存 |
| `ssh_client.cpp` | 可选 SSH/SFTP、后台传输与取消；依赖 libssh2，MSVC 使用 mbedTLS |

CMake 自动扫描顶层 `examples/*.cpp` 并创建 `aino_<文件名>` 目标，SSH 保留 `aino_ssh` 名称。
构建方法与输出位置见 [01](01-quickstart.md)。画廊卡片数量不等于公开 API 数量，
未展示的有效 API 仍由文档与探针维护。

## 常用约定

返回值、样式默认值与绘制顺序如下。

### 1. 返回值按组件语义解释

```cpp
bool button(...);          // true = 本帧被点击
bool input(...);           // true = 值发生了变化（不是"被点了一下"）
bool tabs(...);            // true = 选中项变了
```

判断依据是「**用户意图是否改变了一个持久值**」：

| 组件 | 返回值含义 |
|---|---|
| `button` `icon_button` `nav_item` | 本帧被点击（按下并在框内松开） |
| `input` `checkbox` `toggle` `slider` `radio_group` `stepper` `rating` `textarea` | 值变了 |
| `tabs` `segmented` | 选中索引变了 |
| `select` | 本帧提交了选择，重复选择同一项也返回 true |
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

输入每次改变文本都会返回 true，可在此时更新校验结果。

### 2. `Style` 是可选修饰器，不设即沿用主题

`Style` 里所有颜色默认是 `Color::none()`（全 0，含 alpha=0），
多数尺寸默认是 `-1.0f`；`press_scale=1.0f`、`line_gap=0.0f` 等有独立默认值。
组件读到一个「未设置」的字段就用自身或主题默认值，精确规则见 [02](02-style-and-theme.md)。

```cpp
ui.button("保存", box);                             // 使用主题默认
ui.button("保存", box, Style().primary());          // 显式指定变体
ui.button("保存", box, Style().primary().corner(6));// 变体 + 覆盖圆角
```

`Color::none()` 与 `Color::black()` 不同：前者是「没设置」，后者是「黑色」。

### 3. 浮层按实际绘制时机排序

菜单、tooltip、popover 等弹出内容推迟到帧末；dialog、sheet、drawer、alert_dialog、toast 直接绘制。
直接绘制的浮层放在普通内容之后，面板内自定义按钮紧接着绘制。
详情及内容回调的生命周期见 [08](08-overlays.md)。

---

## 空闲 0% CPU 的前提

aino 的主循环阻塞在 `WaitMessage` 上，**没有事件就不渲染**。
所以下面这些写法会让 CPU 从 0% 变成几十 %，且**画面上完全看不出来**：

| 会让 CPU 起飞的原因 | 正确做法 |
|---|---|
| 在帧回调里请求通用重绘 | 底层拒绝；局部动画用 `request_animation_frame()`，全局播放用 `set_animating()`，后台数据用 `win.wake()` |
| 在帧回调里每帧 `win.set_theme(...)` | `set_theme` 会比较值，但每帧构造主题仍是浪费，改成变化时调 |
| 给 `spinner` 或 `skeleton` 写 `Style().animate()` | 两者默认**只在鼠标悬停时**动；`Style().animate()` 对 spinner 可显式常驻，`skeleton` 仍受悬停控制。画廊预览统一悬停才动 |
| `input` / `textarea` 处于聚焦态 | 光标闪烁天生要帧，这是设计内开销；调 `set_anim_fps` 可降 |

Windows 回归检查空闲帧、CPU、交互动画和多窗口响应。历史逐组件耗时保留在
[10](10-performance.md)，不代表当前 mac 风格耗时。聚焦光标、显式 spinner 动画与应用主动播放会持续请求帧。

---

## 平台支持

| 平台 | 后端 | 验证范围 |
|---|---|---|
| Windows | GDI 字体与 CPU 光栅化，BitBlt 上屏 | 当前本地构建、数值探针、实窗像素、输入、空闲 CPU、多窗口测试 |
| Linux | X11，FreeType/fontconfig 运行时加载，缺失则 XFontSet | 仓库 CI 配置编译、开窗冒烟与几何像素断言；当前 mac 风格尚未完成 Linux 实机核对 |
| macOS | Cocoa / CoreText / CoreGraphics | 仓库 CI 配置编译、开窗冒烟与几何像素断言；当前 mac 风格尚未完成 macOS 实机核对 |

跨平台检查入口为 `tools/ci_smoke.cpp` 与 `tools/ci_pixels.cpp`，像素断言侧重几何、颜色、裁剪、阴影与文字覆盖。
Windows 额外通过 `tools/verify_gallery.py` 检查 19 项整窗行为；字形与实际系统效果仍需人工查看。
Linux/macOS 的输入、IME、剪贴板与多窗口不能由这些像素断言证明。
macOS 当前事件循环仍有轮询；Windows 的阻塞空闲结果不能直接外推到其它平台。

### 已知后端限制

- Linux 剪贴板读取与 XIM/HiDPI 行为尚未实机核对，中文依赖已安装的字体。
- macOS 剪贴板与部分修饰键路径尚不完整，Cocoa 上屏、交互及多窗口需真机核对；AppKit 窗口应在主线程管理。
- Windows 系统符号与 macOS 的 SF Symbols 字形不同，系统图标一致不等于跨平台像素相同。

### gallery 自己的两条工具钩子

gallery 展示常用控件，另有两个供工具使用的开关（完整 API 仍见组件文档）：

| 环境变量 | 作用 | 谁在用 |
|---|---|---|
| `AINO_LAYOUT_REPORT=<路径>` | 首帧写出卡片名称、所属分类、位置及尺寸，首行汇总分类数与卡片数（未搜索时为 `sections=15 cards=43`） | 布局诊断 |
| `AINO_START_PAGE=<序号或名字>` | 直接滚动到指定分类 | `tools/shot_pages.py` 按分类截图 |

分类、卡片数量、尺寸、间隔与搜索过滤由画廊探针检查。
组件状态、内容裁剪与交互由 `tools/probe/macos_gallery_probe.cpp` 验证。
`shot_pages.py` 只保存截图，文字排版与外观仍需人工查看。

### 回归与截图

```sh
python tools/regress.py
python tools/regress.py --only macos_gallery_probe
python tools/regress.py --full
python tools/perf_gallery.py
python tools/shot_pages.py
python tools/child_window.py --shot
```

`regress.py` 默认只运行 7 个核心交互探针；`--only` 指定探针，`--list` 列出名称，
`--full` 执行全部功能检查。探针产物已是最新时跳过编译，`--no-build` 完全禁止编译。
性能采样独立放在 `perf_gallery.py`，不再重复启动功能回归；输出单核及整机 CPU、
帧耗时和绘制耗时，JSON 默认保存在 `build/perf-gallery.json`。
`shot_pages.py` 保存分类截图，不能代替断言或人工排版核对。
`tools/probe/example_ui_probe.cpp` 检查函数侧栏、全部绘图模式的采样缓存、无效公式及输入快捷键隔离；`python tools/build_ssh.py ui`
构建 SSH 界面探针，覆盖 UTF-8、选区、撤销、粘贴、会话页签与弹窗输入隔离，不连接服务器。
显式使用 `build/ssh_ui_probe.exe --local-transfer <端口>` 才会连接 `127.0.0.1` 上的
本地测试服务器（`demo/demo`），检查后台上传 / 下载、取消、错误清理及 PTY 尺寸同步。
运行方法与独立测试目录设置见 [01](01-quickstart.md)。

系统链接库：Windows `user32/gdi32`；Linux `X11/dl`；macOS `Cocoa/CoreText/CoreGraphics`。
原始设计资料见 [macos27](macos27/README.md)。

---

作者：sudoevolve（[sudoevolve@gmail.com](mailto:sudoevolve@gmail.com)）· 许可证 [Apache-2.0](../LICENSE)
