# 02 · 样式与主题

`Style` 是每个组件的可选最后一个参数，`Theme` 是全局配色与度量。
本篇把两者的**全部字段**逐项列出，并给出每个字段的默认值与生效规则。

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

> **坑**：`is_none()` 判的是 `alpha == 0`。所以 `Color(255, 0, 0, 0)`（透明红）
> 会被当作「没设置」，组件会回落到主题默认色。要表达「透明」请用主题色配
> `scaled_alpha(0.0f)` 之外的明确手段，或直接接受主题默认。

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
| `animated` | `bool` | `false` | 显式要求「这个组件会动」（`spinner` `skeleton` 读；不写则只在鼠标悬停时动） |
| `press_scale` | `float` | `1.0f` | 按下时整体缩放，`1.0` = 不缩放 |

### 2.1 链式方法（全部 34 个）

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
| `scale_on_press(s)` | `press_scale = s` | 按压缩放 |
| `wrap_text(v = true)` | `wrap = v` | 自动折行 |
| `line_spacing(g)` | `line_gap = g` | 行距 |
| `animate(v = true)` | `animated = v` | 声明「无条件一直动」（不写则只在悬停时动） |

### 2.2 语义变体

```cpp
Style().primary()      // 0 Default     深色填充（浅色主题下是近黑）
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

## 3. `Theme` 全部 token

### 3.1 语义色（19 个）

| token | 浅色 | 深色 | 用在哪 |
|---|---|---|---|
| `background` | `#FFFFFF` | `#09090B` | 窗口底色 |
| `foreground` | `#09090B` | `#FAFAFA` | 主文字 |
| `card` | `#FFFFFF` | `#101012` | `panel` / `card` 底 |
| `card_foreground` | `#09090B` | `#FAFAFA` | 卡片内文字 |
| `popover` | `#FFFFFF` | `#131316` | 浮层底（menu / tooltip / toast） |
| `popover_foreground` | `#09090B` | `#FAFAFA` | 浮层文字 |
| `primary` | `#18181B` | `#FAFAFA` | 主色（按钮填充、进度条、滑杆已选段） |
| `primary_foreground` | `#FAFAFA` | `#18181B` | 主色上的文字 |
| `secondary` | `#F4F4F5` | `#27272A` | 次级填充（badge、次级按钮） |
| `secondary_foreground` | `#18181B` | `#FAFAFA` | 次级填充上的文字 |
| `muted` | `#F4F4F5` | `#27272A` | 低强调底（skeleton、code 块、slider 轨道） |
| `muted_foreground` | `#71717A` | `#A1A1AA` | 次要文字、占位符 |
| `accent` | `#F4F4F5` | `#27272A` | hover 底色、选中底色 |
| `accent_foreground` | `#18181B` | `#FAFAFA` | accent 上的文字 |
| `destructive` | `#EF4444` | `#DC2626` | 危险色 |
| `destructive_foreground` | `#FAFAFA` | `#FAFAFA` | 危险色上的文字 |
| `border` | `#E4E4E7` | `#27272A` | 描边、分隔线 |
| `input` | `#E4E4E7` | `#2E2E33` | 输入类控件的边框（深色下比 border 亮一点） |
| `ring` | `#18181B` | `#D4D4D8` | 焦点环（以 55% alpha 画 2px） |

### 3.2 度量（8 个）

| token | 默认 | 说明 |
|---|---|---|
| `radius` | `8.0f` | 基准圆角（button / skeleton / progress） |
| `radius_sm` | `4.0f` | 小圆角（code_text / nav_item / select / menu） |
| `radius_lg` | `10.0f` | 大圆角（panel / dialog） |
| `radius_full` | `9999.0f` | 胶囊（badge / progress 轨道） |
| `font_size` | `14.0f` | 基准字号 |
| `line_height` | `20.0f` | 基准行高 |
| `control_h` | `36.0f` | 标准控件高度（建议 `button`/`input`/`select` 用） |
| `control_h_sm` | `32.0f` | 小号控件高度 |

### 3.3 字体（2 个）

| token | 默认 | 说明 |
|---|---|---|
| `font_family` | `""` | 空 = **自动挑一个和系统一致的字体**，见下方 3.3.1 |
| `font_family_mono` | `""` | **保留字段，当前版本不生效**：它只被传给 `TextRenderer::init` 存下来，没有任何绘制路径读它。`code_text` 用的是普通无衬线，只是字号小一号 + 加了 `muted` 底衬 |

