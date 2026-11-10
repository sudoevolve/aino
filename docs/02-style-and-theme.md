# 02 · 样式与主题

当前主题采用 macOS 风格，保留 shadcn 命名字段供已有应用使用。
尺寸和颜色以 `aino.hpp` 的主题工厂为准；资料来源与映射见 [设计资料](macos27/README.md)。
背景材质使用模糊近似，没有折射与色散。

`Style` 是每个组件的可选最后一个参数，`Theme` 是全局配色与度量。
本篇把两者的字段逐项列出，并给出每个字段的默认值与生效规则。

---

## 1. 基础类型

### 1.1 `Color`

```cpp
struct Color { uint8_t r, g, b, a; };
```

| 成员 | 说明 |
|---|---|
| `Color(r, g, b, a = 255)` | 直接构造，分量 0..255 |
| `Color::hex(0xRRGGBB, alpha = 255)` | 十六进制构造，`0x18181B` 这种写法 |
| `Color::rgba(r, g, b, a = 255)` | 同上，各分量独立给 |
| `Color::none()` | **未设置**（等价于 alpha=0），所有 `Style` 颜色字段的默认值 |
| `Color::white()` / `black()` / `transparent()` | 快捷构造 |

| 方法 | 说明 |
|---|---|
| `is_none()` | 判断是否「未设置」。**只看 `a == 0`，不检查 RGB** |
| `with_alpha(a)` | 换一个 alpha（0..255） |
| `scaled_alpha(f)` | 按倍率缩放当前 alpha（`0.55` = 压到 55%） |
| `alpha255(v)` | alpha 直接给 0..255，自动夹取 |
| `mix(other, t)` | 线性插值，`t=0` 返回自身，`t=1` 返回 `other`（含 alpha） |
| `lighten(amount)` | 每个通道朝 255 靠拢 `amount` 比例（0..1） |
| `darken(amount)` | 每个通道乘以 `1-amount` |
| `luminance()` | 感知亮度 0..1 = `(0.2126r + 0.7152g + 0.0722b)/255` |
| `rgb24()` | 打包成 `0xRRGGBB` |
| `==` / `!=` | 四通道全等 |

`Style` 中 alpha 为 0 的颜色表示未设置，不能用透明色覆盖组件默认背景。
图元的透明色则直接跳过绘制。`mix()` 同时插值 RGB 与 alpha；做淡入时使用
`accent.scaled_alpha(t)` 保持 RGB，避免从透明黑插值带来的暗色中间帧。

### 1.2 `Rect`

```cpp
struct Rect { float x, y, w, h; };
```

| 成员 | 说明 |
|---|---|
| `Rect(x, y, w, h)` | 左上角 + 宽高（逻辑单位） |
| `Rect::from_ltrb(l, t, r, b)` | 左上右下 |
| `Rect::centered(cx, cy, w, h)` | 由中心点构造 |
| `left()/top()/right()/bottom()` | 四条边坐标 |
| `cx()/cy()` | 中心点 |
| `empty()` | `w <= 0 || h <= 0` |
| `contains(x, y)` / `contains(Vec2)` | 点是否在内（**右/下边界不含**） |
| `intersects(other)` | 是否相交 |
| `inset(d)` / `inset(dx, dy)` | 向内收缩（负值等于 `expand`） |
| `offset(dx, dy)` / `offset(Vec2)` | 平移 |
| `expand(d)` | 向外扩张（= `inset(-d)`） |
| `cut_left(n)` | **切走**左边 n 宽，自身左边界右移、宽度减小，返回切下的那块 |
| `cut_right(n)` / `cut_top(n)` / `cut_bottom(n)` | 同上，其余三个方向 |
| `pad(dx, dy)` | 同 `inset`，语义化别名 |
| `center_y(h)` / `center_x(w)` | 在本盒子里**跨轴居中**一个 h 高 / w 宽的盒子，另一根轴照抄 |
| `center(w, h)` | 居中一个 w×h 的盒子 |
| `intersect(other)` | 交集；不相交时宽高都为 0（`empty()` 为真） |

