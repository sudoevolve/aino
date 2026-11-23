# aino Wiki

正式教程以仓库中的源码与 API 为准；安装和构建从第一篇开始。

| 教程 | 内容 |
| --- | --- |
| [快速开始](01-quickstart.md) | 接入、构建、事件循环、SSH 与多窗口 |
| [样式与主题](02-style-and-theme.md) | Style、Theme、颜色与几何类型 |
| [布局与滚动](03-layout.md) | Stack、滚动容器、虚拟列表与分栏 |
| [文本](04-text.md) | 标签、说明、代码和文本缓存 |
| [容器](05-containers.md) | 卡片、面板、分隔线与进度 |
| [交互组件](06-interactive.md) | 按钮、输入、选择、表单与状态 |
| [展示组件](07-display.md) | 头像、表格、条目、图表与分页 |
| [浮层](08-overlays.md) | 菜单、对话框、提示和抽屉 |
| [图元与度量](09-primitives.md) | 几何、阴影、模糊、图像与系统图标 |
| [性能与资源](10-performance.md) | 按需绘制、缓存预算、采样方法及历史基准 |
| [动画与变换](11-motion.md) | 缓动、平移、缩放与旋转 |
| [组件索引](12-component-index.md) | 公开 API、组合示例与使用范围 |
| [平台与功能边界](14-platform-status.md) | 原生后端支持范围及已知限制 |
| [原生 Wayland](15-wayland.md) | 无 XWayland 构建、窗口、缩放、输入与剪贴板 |

## 开发约定

仅在一个编译单元定义 `AINO_IMPLEMENTATION`；应用持有业务数据，控件按帧构建。
`button()` 返回本帧点击，`input()` / `toggle()` 返回值是否改变。`select()` 返回是否提交选择。

`Window`、`Ui` 和绘制操作留在 UI 线程；macOS 使用主线程。后台业务调用 `wake()`，
并在窗口析构前退出。局部动画使用帧请求或 `request_frame_in()`，无需持续轮询。

同组设置放在一张卡片内，说明在左、控件在右；长内容使用滚动区域。
示例样式留在各自 `.cpp`，尺寸和验证约定见 [AGENTS.md](../../AGENTS.md)。

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

## 空闲与后台更新

无事件时窗口循环阻塞等待，后台数据改变后调用 `win.wake()`；动画按需请求下一帧。
不要在每帧重建主题或主动轮询无变化的业务状态。输入光标、显式动画与播放中的应用仍会产生定时帧，
不能将“无交互、无动画”下的空闲数据外推为所有场景 0% CPU。缓存、截止时间与采样口径见
[性能与资源](10-performance.md)，原生后端边界见[平台说明](14-platform-status.md)。

## 工程资料

[审查与修复记录](../reviews/README.md)与[设计研究](../design/README.md)单独维护。
日常开发只编译受影响目标；发布时运行回归。工具入口和截图方法见 [开发工具](../../tools/README.md)。
