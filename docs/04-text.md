# 04 · 文本类组件

五个纯展示组件，都不响应交互、不返回 bool、不占焦点。
它们的共同点是都走 `draw_text`，所以 `wrap` / `line_gap` / `align` / `valign` / `italic`
全部生效。

```cpp
void label      (const std::string& text, const Rect& box, const Style& st = Style());
void heading    (const std::string& text, const Rect& box, const Style& st = Style());
void subheading (const std::string& text, const Rect& box, const Style& st = Style());
void muted_text (const std::string& text, const Rect& box, const Style& st = Style());
void code_text  (const std::string& text, const Rect& box, const Style& st = Style());
```

五个组件的区别**只在默认字号/字重/前景色**（`code_text` 另外多一层底衬块），
其余行为完全一致。

---

## 1. `label` —— 正文

| 项 | 默认值 |
|---|---|
| 返回 | 无 |
| 字号 | `theme.font_size`（14） |
| 字重 | 400 |
| 前景 | `st.fg` → 否则 `muted ? muted_foreground : foreground` |
| 对齐 | `Style` 里的 `align`/`valign`（默认 `Left` / `Middle`） |

**生效的 Style 属性**：
`fg` `muted` `font_size` `font_weight` `italic` `align` `valign` `wrap` `line_gap`

**不读的属性**：`bg` `border_*` `radius` `pad_*` `placeholder` `variant` `disabled`
（正文文字没有底和框，这些字段对它没有意义）

```cpp
ui.label("正文内容", {24, 24, 320, 22}, aino::Style().left());
ui.label("次要说明", {24, 50, 320, 20}, aino::Style().muted_text());
ui.label(段文, box, aino::Style().wrap_text().line_spacing(3.0f));
```

> `label` 的 `align` 默认是 `Left`，且**不置 `align_set`**。
> 这与 `button` 不同 —— `button` 会在 `align_set == false` 时强制居中，
> `label` 不会。所以想居中就显式 `.center()`。

---

## 2. `heading` —— 标题

| 项 | 默认值 |
|---|---|
| 字号 | **24** |
| 字重 | **700** |
| 前景 | 同 `label` |

用于页面主标题。字号是硬编码的 24，不跟 `theme.font_size` 缩放
（想跟随主题就显式 `.font(theme.font_size * 1.7f)`）。

```cpp
ui.heading("概览", col.next(34.0f), aino::Style().left());
```

> 24px / 700 的字形需要字体里有粗体字面。系统字体引擎会做合成加粗兜底，
> 但合成粗体的笔画比真粗体脏一点。在意的话把字号降一档或换 `semi_bold()`。

---

## 3. `subheading` —— 副标题

| 项 | 默认值 |
|---|---|
| 字号 | **18** |
| 字重 | **600** |
| 前景 | 同 `label` |

小节标题。常配 `label` 做「分组标题 + 组内条目」的结构。

```cpp
ui.subheading("文本", col.next(26.0f), aino::Style().left());
ui.label("正文……", col.next(22.0f), aino::Style().left());
```

---

## 4. `muted_text` —— 次要说明

| 项 | 默认值 |
|---|---|
| 字号 | `theme.font_size`（14） |
| 字重 | 400 |
| 前景 | `st.fg` → 否则**恒定** `muted_foreground` |

和 `label` 的唯一区别是前景色固定取 `muted_foreground`。
**它不吃 `muted` 标志**（本来就是次要色，再吃一次没有意义）。

```cpp
ui.muted_text("单头文件 · 零第三方依赖", col.next(18.0f), aino::Style().font(11.0f).left());
```

常用于：辅助说明、时间戳、单位、空状态提示、表单字段的补充解释。

---

## 5. `code_text` —— 行内代码 / 数值展示

唯一带底衬的文本组件。