`cut_*` 是手写坐标时最省事的工具（会修改自身），`inset/offset/center_*` 是纯函数。
`center_y/center_x` 专治「布局器切出来的盒子比控件大」：hbox 的横切片是整行高，
往里放一个方形按钮或一行文字时，别再写 `r.y + 17` 这种常量（行高一改就偏）。

### 1.3 其它枚举

```cpp
enum class Align  { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom };
enum class Cursor { Arrow, Hand, IBeam, ResizeH, ResizeV, Text };

enum Key {
    KEY_BACKSPACE = 8, KEY_TAB = 9, KEY_ENTER = 13, KEY_ESC = 27, KEY_SPACE = 32,
    KEY_DELETE = 127,
    KEY_LEFT = 0x1000, KEY_RIGHT, KEY_UP, KEY_DOWN,
    KEY_HOME, KEY_END, KEY_PAGEUP, KEY_PAGEDOWN,
    KEY_SHIFT, KEY_CTRL, KEY_ALT, KEY_SUPER,
    KEY_F1 ... KEY_F12, KEY_COUNT
};
```

### 1.4 `Input`（帧内读取）

```cpp
const aino::Input& in = ui.input();
```

| 字段 | 说明 |
|---|---|
| `mouse` | 逻辑坐标（已除 DPI 缩放） |
| `mouse_prev` / `mouse_delta` | 上一帧位置 / 本帧位移 |
| `wheel` | 本帧滚轮增量，向上为正 |
| `down[3]` / `pressed[3]` / `released[3]` | 左中右：按住 / 本帧按下 / 本帧抬起 |
| `double_click` / `click_count` | 双击标志 / 连击次数 |
| `key_down[]` / `key_pressed[]` | 按键状态（索引用 `KEY_*`） |
| `text_input` | 本帧输入的 UTF-8 文本（已过滤控制字符） |
| `shift()/ctrl()/alt()/super()` | 修饰键快捷判断 |
| `mouse_drag_start` / `dragging` | 拖拽起点 / 是否在拖 |

---

## 2. `Style` 全部字段

所有字段默认值都表示「**没设置，用主题默认**」，这是它作为「可选修饰器」的基础。

| 字段 | 类型 | 默认值 | 含义 |
|---|---|---|---|
| `bg` | `Color` | `none()` | 填充色；`button` 这类组件会据此重新派生 hover/press 色 |
| `fg` | `Color` | `none()` | 前景色（文字/图标） |
| `border_color` | `Color` | `none()` | 描边色；设了它且 `border_width<=0` 时会自动补成 1px |
| `border_width` | `float` | `-1.0f` | 描边宽度；`<0` = 用组件默认（多数是 0 或 1） |
| `radius` | `float` | `-1.0f` | 圆角；`<0` = 用主题对应组件的默认圆角 |
| `pad_x` | `float` | `-1.0f` | 水平内边距（`input`/`select`/`table`/`nav_item` 等读它） |
| `pad_y` | `float` | `-1.0f` | 垂直内边距（主要 `textarea` 读） |
| `font_size` | `float` | `-1.0f` | 字号；`<=0` = 用组件/主题默认 |
| `font_weight` | `int` | `-1` | 字重 100..900；`<=0` = 默认；`>=600` 走粗体字面 |
| `italic` | `bool` | `false` | 斜体 |
| `disabled` | `bool` | `false` | 禁用：控件不响应交互，颜色压到 45% alpha |
| `variant` | `int` | `-1` | 语义变体 `0..5`；`-1` = 由组件决定默认 |
| `align` | `Align` | `Left` | 水平对齐 |
| `valign` | `VAlign` | `Middle` | 垂直对齐 |
| `align_set` | `bool` | `false` | 区分「用户显式指定」与「组件默认」—— 组件靠它决定要不要强制居中 |
| `valign_set` | `bool` | `false` | 同上，垂直方向 |
| `muted` | `bool` | `false` | 前景改用 `muted_foreground` |
| `placeholder` | `std::string` | `""` | 占位文本（`input` / `textarea` / `select` 用） |
| `wrap` | `bool` | `false` | 自动折行；`\n` 永远生效 |
| `line_gap` | `float` | `0.0f` | 行距附加量（0 = 直接用字体的行高） |
| `animated` | `bool` | `false` | spinner 可常驻；skeleton 需显式开启，且仍只在悬停时动 |
| `password` | `bool` | `false` | 单行输入以圆点显示，编辑值保留原文 |
| `row_sep` | `bool` | `false` | 条目行底分隔线 |
| `blur_radius` | `float` | `-1` | `glass()` 的背景模糊半径 |
| `press_scale` | `float` | `1.0f` | 按下时整体缩放，`1.0` = 不缩放 |

