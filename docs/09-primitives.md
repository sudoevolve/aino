# 09 · 图元与度量

组件是「语义」，图元是「笔画」。所有几何都由本库 CPU 光栅化（带覆盖率抗锯齿），
不走 GDI 的 `RoundRect` / `AlphaBlend` / `GradientFill` —— 这是 Windows 上
只需要 `user32 + gdi32` 的原因。

**所有坐标都是逻辑单位**（与 `box` 一致），内部自动乘 DPI 缩放。

---

## 1. 填充与描边

```cpp
void clear      (const Color& c);
void fill_rect  (const Rect& r, const Color& c, float radius = 0.0f);
void stroke_rect(const Rect& r, const Color& c, float radius = 0.0f, float width = 1.0f);
void fill_circle(float cx, float cy, float radius, const Color& c);
void fill_crescent(float cx, float cy, float radius,
                   float cut_cx, float cut_cy, float cut_radius, const Color& c);
void draw_line  (float x0, float y0, float x1, float y1, const Color& c, float width = 1.0f);
```

| 函数 | 说明 |
|---|---|
| `clear(c)` | 整块后备缓冲填成 `c`（每帧开头用，通常是 `theme.background`） |
| `fill_rect(r, c, radius)` | 圆角矩形填充。`radius = 0` 是矩形；半径会被 clamp 到 `min(w,h)/2`，所以给 999 就得到胶囊或圆 |
| `stroke_rect(r, c, radius, width)` | 圆角矩形描边。**描边画在矩形边界内侧**（不占额外空间） |
| `fill_circle(cx, cy, r, c)` | 圆。圆角矩形给足半径也能画圆，但这个更省 |
| `fill_crescent(cx, cy, r, cut_cx, cut_cy, cut_r, c)` | 月牙 = 圆减圆（几何差集），**不需要背景色**，见下 |
| `draw_line(x0, y0, x1, y1, c, width)` | 任意方向线段，端点圆头 |

### 月牙为什么是独立图元

图标里的月牙，常见画法是「先画实心圆，再用**背景色**画一个圆挖掉」。这个挖掉的圆
是不透明的底色，所以只要背景一换（悬停高亮、换主题、图片底、渐变底）就会露出一块
颜色不对的斑 —— 也就是"穿帮"。

`fill_crescent` 走的是距离场 CSG 差集：

```
dA = |p - A| - rA          A 内部为负
dB = |p - B| - rB          B 内部为负
d  = max(dA, -dB)          A 且 非 B
coverage = clamp(0.5 - d, 0, 1)     // 1px 过渡带
```

每个像素只 `blend` 一次，**完全不需要知道底下是什么颜色**，边缘仍是灰度 AA。
代价是一次包围盒扫描（只扫 A 的框），和画一个实心圆同一量级。

```cpp
// 16px 图标里的月亮：外圆 r=8，挖掉一个偏右上、比外圆略大的圆
ui.fill_crescent(bx, by, 8.0f, bx + 3.6f, by - 2.8f, 8.6f, ui.theme().foreground);
```

`cut_r` 比 `radius` 大是正常的（挖的圆更大、位置偏一点，留下的月牙更薄更尖）；
两者差不多大时月牙最厚。回归见 `tools/probe/resize_probe`、`tools/probe/arc_probe`
同一类像素断言口径。

### 早期返回（不会画任何东西）

| 条件 | 说明 |
|---|---|
| `c.a == 0` | 全透明直接跳过（`Color::none()` 就是这种情况） |
| `r.empty()` | `w` 或 `h` ≤ 0 |
| 转换到物理像素后为空 | 小于一个像素的矩形会被丢掉 |

所以「用 `Color::none()` 表示不画」是安全的，不需要自己加 if。

### 抗锯齿与半透明

- 边缘走**覆盖率 AA**：每个像素按几何与像素方格的交集比例混合
- `c.a < 255` 时逐像素 `blend`，**不走整像素快填**
  （覆盖率 1 ≠ 可以写实色，这是踩过的坑）