| 项 | 默认值 |
|---|---|
| 字号 | `theme.font_size - 1`（13） |
| 字重 | 400（固定，不吃 `font_weight`） |
| 底衬 | `st.bg` → 否则 `theme.muted` |
| 圆角 | `st.radius` → 否则 `theme.radius_sm`（4） |
| 内边距 | 水平 **6px**（硬编码），垂直 0 |
| 前景 | `st.fg` → 否则 `theme.foreground` |
| 垂直对齐 | 固定 `Middle` |
| 描边 | 只有 `border_width > 0` **且** `border_color` 非 none 时才画 |

**生效的 Style 属性**：
`bg` `fg` `border_color` `border_width` `radius` `font_size` `align`
（`align` 仅在显式设置时生效，默认 `Left`）

```cpp
ui.code_text("aino.hpp · 0.1.0", col.next(30.0f), aino::Style().font(13.0f));
ui.code_text(std::to_string(count), col.next(30.0f));   // 数字用底衬块更醒目
```

> ⚠️ **它不做等宽渲染**。实现里没有切换到 `theme.font_family_mono`，
> 用的就是普通无衬线字体。所以它不是真正的「代码块」，而是「带灰底的小号文本」。
> 真要展示多行代码，用下一节的 `code_block`。

---

## 6. `code_block` —— 带行号与着色的只读代码块

`code_text` 是一行小标签，`code_block` 才是正经代码块：**等宽字体 + 行号列 + C++ 词法着色**。

```cpp
// 高度先算后排 —— 它不会自己长高
const float h = ui.code_block_height(src, aino::Style().font(13.0f));
ui.code_block(src, {x, y, 620.0f, h}, aino::Style().font(13.0f));
```

| 项 | 默认值 |
|---|---|
| 字号 | `st.font_size`，否则 **12.5** |
| 行高 | `max(字号 × 1.62, 15)` |
| 整体高度 | `10 + 行高 × 行数 + 10`（`code_block_height()` 算的就是这个） |
| 底色 | `st.bg`，否则 `theme.muted.mix(theme.background, 0.3f)` |
| 描边 | `theme.border`，1px |
| 圆角 | `st.radius`，否则 `theme.radius` |
| 行号列宽 | **最大行号实测宽 + 10**（不是固定几像素，行数上万也不会撑破） |
| 左内边距 | 12px（在行号列之前） |
| 行号字号 | `字号 - 1`，色 `gutter` |
| 行号与代码之间 | 一条 1px 竖线，位置取整后 `+0.5`，避免压在两列像素中间发虚 |

它走的是**等宽通道**（`TextRenderer::measure_mono` / `draw_mono`），不经过 `draw_text`。
三个后端都实现了它：Windows 用 Consolas（`CreateFontW` 的 `FIXED_PITCH|FF_MODERN`），
macOS 用 `Menlo`，Linux 用 fontconfig 的 `monospace`。后端不实现也编得过 —— 基类会
退化成比例字体、列对不齐，总比整块文字消失好。

### 6.1 它不滚动，也不折行

这两条是唯一必须记住的：

- **不滚动**：`box.h` 装得下几行就画几行，多出来的直接裁掉（`cap = (box.h - 12) / 行高`）。
- **不折行**：一行画到 `box.right() - 4` 就断，不会接第二行。长行要先自己切，或者给足宽度。

想看整段就外面套一层 `scroll()`。也正因为这两条，**高度一律用 `code_block_height()` 算** ——
它和 `code_block()` 共用同一个行高公式，手填数字迟早对不上。

### 6.2 着色只做到「分块看得清」

`detail::code_lex` 做的是：拆行 → 每行切段 → 关键词 / 类型 / 字符串 / 注释 / 数字五类。
**不做完整语法解析** —— 代码块是给人看的，认错一个标识符的类别没有后果；但字符串、注释、
数字这三类必须正确。