### 2.1 链式方法

| 方法 | 等价于 | 说明 |
|---|---|---|
| `color(c)` / `text_color(c)` | `fg = c` | 前景色 |
| `background(c)` / `fill(c)` | `bg = c` | 填充色 |
| `stroke(w, c)` | `border_width = w; border_color = c` | 描边 |
| `corner(r)` | `radius = r` | 圆角 |
| `padded(x, y)` | `pad_x = x; pad_y = y` | 内边距 |
| `padding(v)` | `pad_x = pad_y = v` | 等距内边距 |
| `font(size)` | `font_size = size` | 字号 |
| `weight(w)` | `font_weight = w` | 字重 |
| `bold()` | `font_weight = 700` | 粗体 |
| `semi_bold()` | `font_weight = 600` | 半粗 |
| `medium()` | `font_weight = 500` | 中等 |
| `normal_weight()` | `font_weight = 400` | 常规 |
| `italicized()` | `italic = true` | 斜体 |
| `left()` / `center()` / `right()` | `align = ...; align_set = true` | 水平对齐，**会置 `align_set`** |
| `top()` / `middle()` / `bottom()` | `valign = ...; valign_set = true` | 垂直对齐 |
| `muted_text()` | `muted = true` | 前景用 muted 色 |
| `hint(s)` | `placeholder = s` | 占位文本 |
| `off()` | `disabled = true` | 禁用 |
| `secret(on = true)` | `password = on` | 密码输入 |
| `row_lines()` | `row_sep = true` | 条目分隔线 |
| `blur(b)` | `blur_radius = b` | 材质模糊半径 |
| `scale_on_press(s)` | `press_scale = s` | 按压缩放 |
| `wrap_text(v = true)` | `wrap = v` | 自动折行 |
| `line_spacing(g)` | `line_gap = g` | 行距 |
| `animate(v = true)` | `animated = v` | spinner 显式持续播放；skeleton 仍只在悬停时推进 |

### 2.2 语义变体

```cpp
Style().primary()      // 0 Default     强调色填充
Style().secondary()    // 1 Secondary   浅灰填充
Style().outline()      // 2 Outline     透明底 + 1px 描边
Style().ghost()        // 3 Ghost       透明底无描边，只有 hover 才出现底色
Style().destructive()  // 4 Destructive 红底（危险操作）
Style().link()         // 5 Link        透明底，前景用 primary，无内边距感
```

变体的取色完全来自 `Theme`，对照表：

| 变体 | 底色 | 前景 | 描边 | hover / press 怎么来 |
|---|---|---|---|---|
| `Default` | `primary` | `primary_foreground` | — | 由 `primary` 派生 |
| `Secondary` | `secondary` | `secondary_foreground` | — | 由 `secondary` 派生 |
| `Outline` | 透明 | `foreground` | `border` 1px | 底色用 `accent` |
| `Ghost` | 透明 | `foreground` | — | 底色用 `accent` |
| `Destructive` | `destructive` | `destructive_foreground` | — | 由 `destructive` 派生 |
| `Link` | 透明 | `primary` | — | 底色用 `accent` |

**hover / press 的派生规则**（`detail::hover_color` / `press_color`）：

```
luminance > 0.5  →  亮底压暗：hover = darken(0.07)，press = darken(0.13)
luminance <= 0.5 →  暗底提亮：hover = lighten(0.14)，press = lighten(0.24)
```

