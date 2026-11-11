# 07 · 展示类组件

展示组件，以及可交互的折叠标题、条目与分页控件。
未交互、未显式启用动画时保持静止；spinner 默认悬停才转，`.animate()` 可显式持续播放。

```cpp
void avatar    (const std::string& text, const Rect& box, const Style& st = Style());
void spinner   (const Rect& box, const Style& st = Style());
void alert     (const std::string& title, const std::string& description, const Rect& box, const Style& st = Style());
void breadcrumb(const std::vector<std::string>& items, const Rect& box, const Style& st = Style());
void kbd       (const std::string& text, const Rect& box, const Style& st = Style());
bool accordion (const std::string& title, bool* open, const Rect& box, const Style& st = Style());
void table     (const std::vector<std::string>& headers, const std::vector<std::vector<std::string>>& rows,
                const Rect& box, const Style& st = Style());
void sparkline (const std::vector<float>& values, const Rect& box, const Style& st = Style());
void bars      (const std::vector<float>& values, const Rect& box, const Style& st = Style());
```

---

## 1. `avatar` —— 圆形头像

| 项 | 默认值 |
|---|---|
| 返回 | 无 |
| 形状 | 圆形，直径 = `min(box.w, box.h)` |
| 底色 | `st.bg` → 否则 `theme.fill`（10% 叠加填充） |
| 字号 | `st.font_size` → 否则 `d * 0.42`（随直径缩放） |
| 字重 | 600（固定） |
| 前景 | `st.fg` → 否则按与窗口背景合成后的亮度选 `foreground` / 白色 |
| 描边 | **默认 1px `theme.border`**；`st.border_width` 可改粗细（写 0 关掉），`st.border_color` 改色 |

**生效的属性**：`bg` `fg` `border_color` `border_width` `font_size` `disabled`

取的是文本的**第一个码点**（不是第一个字节，所以中文不会被截半）。空串显示 `?`。

```cpp
ui.avatar("张伟", {24, 24, 48, 48});
ui.avatar("A",    {24, 24, 40, 40}, aino::Style().background(aino::Color::hex(0x2563EB)));
ui.avatar("李",   {24, 24, 48, 48}, aino::Style().stroke(2, aino::Color::hex(0xFFFFFF)));
```

`box` 不是正方形时按**最小边**取直径并居中，所以传 `{24,24,48,40}` 也不会变椭圆。

默认描边保证浅色容器中的轮廓可辨。使用 `Style().stroke(0, aino::Color::none())` 可关闭。

---

## 2. `spinner` —— 加载指示器

| 项 | 默认值 |
|---|---|
| 返回 | 无；**只在鼠标悬停（或 `animate()`）时请求重绘** |
| 底环色 | `theme.muted` |
| 旋转弧色 | `theme.primary` |
| 线宽 | `max(1.5, d * 0.12)` |
| 半径 | `d * 0.5 - 1.5` |
| 最小尺寸 | 6px（更小直接不画） |
| 角速度 | `4.6 rad/s`（约 0.73 圈/秒） |
| 拖尾 | 整圈连续 alpha，最暗处保留 14%，无断开的端点 |

**生效的属性**：`bg`（弧色）`border_color`（底环色）`disabled` `animated`

### 环形绘制

底环与周期性 alpha 拖尾直接按距离场扫描，每个像素只合成一次，避免短线段连接处叠色。
像素断言见 `tools/probe/arc_probe.cpp`。

### 什么时候转

默认**只在鼠标悬停时转**。静止页面不吃 CPU 是 aino 的头号承诺，而 spinner 常常
只是"这里在加载"的静态标识 —— 一个永远在转的圈会让整页从 0 帧变成 40fps。

```cpp
ui.spinner(box);                             // 悬停才转（默认）
ui.spinner(box, aino::Style().animate());    // 无论鼠标在哪都转
ui.spinner(box, aino::Style().background(aino::Color::hex(0x16A34A)));   // 绿色
```

历史测量（2026-10-17，460×300 窗口，单个 spinner；不代表当前版本耗时）：

| 条件 | 帧 | CPU |
|---|---|---|
| 默认，不悬停 | **0** | **0.00%** |
| 默认，悬停 | 约 41fps | 4.39% |
| `.animate()`，一直转 | 约 39fps | 0.52%~2.60%（随窗口可见性） |

> 建议尺寸 **16~28px**。太大时线宽会跟着变粗（`d * 0.12`），观感会笨重。

---

## 3. `alert` —— 提示条