- 圆角、圆、线段共享同一套光栅化核心，所以它们的边缘观感一致

### 示例

```cpp
ui.clear(ui.theme().background);

ui.fill_rect({24, 24, 200, 60}, aino::Color::hex(0x18181B), 12.0f);          // 圆角块
ui.stroke_rect({24, 100, 200, 60}, aino::Color::hex(0xE4E4E7), 8.0f, 1.0f);  // 描边
ui.fill_circle(300, 60, 20, aino::Color::hex(0x2563EB));                     // 圆
ui.fill_crescent(400, 60, 18.0f, 410.0f, 52.0f, 19.0f, aino::Color::hex(0x18181B));  // 月牙
ui.draw_line(24, 180, 400, 180, aino::Color::hex(0xE4E4E7), 1.0f);           // 细线
```

---

## 2. 文本

```cpp
void draw_text(const std::string& text, const Rect& box, const Color& c,
               const Style& st = Style());
```

`Style` 在这里只读这几个字段：`align` `valign` `font_size` `font_weight` `wrap` `line_gap`。

| 项 | 说明 |
|---|---|
| 默认字号 | `theme.font_size`（14） |
| 默认字重 | 400 |
| 折行 | `\n` 永远生效；`st.wrap` 为真时再按 `box.w` 自动折行 |
| 裁剪 | **总是**裁到 `effective_clip ∩ box` |
| 对齐 | `align` 决定水平，`valign` 决定垂直 |

裁剪是强制的：文字永远不会画到 `box` 外面。这是为了保证「对话框正文
横着爬出面板」这类问题不可能发生（早期版本只裁几何，长英文串会横穿整窗）。

```cpp
ui.draw_text("居中文字", {24, 24, 200, 40}, aino::Color::hex(0x09090B),
             aino::Style().center().middle());
```

> 想要一层底就用 `label` / `code_text`——它们内部是 `draw_text` 加一层装饰。
> `alert` 是例外：macOS 的 inline 提示**不铺底**，只有一枚符号 + 两行文字。

---

## 3. 阴影

```cpp
void drop_shadow(const Rect& r, float radius, float blur, const Color& c, float dy = 1.0f);
```

| 参数 | 说明 |
|---|---|
| `r` | 阴影的**投影轮廓**（通常就是被投影的组件框） |
| `radius` | 轮廓圆角，与被投影组件的圆角保持一致 |
| `blur` | 模糊半径，越大越散 |
| `c` | 阴影色，通常 `Color(0, 0, 0, 30~50)` |
| `dy` | 垂直偏移（正值向下） |

### 走距离场，不是叠层

实现是一次成形的 SDF：

```
alpha = c.a * clamp(1 - d/blur, 0, 1)²      // d = 到轮廓的距离
```

平方衰减没有硬边。**峰值 alpha 严格不超过传入的 `c.a`** ——
早期版本是叠 N 层半透明圆角矩形，叠加处会累积（传 46 渲出约 148，糊成一坨黑）。

| 浮层 | 建议参数 |
|---|---|
| `tooltip` / `menu` | `blur 10, dy 3, alpha 34` |
| `toast` | `blur 14, dy 5, alpha 38` |
| `dialog` | `blur 22, dy 8, alpha 46` |

```cpp
ui.drop_shadow({100, 100, 260, 80}, 12.0f, 22.0f, aino::Color(0, 0, 0, 46), 8.0f);
ui.fill_rect({100, 100, 260, 80}, aino::Color::white(), 12.0f);
```

> 阴影是**有成本的**，而且成本按「**外扩 blur 后的包围盒**」算，不是你传进去的矩形：
> 260×80 配 `blur 22`，在 DPI 1.5 下实际扫的是 `(390+66) × (120+66) = 456×186 ≈ 8.5 万`像素
> （≈ 635µs）；`dialog` 的 340×170 配 `blur 22` 扫 `576×321 ≈ 18.5 万`像素（≈ 1253µs）。
> 单价约 **6.5 ns/像素**，是普通填充的 4~10 倍 —— 因为每像素要算一次 `rrect_sdf`。
> 大面板 + 大 blur + 每帧重绘 = 明显的帧时间。静置时没影响（不渲染）。

