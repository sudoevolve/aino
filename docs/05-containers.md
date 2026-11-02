# 05 · 容器类组件

六个负责「一块面」的组件：底、线、胶囊、进度、占位。
除 `skeleton` 的可选动画外，它们全部**不响应交互、不返回 bool**。

```cpp
void panel    (const Rect& box, const Style& st = Style());
void card     (const Rect& box, const Style& st = Style());
void separator(const Rect& box, const Style& st = Style());
void badge    (const std::string& text, const Rect& box, const Style& st = Style());
void progress (float value, const Rect& box, const Style& st = Style());
void skeleton (const Rect& box, const Style& st = Style());
```

---

## 1. `panel` —— 卡片 / 面板

| 项 | 默认值 |
|---|---|
| 底 | `st.bg` → 否则 `theme.card`；都没给就是 `theme.fill3`（5% 叠加） |
| 描边 | `st.border_color` → **默认没有**（`Color::none()`，不画） |
| 描边宽 | `st.border_width` → 否则 **1.0**（只有给了颜色才用得上） |
| 圆角 | `st.radius` → 否则 `theme.radius_xl`（12） |
| `muted` | `= true` 时底改用 `theme.fill4`（3%，忽略 `bg`） |

> **默认不描边**。Figma 的 `Group Box` 符号只有一层填充（3% 再乘 50% 不透明度），
> 图层里没有任何 stroke —— macOS 的分组靠"面比底深一档"分层，画一圈 1px 线是
> Web 卡片的语法。默认填充取 `Fills - Opaque/Tertiary`（5%），也就是系统
> "grouped background" 那一档；kit 裸值 1.5% 在白底上等于看不见。
>
> 要框就显式给：`ui.panel(box, aino::Style().stroke(1.0f, ui.theme().separator))`。

**生效的 Style 属性**：`bg` `border_color` `border_width` `radius` `muted`

> ⚠️ `muted` 是 `Style` 的**字段**，但**没有** `muted()` 这个链式方法 ——
> 链式方法叫 `muted_text()`（历史命名，本是给文本用的）。它能设上 `muted = true`，
> 所以对 `panel` 也有效，只是读起来别扭。更直白的写法是直接给颜色：
>
> ```cpp
> ui.panel(box, aino::Style().background(ui.theme().muted));
> ```

```cpp
ui.panel({24, 24, 400, 240});                                  // 标准分组卡（5% 底、r12、无边框）
ui.panel({24, 24, 400, 60}, aino::Style().muted_text());       // 更淡的一档（3%，忽略 bg）
ui.panel(box, aino::Style().stroke(1.0f, aino::Color(0, 0, 0, 26)));   // 显式要一圈线
```

> 判断条件是 `bw > 0 && bd.a > 0`：颜色是 `Color::none()`（a=0）就直接不画，
> 宽度给什么都不影响。所以"无边框"是默认态，不需要再写 `stroke(0, ...)` 去关它。

`panel` 是**底层**：它只画底和框，不画内容。内容要紧接着用 `Stack` 布局在上面画。

```cpp
const aino::Rect card = {24, 24, 400, 240};
ui.panel(card);
aino::Stack c = ui.vbox(card, 10.0f, 18.0f);   // 内边距 18 由 Stack 的 pad 提供
ui.heading("标题", c.next(32.0f), aino::Style().left());
```

---

## 2. `card` —— `panel` 的别名

```cpp
inline void Ui::card(const Rect& box, const Style& st) { panel(box, st); }
```

完全等价。语义上 `card` 强调「一张卡」，`panel` 强调「一个面板」，
实现是同一个函数，参数与默认值完全相同。

---

## 3. `separator` —— 分隔线

| 项 | 默认值 |
|---|---|
| 颜色 | `st.bg` → 否则 `theme.border` |
| 粗细 | 恒定 **1px** |
| 方向 | `box.w >= box.h` → 横线；否则竖线 |

**生效的 Style 属性**：`bg`

方向是**自动判断**的，不需要指定：

```cpp
ui.separator(col.next(1.0f));            // 宽 320、高 1 → 横线
ui.separator({300, 24, 1, 200});         // 宽 1、高 200 → 竖线
```

横线画在 `box` 的垂直中心（`cy - 0.5`），竖线画在水平中心（`cx - 0.5`）。
所以给 `next(1.0f)` 得到的就是紧贴一行的细线。

---

## 4. `badge` —— 胶囊标签

| 项 | 默认值 |
|---|---|
| **变体** | 默认 `Style::Default`（`primary` 底 + `primary_foreground` 字） |
| 底 | 按变体；`st.bg` 显式覆盖 |
| 前景 | 按变体；`st.fg` 显式覆盖 |
| 圆角 | `st.radius` → 否则 **999**（胶囊） |
| 字号 | `st.font_size` → 否则 `theme.font_size - 3`（11） |
| 字重 | `st.font_weight` → 否则 **500** |
| 对齐 | 固定居中（水平 + 垂直） |
| 描边 | `Style::Outline` 自带 1px `border`；其余变体默认不描边 |