#### 3.3.1 `font_family` 留空时到底用了什么字体

不是写死的某一个名字，而是按顺序探测（Windows 后端）：

1. `Segoe UI Variable Text` —— Win11 的正文字体，资源管理器和设置界面用的就是它
2. `Segoe UI Variable` —— Win11 早期版本的命名
3. `Segoe UI` —— Win7 起一定有
4. 都没有就问系统 `SPI_GETNONCLIENTMETRICS` 的 `lfMessageFont`（简中机器上是 `Microsoft YaHei UI`）

**中文字形由 GDI 的字体链接提供**：注册表 `SystemLink` 里 Segoe UI 链到 `msyh.ttc`，
所以同一行里拉丁字母用 Segoe、汉字用微软雅黑，逐字替换、基线对齐 —— 这和
Windows 自己渲染中英混排的方式完全一致，不需要你手动指定两种字体。

| 你可能遇到的差别 | 原因 |
|---|---|
| 换了字体后中文宽度略宽约 5% | 经由字体链接取到的 CJK 字面，advance 比直接指定雅黑宽 1px/字 |
| 文字是灰度抗锯齿，不是 ClearType | 这是刻意的。试过 `CLEARTYPE_QUALITY`：画进 32bpp DIB 再 `BitBlt` 这条路径上，GDI 给出的次像素边纹过重（实测 21px 中文最大通道差 174、8.7% 的像素带明显橙蓝彩边），比系统的 ClearType 脏得多 |

想强制某个字体就显式写进主题（只选一个）：

```cpp
t.font_family = "Microsoft YaHei UI";   // 全用雅黑：拉丁也走它的西文字形（更宽更圆）
// t.font_family = "Consolas";          // 想要全等宽就换成这个
```

> 平台差异：Windows 会做上面这套探测；X11 / macOS 后端只是把 `font_family`
> 原样交给系统字体接口，留空时用各自的平台默认值。

> `code_text` **不是真正的等宽渲染**。若需要严格等宽（对齐数字、代码缩进），
> 当前版本做不到 —— 要么把 `font_family` 整体换成一个等宽字体，
> 要么等后续版本把 `font_family_mono` 接进绘制路径。

### 3.4 自定义主题

```cpp
aino::Theme t = aino::Theme::light();
t.primary   = aino::Color::hex(0x2563EB);   // 蓝色主色
t.radius    = 6.0f;                         // 全部圆角收紧
t.font_size = 13.0f;
t.font_family = "Microsoft YaHei UI";
win.set_theme(t);
```

`Theme::same_as()` 比较全部 19 色 + 8 度量 + 2 字体，
所以每帧调 `set_theme` 也安全（不会白白请求重绘）。

---

## 4. 属性生效规则

`Style` 有 30 个字段，但**不是每个组件都读全部**。规则分三类：

### 4.1 所有组件都读的

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

想知道某个组件的默认圆角/字号/内边距是多少，看该组件的专章
（`04`~`08` 每篇都有一张「默认值」表）。

---

## 5. 内置图标常量

`aino::icons` 提供一组不依赖字体的 Unicode 符号，可直接当 `icon_button` 的 `glyph`：

| 常量 | 字符 | 名称 |
|---|---|---|
| `icons::check` | ✓ | 对勾 |
| `icons::chevron_down` | ⌄ | 下箭头 |
| `icons::chevron_right` | › | 右箭头 |
| `icons::close` | ✕ | 关闭 |
| `icons::plus` | + | 加 |
| `icons::minus` | − | 减（U+2212 真减号，非连字符） |
| `icons::search` | ⌕ | 搜索 |
| `icons::settings` | ⚙ | 设置 |
| `icons::sun` | ☀ | 太阳 |
| `icons::moon` | ☾ | 月亮 |
| `icons::dot` | • | 圆点 |

```cpp
ui.icon_button(aino::icons::close, box, aino::Style().ghost());
```

这些是 UTF-8 字节串，字体缺字形时会显示成方框 —— 常见符号在
Segoe UI Symbol / 微软雅黑下都有。需要更多图标时嵌入自己的字体图标即可。