切段不只为了颜色：**同色相邻字符会合并**，一行几十次 GDI 调用能压到三五次。色板按背景亮度
自动选两套，跟着 `theme` 走，不需要每次自己配七个颜色。

### 6.3 测量模式下它不会真画

`code_block` 自己调 `TextRenderer` 画字、绕过了 `draw_text`，所以它在入口单独拦了一道：

```cpp
if (impl->measure_on) { impl->measure_add(box); return; }
```

少这一道，`begin_measure()` 期间整段代码会真画到屏幕上 —— 就是 0.3.4 修过的那类
「测量留残影」。自己写的组件如果用 `draw_text` 之外的路径落像素，也要补同样的闸。

---

## 7. 排版相关的两个工具

文本高度**不是自动的**，必须量：

```cpp
// 这段文字在 380 宽下需要多高（与 draw_text 用的是同一套折行代码）
float h = ui.text_height(long_text, 380.0f, aino::Style().wrap_text());

// 单词宽度（用于 next_text 或自己算对齐）
float w = ui.text_width("确定", 14.0f, 500);   // 字号 + 字重
```

折行规则：

| 场景 | 行为 |
|---|---|
| `\n` | 永远生效（不受 `wrap` 影响） |
| CJK 字符 | 逐字断行（可以在任意字之间断） |
| 西文单词 | 优先退到最近的空格；**没有空格才硬断** |
| 溢出 | 超出 `box` 的部分被裁掉（不会画出去） |

`draw_text` 会把文字裁到 `clip ∩ box`，所以长英文串不会横穿整个窗口 ——
这是踩过的坑（早期只裁几何，对话框正文会爬出面板）。

---

### 7.1 大块文本：折行结果有缓存

折行是 O(字符数) 的逐字量宽。一段 20 万字的中文，**每帧重排一次就是 1.7ms**，
而屏幕上其实只有几十行可见。所以 aino 在两层做了缓存：

| 层 | 键 | 命中成本 | 作用 |
|---|---|---|---|
| 单字宽度 | (字号, 字重, 码点) | 一次哈希 | 首次折行时每个字只问 GDI 一次 |
| 整段折行 | (文本地址, 长度, 宽度, 字号, 字重, wrap) | 一次哈希 + 一次 `memcmp` | 静态文本每帧只查一次 |

实测 20 万字（700px 宽）：**不缓存 1.66 ms → 命中 6.5 µs（约 250 倍）**。

几个必须知道的细节：

- **键里放的是文本地址，不是内容哈希**。内容哈希要遍历整串（20 万字约 200µs/帧），
  每帧都付不起。地址在调用方的 `std::string` 不扩容时是稳定的；命中后用
  `memcmp` 兜底校验，所以地址被复用（临时字符串落在同一个栈地址）也不会返回错的行。
- **缓存有上限**，默认 6 MB，超了按「最久未用」淘汰。环境变量
  `AINO_WRAP_BUDGET=<KiB>` 可调。20 万字的中文大约 1.6 MB 行文本，够放 3~4 段。
- **只画可见的行**。`draw_text` 会按裁剪区把行区间算出来，跳过整行都在裁剪区外的
  行——滚动区的长文档不会每帧把上万行都交给 GDI。
- 改字号 / 改可用宽度 / 改 `wrap` 都会命中不同条目，不会串。**改字体家族**不会
  自动清缓存（主题色变化是安全的，因为键里含字号）。

```cpp
// 想量一段长文的高度，直接用同一个缓存：
float h = ui.text_height(long_doc, 700.0f, aino::Style().wrap_text());
```

---

## 8. 该用哪个

| 场景 | 选择 |
|---|---|
| 页面主标题 | `heading` |
| 区块标题 | `subheading` |
| 正文、说明 | `label` |
| 辅助说明、时间、单位 | `muted_text` |
| 数值、路径、版本号、行内代码 | `code_text` |
| 多行代码、源码展示 | `code_block`（行号 + 着色，高度用 `code_block_height`） |