| 项 | 默认值 |
|---|---|
| 返回 | 无 |
| 底色 | **不铺底**（见下）；`st.bg` 显式给了才画；`variant == Secondary` 时用 `theme.fill4` |
| 描边 | **无**（macOS 不用边框分层） |
| 圆角 | 10（只在铺底时可见） |
| `pad_x` | **12**（同时用于垂直内边距） |
| 字号 | `theme.font_size` |
| 符号 | 系统符号：`info.circle`（中性 50%）/ `exclamationmark.triangle`（系统红） |
| 标题 | 600 字重，色 `foreground`（**不因 Destructive 染红**） |
| 描述 | 400 字重，色 `muted_foreground`，**自动折行** |

**生效的属性**：`bg` `fg` `radius` `pad_x` `font_size`
`variant`（`Default` / `Secondary` / `Destructive`）`disabled`

默认只显示系统符号、标题和描述。显式指定背景或使用 Secondary 时绘制淡底；Destructive 改变符号颜色，标题保留标签色。

### 三个变体

```cpp
ui.alert("提示", "默认：只有符号和文字，没有任何面",          box);
ui.alert("提示", "次要：3% 淡底（少数需要分块的地方才用）",   box, aino::Style().secondary());
ui.alert("错误", "危险：符号转系统红，标题仍是 label 色",      box, aino::Style().destructive());
```

符号使用 `SystemIcon::Info` / `SystemIcon::Warning`，由平台系统图标通道绘制。

### 高度必须量

描述会折行，所以高度要用 `alert_height` 算：

```cpp
const char* t = "提示";
const char* d = "这是一条比较长的说明文字，用来验证正文在给定宽度下的折行与裁剪行为。";
const aino::Style st = aino::Style().font(13.0f);
ui.alert(t, d, col.next(ui.alert_height(t, d, 380.0f, st)), st);
```

`alert_height(title, description, width, style)` 用的是与绘制完全相同的折行代码，
所以给多少就显示多少，不会裁掉最后一行。

`description` 传空串时退化成「图标 + 单行标题」的紧凑样式，高度可以给 `box.h`。

---

## 4. `breadcrumb` —— 面包屑

| 项 | 默认值 |
|---|---|
| 返回 | 无 |
| 字号 | `theme.font_size` |
| 最后一段 | 500 字重 + `foreground` |
| 其它段 | 400 字重 + `muted_foreground` |
| 分隔符 | 系统 Next 符号，间距 **14px** |
| 裁剪 | 自动裁到 `box` |

**生效的属性**：`fg`（最后一段色）`font_size` `disabled`

```cpp
ui.breadcrumb({"首页", "组件", "展示补充"}, col.next(20.0f));
```

宽度按各段文字真实宽度累加，**不自动缩放**。段太多会超出 `box` 被裁掉。
需要「超出显示 …」的话自己截断 `items`。

---

## 5. `kbd` —— 键帽提示

| 项 | 默认值 |
|---|---|
| 返回 | 无 |
| 底色 | `theme.fill3` |
| 描边 | 默认无；显式 `st.border_color` 时绘制细边，无加厚下沿 |
| 圆角 | **4** |
| 字号 | `theme.font_size - 2`（默认 11） |
| 字重 | 500 |
| 前景 | `muted_foreground` |

**生效的属性**：`bg` `fg` `border_color` `radius` `font_size` `disabled`

```cpp
ui.kbd("Ctrl", {24, 24, 44, 26});
ui.kbd("Esc",  {74, 24, 36, 26});
```

> 组合键（`Ctrl+S`）不要写进一个 `kbd`，拆成两个键帽中间加 `+` 更像原生。
> 宽度不会自适应文字，建议 `text_width + 16`。

---

## 6. `accordion` —— 折叠面板