四个变体和 `button` 用的是**同一张皮肤表**（`detail::make_skin`），所以配色一定对得上：

| 变体 | 底 | 字 |
|---|---|---|
| `Style::Default` | `primary`（黑） | `primary_foreground` |
| `Style::Secondary` | `secondary`（浅灰） | `secondary_foreground` |
| `Style::Outline` | 透明 + 1px `border` | `foreground` |
| `Style::Destructive` | `destructive`（红） | `destructive_foreground` |

**生效的 Style 属性**：
`bg` `fg` `border_color` `border_width` `radius` `font_size` `font_weight`

```cpp
ui.badge("New", r.next(52.0f));
ui.badge("已验证", r.next(62.0f), aino::Style().background(aino::Color::hex(0x16A34A))
                                        .color(aino::Color::hex(0xFFFFFF)));
ui.badge("失败", r.next(52.0f), aino::Style().destructive());
```

> **默认变体是 Default（黑底），不是 Secondary。** 这一点以前是错的：实现里把
> `bg` / `fg` 硬编码成 `secondary`，`make_skin` 算出来的配色只被取了字重 —— 于是
> "Default 和 Secondary" 画出来一模一样，文档上写着四种变体、实际只有两种视觉。
> 现在四个变体都走皮肤表。

> `badge` 的宽度**不会自适应文字**。它按你给的 `box` 画，文字超宽会被裁。
> 要包住文字：`rect.w = ui.text_width(text, 11.0f, 500) + 20.0f`。

---

## 5. `progress` —— 进度条

| 项 | 默认值 |
|---|---|
| 轨道色 | `st.border_color` → 否则 `theme.muted` |
| 填充色 | `st.bg` → 否则 `theme.primary` |
| 圆角 | `st.radius` → 否则 **999**（两端圆的胶囊） |
| `value` | 自动 clamp 到 `0..1` |

**生效的 Style 属性**：`bg`（填充色）`border_color`（轨道色）`radius`

```cpp
ui.progress(0.68f, col.next(8.0f));                                     // 68%
ui.progress(0.32f, col.next(8.0f), aino::Style().background(aino::Color::hex(0xEF4444)));
```

注意字段的语义和其它组件**相反**：这里是 `bg` 决定**填充部分**，`border_color`
决定**轨道**。因为「填充」才是主体，轨道只是背景。

建议高度 **6~10px**（圆角 999 会自动退化成胶囊，不需要另设）。

---

## 6. `skeleton` —— 占位骨架

| 项 | 默认值 |
|---|---|
| 底色 | `st.bg` → 否则 `theme.muted` |
| 圆角 | `st.radius` → 否则 `theme.radius`（8） |
| 动画 | **默认静态**；`Style().animate()` 后**鼠标悬停才呼吸** |

**生效的 Style 属性**：`bg` `radius` `animated`

```cpp
ui.skeleton(col.next(16.0f));                          // 静态灰块（推荐）
ui.skeleton(col.next(16.0f), aino::Style().animate()); // 悬停时呼吸
```

### 为什么默认静态，且 `animate()` 也只悬停才动

呼吸感要靠时间驱动，也就是**必须每帧 repaint**。而 aino 的头号承诺是
「没有动画就空闲 0% CPU」—— 一个没人看的占位块不该让整窗从 0% 变成 20%。

所以「持续重绘」必须由调用方**点名要求**（`animate()`），而且**即便点名了，
也只在鼠标悬停时才推进**：常驻动画的代价和窗口面积成正比，不能默认付。

实测（460×300 窗口，单个 `animate()` 骨架）：
- 不悬停：**0 帧 / 0.00% CPU**
- 悬停：约 40fps、2.84% CPU（只在悬停期间）

呼吸的实现：`alpha = 255 * (0.55 + 0.25 * sin(phase))`，
`phase` 是**组件自己的局部相位**，只在悬停时按 `dt * 3.2` 累加 —— 所以移开再
移回来是「接着呼吸」，不会跳到全局时钟对应的相位（约 0.51Hz 的摆动频率）。

> 想要「永远在呼吸」的骨架，当前版本不提供 —— 那等于让整页常驻重绘。
> 真需要的话，在帧回调里对 `ui.repaint()` 自行控制节奏。

---

## 组合示例：统计卡片

```cpp
void statCard(aino::Ui& ui, const aino::Rect& r, const char* title, const char* value) {
    ui.panel(r);
    aino::Stack c = ui.vbox(r, 8.0f, 16.0f);
    ui.muted_text(title, c.next(18.0f), aino::Style().font(12.0f).left());
    ui.heading(value,    c.next(30.0f), aino::Style().font(22.0f).left());
    ui.progress(0.62f,   c.fill());
}
```

`panel` 提供面，`Stack` 提供内边距与行距，`muted_text` / `heading` 提供文字层级，
`progress` 提供一条信息。这是最省事的「卡片」写法。