所以**自定义 `bg` 也自动拥有正确的 hover/press**，不需要手写三套颜色。

### 2.3 显式覆盖优先

`make_skin` 的覆盖顺序（后者赢）：

```
variant 决定 base/fg/border
  → st.bg 覆盖 base（并重算 hover/press）
  → st.fg 覆盖 fg
  → st.muted 覆盖 fg 为 muted_foreground
  → st.border_color 覆盖 border（并补 border_w=1）
  → st.border_width >= 0 覆盖 border_w
  → st.radius 覆盖 radius
```

也就是说 `Style().primary().fill(red)` 得到的是**红底 + primary 的前景色**，
不是红色变体。要完整的红色语义用 `Style().destructive()`。

---

## 3. `Theme` 颜色与度量

### 3.1 原生语义色

颜色以 RGBA 保存。透明标签和填充应合成在当前表面上，不等同于固定灰色。

| 字段 | 浅色 / 深色 | 用途 |
|---|---|---|
| `label` | 黑 / 白，alpha 217 | 主标签 |
| `label2` | 黑 alpha 128 / 白 alpha 140 | 次要文字 |
| `label3/label4/label5` | 黑 / 白，alpha 64/26/13 | 低强调文字 |
| `fill/fill2/fill3/fill4/fill5` | 黑 / 白，alpha 26/20/13/8/5 | 五级叠加填充 |
| `fill_hover/fill_press` | 黑 / 白，alpha 38/44 | 状态填充 |
| `window_bg` | `#FFFFFF` / `#1E1E1E` | 窗口背景 |
| `control_bg` | `#FFFFFF` / `#282828` | 控件表面 |
| `sidebar_bg/title_bg` | `#EAEAEA` / `#1E1E1E` | 侧栏和工具栏 |
| `popover_bg/tooltip_bg` | `#F6F6F6` / `#282828` | 浮层表面 |
| `separator` | 黑 / 白 alpha 26 | 分隔线 |
| `tint` | `#0088FF` / `#0091FF` | 默认强调色 |
| `tint_foreground` | 白色 | 强调色上的文字 |
| `track` | 黑 alpha 18 / 白 alpha 10 | 环形进度轨道 |
| `shadow_color` | 黑 alpha 64 / 150 | 阴影 |
| `tooltip_stroke` | 透明 / 白 alpha 38 | 气泡边缘 |
| `scrollbar` | 黑 alpha 128 / 白 alpha 140 | 滚动滑块 |
| `table_sel` | `#0165E2` / `#0158D2` | 默认活动选中行 |
| `table_sel_inactive` | 黑 alpha 36 / 白 alpha 46 | 失焦选中行 |
| `row_alt` | 黑 / 白 alpha 13 | 交替行填充 |
| `overlay` | `(171,171,171,153)` / `(23,23,23,158)` | 可选遮罩 token，dialog 不自动使用 |

强调色板包括 `blue/red/orange/yellow/green/mint/teal/cyan/indigo/purple/pink/brown/gray`。
`track_well()` 返回随浅深色变化的中性凹槽色，`is_dark()` 依据窗口背景亮度判断。

### 3.2 兼容字段映射

工厂在创建时设置这些映射；直接改任一字段不会自动重算其它字段。

| 兼容字段 | 来源 |
|---|---|
| `background/foreground` | `window_bg/label` |
| `card` | 浅色 `control_bg`；深色 `#252525`（panel 默认实际使用 `fill3`） |
| `card_foreground` | `label` |
| `popover/popover_foreground` | `popover_bg/label` |
| `primary/primary_foreground` | `tint/tint_foreground` |
| `secondary/secondary_foreground` | `fill/label` |
| `muted/muted_foreground` | `fill3/label2` |
| `accent/accent_foreground` | `fill3/label` |
| `destructive/destructive_foreground` | `red/白色` |
| `border/input` | `separator`（input 同为黑/白 alpha 26） |
| `ring` | 默认强调蓝 alpha 128/140 |

### 3.3 尺寸