需要**底 + 框 + 内边距**的文本块（如提示条）→ 用 `alert`；
需要**可点击**的文本 → 用 `Style().ghost()` 或 `Style().link()` 的 `button`。
---

## 9. 三个平台的字体从哪来

字号（`Style::font_size`，默认 `theme.font_size`）在三个后端都当**像素尺寸**用，
但取字形的路完全不同：

| 平台 | 后端 | 字形来源 | 抗锯齿 | 粗体 |
|---|---|---|---|---|
| Windows | GDI | Segoe UI（中文经字体链接落到微软雅黑） | 灰度 AA | 真字面；缺字面时 GDI 合成加粗 |
| macOS | CoreText | 系统 UI 字体（SF） | 有 | 真字面 |
| Linux | FreeType + fontconfig（都是运行时 `dlopen`） | 主字体 `sans` → DejaVu Sans；缺字时沿 `FcFontSort` 的兜底链找到 Noto Sans CJK | 灰度 AA | fontconfig 按 `weight` 匹配字面 |

### 字面量编码：一个「·」就能让整片中文变成乱码

三个平台内部都按 UTF-8 处理字符串（`std::string` 里存的就是 UTF-8 字节）。风险在
**C++ 窄字面量**：MSVC 不加 `/utf-8` 时按系统 ANSI 代码页编码。

单纯一个系统 ANSI 串（用户没加 `/utf-8`、串里全是中文）靠 `to_wide()` 的逐段解码
救得回来，但**混合编码**要麻烦得多：串里只要有一个「ANSI 里也有单字节表示」的字符，
它被编成单字节、中文留着 UTF-8 字节 —— 整串既不合法 UTF-8，也不是纯 ANSI。

`"·"`（U+00B7）就是最典型的触发器，它在 1252 和 936 里都是单字节：

| 字面量 | 编译后 | 旧版（整串回退 ACP） | 现在 |
|---|---|---|---|
| `"中文示例"` | 纯 UTF-8 | 严格解码通过 ✓ | 同 ✓ |
| `"最小示例 · 0.4.7"` | 中文留 UTF-8，「·」变 `0xB7` | 整串按 ACP 重解 → `æœ€å°ç¤ºä¾‹` ✗ | 中文走 UTF-8、`0xB7` 走 ACP 单字节 → 正确 ✓ |
| 源码被当成 Latin-1 读 | 每个字节变成一个字符（仍是合法 UTF-8） | `æœ€å°ç¤ºä¾‹`（C1 方框）✗ | 逐字节还原 → 正确 ✓ |

症状很好认：**窗口标题乱码、界面里的中文却是好的** —— 界面那些串里没有 ACP 能
表示的字符，整串仍是合法 UTF-8，严格解码能过。

三道防线：

1. 本头文件开头有 `#pragma execution_character_set("utf-8")`（MSVC），等价于
   `/utf-8` 的执行字符集那一半。pragma 对**本编译单元余下部分**生效，所以先 include
   本头文件、后面再写的字面量也被覆盖；单独一个只定义常量的编译单元仍需 `/utf-8`。
2. `to_wide()` 改成**逐段混合解码**：按 UTF-8 规则逐个码位走，走得通就按 UTF-8 出；
   走不通的那一（或两）个字节才交给 `CP_ACP`（先试单字节，再试双字节，都不行落
   U+FFFD）。关键是它**绝不整串回退** —— 合法的中文永远是合法的中文，任何输入都
   不可能退化成「每个字节一个西欧字母」。
