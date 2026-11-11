# 12 · 组件与示例索引

当前库使用 macOS 风格语义色与控件外观。尺寸、颜色和交互以当前 API 文档与
`aino.hpp` 为准；[社区设计资料](macos27/README.md) 只作参考。背景材质使用模糊近似。

## 公开组件

| 用途 | API | 文档 |
|---|---|---|
| 文本 | `label`、`heading`、`subheading`、`muted_text`、`code_text`、`code_block` | [04 文本](04-text.md) |
| 容器与状态 | `panel`、`card`、`separator`、`badge`、`progress`、`progress_ring`、`skeleton` | [05 容器](05-containers.md) |
| 按钮与导航 | `button`、`icon_button`、`nav_item`、`toggle_button`、`button_group` | [06 交互](06-interactive.md) |
| 文本输入 | `input`、`search_field`、`textarea`、`input_group`、`input_otp`、`field` | [06 交互](06-interactive.md) |
| 选择与数值 | `checkbox`、`toggle`、`slider`、`tabs`、`segmented`、`select`、`radio_group`、`stepper`、`rating`、`toggle_group`、`toggle_group_multi`、`combobox`、`calendar`、`color_well` | [06 交互](06-interactive.md) |
| 展示与条目 | `avatar`、`spinner`、`alert`、`breadcrumb`、`kbd`、`accordion`、`table`、`sparkline`、`bars`、`item`、`empty_state`、`pagination`、`image_well` | [07 展示](07-display.md) |
| 浮层 | `tooltip`、`menu`、`dialog`、`sheet`、`toast`、`popover`、`hover_card`、`alert_dialog`、`drawer`、`context_menu`、`menubar` | [08 浮层](08-overlays.md) |
| 布局 | `vbox`、`hbox`、`begin_scroll`、`end_scroll`、`split_pane`、`aspect_rect` | [03 布局](03-layout.md) |
| 绘图与材质 | 几何图元、渐变、阴影、裁剪、文字度量、`system_icon`、`backdrop_blur`、`glass` | [09 图元](09-primitives.md) |
| 动画与变换 | `tween`、`tween_color`、`fade`、`smooth`、`push_transform`、`pop_transform` | [11 动画](11-motion.md) |

`card()` 是 `panel()` 的语义别名。`toggle()` 是开关，`toggle_button()` 是保留按下态的按钮。
`button_group()` 表示动作组；单选组使用 `toggle_group()`，多选组使用 `toggle_group_multi()`。
`menu()` 返回动作选择，`select()` 保存当前选中项，`combobox()` 另支持搜索。

## Gallery 中的组合示例

`examples/gallery.cpp` 包含 15 个分类、43 张卡片。卡片数量与公开 API 数量不同：
同一张卡片可以演示多个 API，一些卡片由基础组件与图元组合而成。

| 示例 | 实现边界 |
|---|---|
| 搜索、密码与输入状态 | 复用输入组件，应用管理文本、校验状态及可见性 |
| 日期与时间 | `calendar` 配合应用持有的时间值；没有独立 DatePicker 对象 |
| 强调色池 | `color_well` 提供点击事件，应用调用 `Theme::set_tint()` 并保存色板选择 |
| 通知 | `toast` 配合应用持有的消息与关闭状态；画廊不自动倒计时 |
| 排序表格 | 应用排序数据，再绘制表格；`table` 本身不排序 |
| 轮播与分页 | 应用持有当前索引，用按钮与分页器切换内容，没有隐式自动播放 |
| Markdown 卡片 | 展示应用组织的文本内容，核心库不含 Markdown 解析器 |
| 工具栏、侧栏 | 由按钮、导航条目、布局器与系统图标组合 |
| 虚拟列表 | 滚动容器配合可见区计算，只绘制可见行 |
| 新建素材子窗口 | 画廊负责窗口线程、状态同步与结果回填；Windows 已实测，Cocoa 不支持据此推断后台建窗 |

公开 API 未出现在 Gallery 中不代表废弃。完整参数和限制见对应章节，
不要用卡片文案推断库已提供解析器、文件拖放、系统颜色选择器等能力。

## 兼容接口与适用范围

- 旧主题字段 `background/card/primary/secondary/accent` 等仍用于兼容；强调色通过
  `set_tint()` 同步，不应只修改 `primary`。
- `icon_button(glyph, ...)` 与 `aino::icons::*` 保留 UTF-8 字符接口；当前控件和示例优先
  使用 `SystemIcon`，字符接口不保证跨平台字形一致。
- `row/column/push_padding/begin_group` 是早期布局接口；新代码优先使用 `Stack`。
- `drawer` 的 `side` 参数保留兼容，实际面板位置由 `box` 指定，没有隐式滑入动画。
- `image_well` 是展示占位，`color_well` 只报告点击；文件选择、拖放与系统颜色面板由应用接入。
- Linux/macOS 后端的已知限制见 [平台支持](README.md#平台支持)。

## 检查工具

| 入口 | 用途 |
|---|---|
| `tools/regress.py --only <名称>` | 定向运行组件、布局、编辑、浮层等断言；`--list` 查看名称 |
| `tools/probe/docs_smoke.cpp` | 编译文档涉及的 API 用法 |
| `tools/perf_gallery.py` | 实际画廊静置、移动、滚动、拖动与悬停的 CPU 采样 |
| `tools/perf_bench.py` | 单组件静置、连续绘制与交互耗时对比 |
| `tools/shot_pages.py` | 更新按分类截图，需人工检查文字和布局 |
| `tools/child_window.py` | Windows 素材子窗口的创建、取消与回填 |
| `tools/build_ssh.py` | 构建可选 SSH 应用及其探针，依赖准备见 [01](01-quickstart.md) |

IME、字体、终端字节回放与光栅基准等专项工具保留在 `tools/probe/`，
其中真实键盘和输入法检查需要人工运行，不放入默认回归。