| 档位 | Mini | Small | Medium | Large | XL |
|---|---|---|---|---|---|
| `size_h` | 16 | 20 | 24 | 28 | 36 |
| `size_radius` | 4 | 5 | 6 | 7 | 9 |
| `size_font` | 10 | 11 | 13 | 13 | 13 |
| `size_line` | 13 | 14 | 16 | 16 | 16 |
| `size_padx` | 7 | 10 | 16 | 16 | 16 |
| `size_pady` | 2 | 3 | 4 | 6 | 10 |

高度别名为 `control_h_mini/control_h_sm/control_h/control_h_lg/control_h_xl`。
默认 `radius=6`、`radius_sm=4`、`radius_lg=10`、`radius_xl=12`、`radius_full=9999`，
`font_size=13`、`line_height=16`、`pad_x=16`、`pad_y=4`、`corner_smoothing=0.6`。
组件可按尺寸使用自己的几何规则，不代表每个组件都直接读取所有 token。

### 3.4 字体

`font_family` 为空时使用平台默认无衬线字体。Windows 优先 Segoe UI Variable Text /
Segoe UI，中文由系统字体链接兜底；Linux 使用 fontconfig 的 sans 与逐字字体兜底；
macOS 使用 CoreText。`font_family_mono` 控制 `code_block` 和公开等宽绘图通道，
不改变普通 label/table 的比例字体。

### 3.5 自定义强调色

```cpp
aino::Theme t = aino::Theme::light();
t.set_tint(t.purple);
win.set_theme(t);
```

`set_tint(c)` 同步 `tint/tint_foreground`、`primary/primary_foreground`、`ring`、`table_sel`。
`accent` 仍保留中性悬停填充。应用保存色板索引，切换浅深色后对新主题再次调用此方法。
`same_as()` 比较全部颜色、尺寸表、度量与字体，`set_theme()` 不会对相同值重复排帧。

---

## 4. 属性生效规则

不同组件读取不同字段。常见规则如下，精确支持范围见组件章节：

### 4.1 常见字段

`disabled`（除纯展示类外）、`radius`、`bg`、`fg`、`border_color`、`border_width`、
`font_size`、`font_weight`、`align`、`valign`、`muted`、`variant`。

其中 `align`/`valign` 的生效方式分两种：

- `button` / `badge` / `icon_button` / `kbd`：**默认居中**，只有你显式调
  `.left()` / `.top()` 时才会改（靠 `align_set` / `valign_set` 判断）。
- `label` / `input` / `table` 等：直接沿用 `Style` 里的值。

### 4.2 部分组件才读的

| 字段 | 读取它的组件 |
|---|---|
| `pad_x` | `input` `textarea`(pad_y) `select` `nav_item` `table` `menu` `dialog` `alert` |
| `placeholder` | `input` `textarea` `select` |
| `wrap` | `label` `heading` `subheading` `muted_text` `code_text` `alert` `dialog`（经 `draw_text`） |
| `line_gap` | 同上 |
| `press_scale` | `button` `icon_button` `nav_item` |
| `animated` | `spinner` `skeleton` |
| `italic` | `draw_text` 支持，`label` 系列与 `nav_item` 会透传 |

### 4.3 组件的默认值表

组件默认圆角、字号和内边距见 `04`~`08` 的对应章节。

---

## 5. 系统图标

控件与示例优先使用 `SystemIcon`。Windows 使用 Segoe Fluent Icons，缺失时尝试
Segoe MDL2 Assets；macOS 使用 SF Symbols；X11 使用系统字体的 Unicode 符号回退。
不需要第三方图标字体。

```cpp
const aino::Rect box(24, 24, 24, 24);
if (ui.button("", box, aino::Style().ghost())) win.close();
ui.system_icon(aino::SystemIcon::Close, box.inset(4), ui.theme().label);
```

方形按钮需传相同宽高。`system_icon()` 只绘制，按钮负责命中和状态。
`icon_button(glyph, box)` 和 `aino::icons::*` 保留为 UTF-8 字符接口，字形依赖系统字体；
它们不会自动把任意字符替换为系统图标。