3. `to_wide()` 最后再兜一道 `fix_latin1_mojibake()`：如果解出来的串**所有字符都
   ≤ U+00FF**、且夹着 **U+0080..U+009F 的 C1 控制字符**，就把它当成"UTF-8 字节被
   逐个当作字符读进去"的结果，逐字节还原后按 UTF-8 重解。这种坏法最阴 —— 它**仍然
   是合法 UTF-8**，严格解码照样通过，只是内容已经全是西欧字母（"最小示例"变成
   `æœ€å°ç¤ºä¾‹`），标题栏里那些 C1 字符还会被画成带十六进制的方框。它通常来自
   **源码 BOM 被剥掉**：MSVC 于是按系统 ANSI 去读 UTF-8 源码，注释成了乱码（还能
   编过），字面量也成了乱码。
   判据收得很紧（三条同时满足才动手），所以 `café` 这种真的 Latin-1 文案不会被误伤。

`ci_pixels` 的 S5 场景钉住六条：人工构造的混合串（中文要留住、总长度要接近原义而
不是每个字节一个字符、那个孤立字节要解成 `·` 或替换符）、编译期字面量
`"最小 · 示例"`、整串系统 ANSI 仍按 `CP_ACP` 解、4 字节 UTF-8 要出代理对、
被当成 Latin-1 读过的串能还原回中文、正常 Latin-1 文案不被误判。
除了正常的 `/utf-8` 编译，还会**不带 `/utf-8` 再编一遍**跑同一套断言 —— 那正是
用户手编时的状态。

Linux 上有四个坑，都写在代码注释里：
1. **字形在客户端光栅化，不走 X server**。`XftDrawString` / `Xutf8DrawString` 是把字画到
   **服务端 drawable** 上的，而 aino 每一帧都是先在客户端缓冲里画完、帧末 `XPutImage`
   整块覆盖 —— 直接画会被盖掉；改成画进 Pixmap 再 `XGetImage` 取回又要每次绘制一次
   同步往返，一帧几百段文字就是几百次往返，交互直接卡死。所以走 FreeType：字形在进程内
   光栅化成灰度位图，`blend` 进缓冲，X 一次都不碰。
2. **中文靠 fontconfig 的逐字符兜底**。默认 `sans` 是 DejaVu Sans，它**没有汉字**；
   Xft 只认单个字体、不做 fallback，用它画中文就是满屏 `.notdef` 豆腐方块。
   这里用 `FcFontSort` 拿到排好序的整张字体表，逐字符找第一个覆盖该码位的字体
   （西文几乎都命中主字体，汉字落到 Noto Sans CJK）。`ci_pixels` 里那条
   「有墨的行里有没有一行在中间三分之一列有墨」就是钉这件事的 —— 豆腐方块是空心矩形，
   中间列除了上下两条边什么都没有。
3. **兜底链不能"取前 N 条"**。`FcFontSort` 的返回表里**同一个家族的每个 style 各占
   一条**：DejaVu Sans 一家就能吃掉 Book / Bold / Oblique / Condensed 七八条，Noto Sans
   再占一批。只取前 16 条时，用户机器上扫完 16 条还没有一个覆盖汉字 —— 于是整片中文
   变成空白（不是豆腐块，是彻底不落墨）。等宽通道字体种类少、CJK 挤进了前 16，所以
   表现成「代码块里的中文还在、别处全没了」这个奇怪的不对称。
   现在的做法是**按 charset 探测**（`FcPatternGetCharSet` + `FcCharSetHasChar`）：
   整张表扫一遍，把第一个覆盖 U+4E2D 的字体插到主字体之后，名次怎么排都不影响。
   `ci_pixels` 的 S4 场景用 `AINO_X11_CHAIN_CAP=4` 把链砍短，精确复现这个状态。
4. **基线只按主字体的 metrics**。汉字 ink 本来就在 em 框内（上缘约 0.88em < DejaVu 的
   0.93em），拿「国」的 ink 去撑 asc 反而会把 19px 的字算成 24px 的 asc，行高 30 > 26px
   的盒子，`VAlign::Middle` 居中后整行上移 2px、顶部被裁。踩过这个坑：
   侧边栏那个 `aino` 词标看起来像被切了一刀。
