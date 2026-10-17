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
> 需要对代码缩进就该换字体（见 [02](02-style-and-theme.md) 的说明）。

---

## 6. 排版相关的两个工具

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

### 6.1 大块文本：折行结果有缓存

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

## 7. 该用哪个

| 场景 | 选择 |
|---|---|
| 页面主标题 | `heading` |
| 区块标题 | `subheading` |
| 正文、说明 | `label` |
| 辅助说明、时间、单位 | `muted_text` |
| 数值、路径、版本号、行内代码 | `code_text` |

需要**底 + 框 + 内边距**的文本块（如提示条）→ 用 `alert`；
需要**可点击**的文本 → 用 `Style().ghost()` 或 `Style().link()` 的 `button`。