---

## 4. 度量

```cpp
float text_width  (const std::string& text, float font_size, int weight = -1) const;
float line_height (float font_size) const;
float text_height (const std::string& text, float width, const Style& st = Style()) const;
float alert_height(const std::string& title, const std::string& description,
                   float width, const Style& st = Style()) const;
```

| 函数 | 返回 | 用途 |
|---|---|---|
| `text_width(s, fs, w)` | 单行文本的逻辑宽度 | 按钮/徽标定宽、`next_text` |
| `line_height(fs)` | 一行的高度（含行距） | 算多行文本总高 |
| `text_height(s, w, st)` | 给定宽度下**折行后**的总高 | 正文块留高度 |
| `alert_height(t, d, w, st)` | `alert` 在给定宽度下需要的高度 | 给 `alert` 留高度 |

### 为什么高度要"量"而不是"自适应"

宽度可以立刻量出来（系统字体引擎直接给），但**高度依赖折行**，
折行又依赖宽度 —— 在「边画边切」的立即模式里，这需要两趟遍历。

所以 aino 的立场是：**不做高度自适应，但高度必须可量**。
而且量出来的值和实际绘制用的是**同一套折行代码**（`layout_wrapped_lines`），
不是估算 —— 给多少就画多少，不会裁掉最后一行。

```cpp
// 正文块：先量后给
float h = ui.text_height(paragraph, 380.0f, aino::Style().wrap_text());
ui.label(paragraph, col.next(h), aino::Style().wrap_text());

// 提示块：用专用度量
float ah = ui.alert_height(title, desc, 380.0f, aino::Style().font(13.0f));
ui.alert(title, desc, col.next(ah), aino::Style().font(13.0f));
```

> `text_height` 用 `st.wrap` 决定要不要折行。**如果绘制时用了 `wrap_text()`
> 而量高度时忘了，量出来的是一行的高度**，正文会被裁成一行 —— 这是最常见的坑。

### 手动排版时用 `next_text`

```cpp
aino::Stack row = ui.hbox({24, 24, 400, 36}, 8.0f);
ui.label("确定", row.next(ui.text_width("确定", 14.0f) + 16.0f));
```

横向布局里做「刚好包住文字」的格子，`Stack::next_text` 更省事：

```cpp
ui.label("确定", row.next_text("确定", 16.0f));   // 文字宽 + 左右各 8
```

---

## 5. 裁剪与坐标

```cpp
void push_clip(const Rect& r);   // 压入一个裁剪区（与已有裁剪取交集）
void pop_clip();
```

裁剪同时作用于**几何与文字**。`begin_scroll` 内部会自动处理，不需要额外包。

坐标相关：

| 函数 | 说明 |
|---|---|
| `ui.width()` / `ui.height()` | 客户区逻辑尺寸 |
| `ui.dpi_scale()` | 逻辑→物理倍率（如 1.5） |
| `ui.input().mouse` | 鼠标**逻辑坐标**（已除 DPI） |

图元 API 全部吃逻辑坐标，所以你的代码**不需要关心 DPI** ——
这是刻意的：一处换算，处处一致。

---

## 6. 一帧的完整顺序

自己驱动渲染时（`poll` + `render` 之外的场景），顺序是这样的：

```cpp
win.run([&](aino::Ui& ui) {
    const aino::Theme& t = ui.theme();
    ui.clear(t.background);              // 1. 铺底

    // 2. 背景层（panel / 分隔线 / 图片位）
    ui.panel(page);
    // 3. 内容层（文字、控件）
    ui.heading("标题", ...);
    // 4. 浮层（帧末统一绘制，调用位置不影响层级）
    ui.tooltip(...);
});
```

`begin_frame` / `end_frame` 由 `run()` 负责，不需要手动调。
需要手动控制时用 `win.poll()` + `win.render()`，`render()` 内部会走完整的一帧。