```cpp
bool accordion(const std::string& title, bool* open, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 本帧切换了展开状态 |
| 标题行高 | `min(box.h, max(fs + 8, 24))` |
| 标题左缩 | **22px**，箭头位于行首 |
| 箭头 | 系统 ChevronDown / Next，随展开状态变化 |
| hover 底色 | `theme.fill3` 随悬停淡入 |
| 圆角 | `theme.radius_sm`（4） |
| 焦点环 | `theme.ring` |

**生效的属性**：`fg` `font_size` `radius` `disabled`

### 只画标题行，内容由你画

这是立即模式的关键差异：`accordion` **不管理内容**，它只画标题行并翻转 `*open`。
内容要你自己在 `*open` 为真时紧接着画。

```cpp
bool changed = ui.accordion("高级设置", &s.advanced_open, col.next(44.0f));
if (s.advanced_open) {
    aino::Stack inner = ui.vbox(/* 在标题下方留出的区域 */, 10.0f, 12.0f);
    ui.input(&s.api_key, inner.next(24.0f), aino::Style().hint("API Key"));
    ui.checkbox(&s.dry_run, "试运行", inner.next(22.0f));
}
```

标题区域和内容区域分别分配高度；上例始终为标题分配 44，再在展开分支中为内容分配空间。
不要把内容高度也传给标题行。`Stack::next()` 不会自动扩张容器。

---

## 7. `table` —— 表格

```cpp
void table(const std::vector<std::string>& headers,
           const std::vector<std::vector<std::string>>& rows,
           const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | 无 |
| 表头高 | `max(fs + 10, 24)` |
| 行高 | `max(fs + 7, 20)` |
| 列宽 | **一律等分** `box.w / cols` |
| 表头底 | 默认不铺底；显式 `st.bg` 时铺底 |
| 表头字 | 字号 11、字重 **600**、色 `muted_foreground` |
| 列分隔线 | 无竖线 |
| 斑马纹 | 显式 `st.bg` 时，奇数行使用 `theme.row_alt` |
| 行分隔线 | 每行底部 1px `theme.border` |
| 单元格 `pad_x` | **12** |
| 外框 | 默认无外描边 |
| 裁剪 | 自动裁到 `box`，超出高度的行不画 |

**生效的属性**：`bg`（表头底、启用交替行）`fg`（正文色）`font_size` `pad_x` `radius` `disabled`
数字内容右对齐，表头按该列第一个非空单元格判断对齐方式。

```cpp
std::vector<std::string> heads = {"项目", "数值", "状态"};
std::vector<std::vector<std::string>> rows = {
    {"渲染帧",   "6.4 ms", "正常"},
    {"空闲 CPU", "0.00%",  "达标"},
};
ui.table(heads, rows, col.next(160.0f), aino::Style().font(12.0f));
```

### 已知限制

- 列宽固定等分，不能单独设置。需要自定义列宽时，用布局器、文本与图元组合绘制。
- `rows` 每行的列数可以与 `headers` 不同（少的部分不画，多出来的被忽略）。
- `headers` 为空时按 `rows` 里最长的行数推断列数（此时不画表头行）。
- **不可滚动**。行多了请套 `begin_scroll`，并把 `content_h` 传成
  `表头 + 行数 * 行高`。

---

## 8. `sparkline` —— 折线走势

```cpp
void sparkline(const std::vector<float>& values, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | 无 |
| 最少点数 | 2（少于 2 直接不画） |
| 纵向归一化 | 自动取 `min/max`，全部相等时人为撑开 ±1 |
| 内边距 | 四周 **2px** |
| 线色 | `st.bg` → 否则 `theme.primary` |
| 线宽 | `st.border_width > 0` 时用它，否则 **1.8** |
| 区域填充 | 线下方三角面，alpha **30**（`disabled` 时 14） |
| 只要裸线 | 传 `Style().ghost()`（判断条件是 `variant != Ghost`） |

**生效的属性**：`bg`（线色）`border_width`（线宽）`variant`（Ghost = 无填充）`disabled`

```cpp
ui.sparkline(trend, col.next(120.0f));
ui.sparkline(trend, col.next(120.0f), aino::Style().ghost().background(aino::Color::hex(0x2563EB)));
```

> 它**不画坐标轴、不画网格、不画点**。这是 sparkline 的定义（"词语级别的迷你图"）。
> 需要坐标轴就用 `draw_line` + `label` 自己搭。

---

## 9. `bars` —— 迷你柱状图

```cpp
void bars(const std::vector<float>& values, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | 无 |
| 归一化 | 按 `max(|v|)` 缩放，所以**支持负值** |
| 柱间距 | `max(2, box.w * 0.02)` |
| 柱宽 | 剩余宽度等分 |
| 圆角 | `st.radius` → 否则 `min(3, 柱宽/2)` |
| 正值色 | `st.bg` → 否则 `theme.primary` |
| 负值色 | `theme.muted`（一眼区分"掉下去的那几根"） |
| 最小高度 | 1px（值为 0 也画一条，避免"消失"） |

**生效的属性**：`bg`（正值色）`radius` `disabled`

```cpp
ui.bars({12, 30, 22, 48, -18, 36}, col.next(120.0f));
```

所有值都接近 0 时（`max(|v|) < 1e-6`）直接不画 —— 避免画出一堆 1px 线头。

---

## 10. 组合示例：指标卡

```cpp
void metricRow(aino::Ui& ui, const aino::Rect& area, const std::vector<float>& trend) {
    aino::Stack h = ui.hbox(area, 16.0f);
    const aino::Rect left  = h.next(240.0f);
    const aino::Rect mid   = h.next(200.0f);
    const aino::Rect right = h.fill();

    // 左：头像 + 标题 + 面包屑
    ui.avatar("张", {left.x, left.y, 40, 40});
    aino::Stack t = ui.vbox({left.x + 52.0f, left.y, 188.0f, 40.0f}, 4.0f);
    ui.label("周活跃用户", t.next(20.0f), aino::Style().left());
    ui.breadcrumb({"分析", "用户", "周活"}, t.next(18.0f));

    // 中：数值 + 迷你柱
    aino::Stack b = ui.vbox({mid.x, mid.y, 180.0f, 40.0f}, 2.0f);
    ui.heading("1,284", b.next(24.0f), aino::Style().font(20.0f).left());
    ui.bars(trend, b.fill());

    // 右：状态徽标
    ui.badge("+12.4%", {right.x, right.y + 8.0f, 70.0f, 24.0f});
}
```

`bars` 承担"趋势"、`heading` 承担"数值"、`badge` 承担"变化率"——
三种不同的信息密度用三种组件分开，比塞进一个表格可读得多。

---

## 11. 条目、空状态与分页

### 11.1 `item` —— 一行条目

```cpp
// 标题 + 描述 + 尾部文字（尾部可以留空）
if (ui.item("张伟", "zhangwei@example.com", "管理员", {x, y, w, 52}))
    open_detail();
```

| 项 | 行为 |
|---|---|
| 整行 | 可点（`interact` 覆盖整个 `box`），返回 true 表示本帧被点击 |
| 悬停 | `theme.fill3.scaled_alpha(t_h)` |
| 内边距 | `st.pad_x`，否则 10px |
| 一行 / 两行 | `description` 为空 → 单行（`fs` / 500）；否则标题 `fs` / 500 + 描述 11 / 400，两行整体在 `box` 里垂直居中，行间 2px |
| 尾部文字 | 右对齐、`fs-1`、`muted_foreground`，宽度按实测 + 12 |

它的高度完全由 `box.h` 决定 —— 两行模式需要约 `line_height(fs) + 2 + line_height(11)`，
给少了会挤。52px 可为两行内容保留较充足的间距。

### 11.2 `empty_state` —— 空状态

```cpp
ui.empty_state("还没有通知", "有事时会出现在这里", {x, y, w, 160});
```

标题与描述在给定区域中居中排列，不绘制边框或背景：

| 段 | 字号 / 字重 | 颜色 |
|---|---|---|
| 标题 | `fs + 1` / 500 | `st.fg` → 否则 `foreground` |
| 描述 | `fs - 1` / 400 | `muted_foreground` |

左右各留 16px 不画字。`description` 可以留空，那时只有标题。它**没有交互**，
所以不需要 `*` 参数，也不返回值。

### 11.3 `pagination` —— 页码器

```cpp
int page = 0;
if (ui.pagination(&page, 20, {x, y, 360, 36})) reload(page);
```

| 项 | 行为 |
|---|---|
| 槽位数 | **恒定 7 个**（`总页数 > 7` 时） |
| 布局 | 首 · … · 当前 · … · 末（靠近两端时自动改成「1 2 3 … 末」或「首 … 倒数三 二 末」） |
| 槽宽 | `min(box.h, box.w / 槽位数)`，整排水平居中 |
| 当前页 | 强调色 23% 胶囊底 + 主标签色 |
| 省略号 | **只画不 `interact`** —— 点了没意义，就不做成假控件 |
| 上下页箭头 | `SystemIcon::Prev` / `Next` |
| 返回 | 本帧是否翻页（`*page` 已写好） |

> 槽位数之所以恒定，是因为早期版本按"当前页附近三页"排：翻页时槽位数会从 8 变 6，
> 整排按钮跟着左右跳，用户点第二下就点偏。固定成 7 个之后，翻页只有内容变、位置不动。

## 12. `image_well` —— 图片占位

```cpp
ui.image_well({24, 24, 180, 100}, aino::Style().hint("选择图片"));
```

绘制 `fill3` 底、1px `separator` 细边、系统图片符号及居中说明，默认圆角 `radius_lg=10`，
说明字号 11。支持 `radius` 与 `placeholder`，默认说明为“拖入图片”。
它是只读展示，不处理点击、文件拖放或文件选择；这些行为由应用单独接入。
