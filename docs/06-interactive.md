# 06 · 交互类组件

14 个能改值的组件。它们共享同一套交互机制，先读完第 0 节再读具体组件。

---

## 0. 通用交互机制

### 0.1 组件 id 由「种类 + 位置 + 额外键」算出

```cpp
id = hash(kind, box.x * 2, box.y * 2, extra)
```

`extra` 是文本哈希（button/badge）或基准 id + 索引（tabs 的每一项）。

**`box.w` / `box.h` 不参与**。这条规则有两个直接后果：

| 行为 | 是否安全 |
|---|---|
| 组件的宽度随窗口变化 | ✅ 安全，状态保留 |
| 组件整体**移动**（x/y 变） | ❌ id 变了，hover / 焦点 / 动画进度全部丢失 |
| 两个组件画在同一个 x/y 上 | ❌ id 冲突，hover 会互相串 |

所以：**不要在会移动的容器里依赖组件的持久交互态**。
页面级切换（不同页画不同内容）没问题，因为同一时刻只有一个页面在画。

### 0.2 hover / press 是插值出来的

```cpp
t_hover = animate_value(id, 目标 0/1, 速率 18)   // 多数组件
t_press = animate_value(id, 目标 0/1, 速率 34)   // 更快
```

速率单位是「每秒趋近的指数系数」，18 ≈ 55ms 到 63%，34 ≈ 30ms。
所以按下比悬停更"跟手"。

这些插值**只在值变化时请求重绘**，值稳定后就停了 —— 不会一直吃 CPU。

### 0.3 焦点

```cpp
void* ui.focused_id() const;       // 当前焦点是谁
void  ui.set_focus(void* id);      // 强制移焦
```

需要焦点的组件：`input` `textarea`（光标闪烁 + 键盘输入）。
其它组件点击时也会 `grab_focus`（为了画焦点环），但不需要键盘。

**点击空白会失焦**：`input`/`textarea` 会检测「按下位置不在自己身上」并主动 `drop_focus`。

### 0.4 禁用

`Style().off()` 或 `Style().disabled = true`：

- 控件不再响应 hover / 点击（`interact(..., enabled=false)`）
- 填充色与前景色的 alpha 压到 **45%**
- 返回值恒为 `false`

### 0.5 返回值

| 语义 | 组件 |
|---|---|
| 本帧被点击（按下并在框内松开） | `button` `icon_button` `nav_item` |
| 值发生了变化 | `checkbox` `toggle` `slider` `radio_group` `stepper` `rating` `textarea` `input` |
| 选中索引变了 | `tabs` `segmented` `select` |

「值变化」类组件**每次变化都返回 true**，所以拖动 slider 时会连续返回 true ——
这是设计如此，不是重复触发。要「松手才处理」就自己判断 `!ui.input().down[0]`。

---

## 1. `button` —— 按钮

```cpp
bool button(const std::string& text, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 本帧被点击 |
| 变体 | `Default`（primary 填充） |
| 对齐 | **强制居中**（除非显式 `.left()` / `.top()`） |
| 圆角 | `theme.radius`（8） |
| 字号 | `theme.font_size`（14） |
| 字重 | `st.font_weight` → 否则 400 |
| 光标 | `Hand` |

**生效的属性**：全部 6 个变体 + `bg` `fg` `border_color` `border_width` `radius`
`font_size` `font_weight` `italic` `align` `valign` `disabled` `press_scale`

### 按压缩放

`press_scale` 默认 1.0（不缩放）。想要「按下去小一圈」：

```cpp
ui.button("按我看看", box, aino::Style().primary().scale_on_press(0.97f));
```

### 建议尺寸

高度用 `theme.control_h`（36）。宽度按内容：
`ui.text_width(text, 14.0f, 400) + 32.0f`；想占满一行就直接把 `box.w` 给足
（`button` 天生按传入的 `box` 铺满，不会只包住文字）。

```cpp
if (ui.button("保存", col.next(36.0f), aino::Style().primary())) save();
if (ui.button("取消", col.next(36.0f), aino::Style().outline())) close();
if (ui.button("删除", col.next(36.0f), aino::Style().destructive())) remove();
```

> 返回值只在「按下 + 在同一次按下期间于框内松开」时为 true。
> 按住后把鼠标拖出框再松开**不会**触发 —— 这样才符合按钮的常规预期。

---

## 2. `icon_button` —— 图标按钮

```cpp
bool icon_button(const std::string& glyph, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 本帧被点击 |
| 变体 | **`Ghost`**（透明底，hover 才出现底色） |
| 字号 | `min(box.w, box.h) * 0.55`（自动贴合按钮大小） |
| 字重 | 400（固定） |
| 圆角 | `theme.radius_sm`（4） |
| 对齐 | 居中 |

**生效的属性**：变体 + `bg` `fg` `border_*` `radius` `font_size` `disabled`

```cpp
ui.icon_button(aino::icons::close, {r.right() - 36, r.y, 32, 32});
ui.icon_button("+",  b0, aino::Style().outline());   // 带框
```

> `glyph` 是一个**字符串**，不是图标枚举。用 `aino::icons::*` 的内置常量
> （见 [02](02-style-and-theme.md) 第 5 节）或自己写 UTF-8 字符。
> 字体缺字形会显示成方框。

---

## 3. `nav_item` —— 导航条目

```cpp
bool nav_item(const std::string& label, bool active, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 本帧被点击 |
| 对齐 | **左对齐**（这是它与 `button` 的核心区别） |
| `pad_x` | 14 |
| 圆角 | `theme.radius_sm`（4） |
| 字号 | `theme.font_size` |
| hover 底色 | `accent` 的淡版本 |
| 选中底色 | `accent` 满色 + 前景 `foreground` / 未选中 `muted_foreground` |

**生效的属性**：`bg` `fg` `font_size` `font_weight` `pad_x` `radius` `disabled`

```cpp
for (int i = 0; i < page_count; ++i)
    if (ui.nav_item(kNames[i], page == i, side.next(34.0f))) page = i;
```

`active` 是**外部传入的持续状态**（不是组件自己记的），这正是立即模式：
选中谁由你的 `page` 变量决定。

侧边栏里配一条左侧竖条选中指示器很常见：

```cpp
// 在 nav_item 之前画
ui.fill_rect({side_x, row.y + 6, 3, 22},
             page == i ? aino::Color::hex(0x18181B) : aino::Color::none(), 1.5f);
```

---

## 4. `input` —— 单行输入

```cpp
bool input(std::string* value, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 文本或光标位置**发生了变化** |
| 底色 | `st.bg` → 否则 `theme.background` |
| 描边 | `st.border_color` → 否则 `theme.input`，1px |
| 圆角 | `theme.radius_sm`（4） |
| `pad_x` | **12** |
| 字号 | `theme.font_size` |
| 字重 | 400（固定） |
| 占位符色 | `muted_foreground` |
| 光标 | 悬停或聚焦时 `IBeam` |

**生效的属性**：`bg` `fg` `border_color` `border_width` `radius` `pad_x`
`font_size` `placeholder` `disabled`

### 支持的键盘操作

| 按键 | 行为 |
|---|---|
| 字符输入 | 插入（IME 中文走 `text_input`） |
| `Backspace` / `Delete` | 删除前/后一个**码点**（中文不会删半个字） |
| `Ctrl+Backspace` / `Ctrl+Delete` | 删除到上一个/下一个词边界 |
| `Home` / `End` | 跳到首/尾 |
| `←` / `→` | 左右移一个码点 |
| `Ctrl+←` / `Ctrl+→` | 按词跳（中文一个字一步，西文一个词一步） |
| `Shift+←/→/Home/End` | 扩展选区 |
| `Ctrl+A` | 全选 |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | 复制 / 剪切 / 粘贴（走系统剪贴板） |
| `Ctrl+Z` | 撤销 |
| `Ctrl+Y` / `Ctrl+Shift+Z` | 重做 |
| `Enter` | **不吞**，由调用方在 `ui.input().key_pressed[KEY_ENTER]` 里处理 |

鼠标：单击定位、**按住拖动选**、**双击选词**、**三击全选**、`Shift+单击` 扩选。

### 撤销 / 重做

`input` 和 `textarea` 各自维护一份撤销栈（按控件 id 记忆，最多 200 步）：

| 规则 | 说明 |
|---|---|
| 合并 | 连续打字（0.7s 内、同类操作）合成**一步** —— 打 20 个字只按一次 `Ctrl+Z` |
| 边界 | 停顿超过 0.7s、切换操作类型、粘贴/剪切，都会另起一步 |
| 位置 | 快照里带光标与选区，撤销后光标回到当时的位置 |
| 分支 | 撤销之后再输入会**清空 redo 栈**（不会把新内容覆盖掉） |

内部实现是「改动前的文本快照」，不是操作日志 —— 大文档里每步快照都拷贝整串，
20 万字的文档按 200 步算最多几十 MB。真实场景（输入框里几百字）完全可以忽略；
要编辑超大文档，请自己维护增量撤销栈（本库不管业务语义）。

### 一个踩过的坑：修饰键必须在"按下那一刻"锁存

`Input::ctrl()` 读的是**当前**状态。同一批窗口消息里，`Ctrl` 的 `WM_KEYUP` 可能
排在 `WM_KEYDOWN('Z')` 之后、但在这一帧渲染之前就被处理掉 —— 帧里 `ctrl()` 已经
是 `false`，`Ctrl+Z` 就收不到了（实测快速按键必现，打字快的人一定遇到）。

所以后端在 `WM_KEYDOWN` 的那一刻把修饰位**定格**到 `key_ctrl[k] / key_shift[k] /
key_alt[k]`，组合键判定用 `Input::ctrl_of(k)`（= 本帧按下的 k 当时 Ctrl 是否按住，
自带 `key_pressed[k]` 判定）；`ctrl()`/`shift()` 仍然表示"此刻是否按住"，
给"按住 Ctrl 连续点选"这类场景用。

> 另外：组合键按下后的 150ms 内会丢弃文本字符。真实 `Ctrl+Z` 送的是
> `WM_CHAR(0x1A)`，已经被后端过滤；但某些输入法/布局会送可打印字符，
> 那就变成"撤销一次顺手打了个 Z"。

### 光标闪烁 = 常驻动画

聚焦时每帧 `repaint()`。**未聚焦的 input 静置是 0 帧、0% CPU**，
只有一个聚焦的输入框才会持续要帧（实测约 1~2%）。

```cpp
ui.input(&email, col.next(36.0f), aino::Style().hint("you@example.com"));
```

> `box` 的宽度决定可见区域，文本超出后**自动横向滚动**（跟随光标）。
> 高度建议 36（`theme.control_h`）。

### 中文输入法（IME）

聚焦时库会**每帧**把光标位置报给平台层，由平台设置输入法的两个窗口：

| 窗口 | 位置 |
|---|---|
| 组合窗（拼音串） | 光标 `x`，输入框**下沿** |
| 候选窗（候选字列表） | 光标 `x`，输入框下沿 **+4px** |

Windows 后端用 `ImmSetCompositionWindow` / `ImmSetCandidateWindow`。

**为什么必须显式做**：窗口如果什么都不说，系统会把拼音串和候选框丢在客户区
左上角（或跟着鼠标），于是和一个放在页面中部的输入框完全对不上——这就是
「IME 没在输入区域下方对齐」的表现。

**保持零依赖**：`imm32.dll` 是 `LoadLibrary` + `GetProcAddress` 动态取的，
所以 Windows 上编译仍然**只链 `user32` + `gdi32`**。取不到就静默降级
（输入法的默认定位行为），不影响编译和运行。

> 局限：库**不自绘**组合串，拼音由输入法自己画在输入框下沿。
> 想做成 Chrome 那种"拼音内联显示在文字中间 + 虚线下划线"，需要在
> `WM_IME_COMPOSITION` 里取 `GCS_COMPSTR` 并参与排版 —— 当前版本没做。
>
> 断言在 `tools/probe/ime_probe.cpp`（6 项）：结构体布局回读、
> 组合窗/候选窗坐标、以及"点进输入框后库确实上报了位置"。

---

## 5. `checkbox` —— 复选框

```cpp
bool checkbox(bool* value, const std::string& label, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 勾选态变了 |
| 方框尺寸 | 高度取 `box.h`，方框是正方形 |
| 方框底（未选） | `theme.input` |
| 方框底（选中） | `theme.primary` |
| 对勾色 | `theme.primary_foreground` |
| 标签字号 | `theme.font_size` |
| 焦点环 | `theme.ring` |

**生效的属性**：`fg` `font_size` `disabled`

```cpp
ui.checkbox(&remember, "记住这台设备", col.next(22.0f));
```

点**标签也算点方框**（整个 `box` 都是热区）。建议高度 22，方框会自动居中。

---

## 6. `toggle` —— 开关

```cpp
bool toggle(bool* value, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 开关态变了 |
| 轨道（关） | `theme.input` |
| 轨道（开） | `theme.primary` |
| 滑块色 | `foreground` 与 `background` 按主题自动选 |
| 动画 | 滑块与轨道色渐变 |

**生效的属性**：`font_size` `disabled`（其它字段基本不吃，样式由主题决定）

```cpp
ui.toggle(&notifications, {r.right() - 44.0f, r.y, 44.0f, 26.0f});
```

**建议尺寸 44×26**（成比例的胶囊）。给宽高不一致的盒子会变形。

---

## 7. `slider` —— 滑杆

```cpp
bool slider(float* value, float min_value, float max_value, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 值变了（拖动中会**连续**返回 true） |
| 轨道高 | **4px**，水平内缩 7px |
| 轨道色 | `st.border_color` → 否则 `theme.muted` |
| 已选段色 | `st.bg` → 否则 `theme.primary` |
| 滑块半径 | 7（hover 时 8） |
| 滑块色 | `theme.background`，2px 描边用已选段色 |
| 光标 | `ResizeH` |

**生效的属性**：`bg`（已选段）`border_color`（轨道）`disabled`

```cpp
ui.slider(&volume, 0.0f, 1.0f, col.next(20.0f));
ui.slider(&size, 8.0f, 72.0f, col.next(20.0f), aino::Style().background(aino::Color::hex(0x2563EB)));
```

拖动是**线性映射**：`value = min + (max - min) * (mouse.x - box.x) / box.w`，
所以点是哪里就跳到哪里（不是相对拖动）。

高度给 20 就够（轨道只有 4px，其余是滑块的活动空间）。

---

## 8. `tabs` —— 页签

```cpp
bool tabs(const std::vector<std::string>& labels, int* index, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 选中项变了 |
| 容器底 | `st.bg` → 否则 `theme.muted` |
| 容器圆角 | `theme.radius`（8），内部项圆角 `radius - 3` |
| 内边距 | **3px**（四周） |
| 选中项底 | `st.border_color` → 否则 `theme.background`（深色主题下自亮 7%） |
| 选中文字 | `foreground` / 字重 **600** |
| 未选文字 | `muted_foreground` / 字重 500 |

**生效的属性**：`bg`（容器底）`border_color`（选中项底）`radius` `font_size` `disabled`

```cpp
ui.tabs({"概览", "按钮", "表单"}, &tab, col.next(38.0f));
```

各项**等分**容器宽度（`inner.w / labels.size()`），不支持自定义每项宽度。

---

## 9. `segmented` —— 分段控件

```cpp
bool segmented(const std::vector<std::string>& items, int* index, const Rect& box, const Style& st = Style());
```

```cpp
inline bool Ui::segmented(...) { return tabs(items, index, box, st); }
```

**与 `tabs` 完全同构**，实现是同一个函数。区别只在语义：

- `tabs` 切的是「页面」（内容完全不同）
- `segmented` 选的是「同一个值的不同表达」（日 / 周 / 月）

```cpp
ui.segmented({"日", "周", "月"}, &range, col.next(34.0f));
```

---

## 10. `select` —— 下拉选择

```cpp
bool select(const std::vector<std::string>& items, int* index, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 选中项变了 |
| 底色 | `theme.background` |
| 描边 | `theme.input`；展开时变 `theme.ring`；hover 时与前景色混合 |
| 圆角 | `theme.radius_sm`（4） |
| `pad_x` | **12**（硬编码） |
| 弹层底 | `theme.popover` |
| 弹层阴影 | `blur 10` |
| 选中项底色 | `accent` |

**生效的属性**：`bg` `fg` `border_color` `border_width` `font_size` `disabled`

```cpp
ui.select(items, &idx, col.next(36.0f));
```

### 行为细节

- 点主体 → 展开/收起；展开状态存在 `impl->flags[id]` 里
- **点外部自动收起**（判据：本帧有按下且不在弹层范围内）
- 选中项后立即收起并写回 `*index`
- 弹层宽度取 `max(box.w, 最长项宽度 + 34)`，向下展开
- `*index` 越界时显示为空（不崩）

展开期间**每帧 `repaint()`** —— 因为弹层要持续响应 hover 高亮。
收起后回到 0 帧。

---

## 11. `radio_group` —— 单选组

```cpp
bool radio_group(const std::vector<std::string>& items, int* index, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 选中项变了 |
| 布局 | **纵向等分 `box.h`**，每行一个圆环 + 标签 |
| 圆环色（未选） | `theme.input` |
| 圆环色（选中） | `theme.primary`，描边变粗 |
| 中心点 | 选中时从中心"长出来"（`t*(2-t)` ease-out） |
| 字号 | `theme.font_size` |

**生效的属性**：`fg` `font_size` `disabled`

```cpp
ui.radio_group({"选项一", "选项二", "选项三"}, &choice, col.next(100.0f));
```

高度决定每行的间距：`box.h / items.size()` 就是行高。3 个选项给 100 ≈ 每行 33。

---

## 12. `stepper` —— 数值步进器

```cpp
bool stepper(float* value, float step, float min_value, float max_value,
             const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 值变了 |
| 布局 | `[−]  数值  [+]` 三段式 |
| 按钮宽 | `min(box.h, 34)` |
| 底色 | `theme.background` |
| 描边 | `theme.input` 1px |
| 圆角 | `theme.radius_sm`（4） |
| 数值显示 | 保留 **1 位小数** |

**生效的属性**：`bg` `fg` `border_color` `border_width` `radius` `font_size` `disabled`

```cpp
ui.stepper(&count, 1.0f, 0.0f, 99.0f, col.next(36.0f));
ui.stepper(&ratio, 0.1f, 0.0f, 1.0f, col.next(36.0f));
```

值会被 clamp 到 `[min, max]`。`step` 可正可负（负值让 `+` 变减）。
最小宽度约 90（要有地方显示数字）。

---

## 13. `textarea` —— 多行文本

```cpp
bool textarea(std::string* value, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 内容或光标变了 |
| 底色 | `theme.background` |
| 描边 | `theme.input` 1px |
| 圆角 | `theme.radius_sm`（4） |
| `pad_x` / `pad_y` | 10 / 8 |
| 行高 | 按字体算（同 `line_height`） |
| 光标 | 聚焦时闪烁（**常驻动画**） |

**生效的属性**：`bg` `fg` `border_color` `border_width` `radius` `pad_x` `pad_y`
`font_size` `placeholder` `disabled`

### 换行规则

- **只按 `\n` 换行，不做软换行**（这一条与 `label` + `wrap` 不同）
- 超宽的行**横向裁掉**（不横向滚动）
- `Enter` 插入 `\n`
- 超出高度的内容**纵向滚动**（跟随光标）

```cpp
ui.textarea(&note, col.next(92.0f), aino::Style().font(13.0f));
```

> 需要"自动折行的多行编辑器"目前做不到 —— 软换行会让"光标在第几行第几列"
> 的映射复杂一个数量级，与体积目标冲突。要折行展示就用 `label` + `wrap_text`。

### 中文输入法（IME）

和 `input` 同一套：聚焦时把组合窗钉在**光标所在行的下沿**，候选窗再往下 4px。
光标行滚出可视区时，位置会夹到最近的一行，不会飞到框外。

---

## 14. `rating` —— 星级评分

```cpp
bool rating(int* value, int max_stars, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 分数变了 |
| 星星尺寸 | `min((box.w - 4*(n-1)) / n, box.h)` |
| 星间距 | **4px** |
| 空星色 | `theme.muted`；浅色主题下（亮度 > 0.82）改用 `theme.border` |
| 实星色 | `st.bg` → 否则 `theme.primary` |
| 上限 | `max_stars` 1..32（超出直接返回 false） |
| 光标 | `Hand` |

**生效的属性**：`bg`（实星色）`disabled`

```cpp
ui.rating(&stars, 5, col.next(28.0f));
```

### 交互

- 点第 i 颗星 → `*value = i + 1`
- 点**当前已选的最后一颗**→ 清零（`*value = 0`）
- 悬停时给星星勾一圈细边，提示"可点"

星星是**自绘五角星**（凹多边形，用 `raster_poly`），不是字体字符 ——
所以不依赖系统字体有没有 ★ 字形。

---

## 15. 组合示例：一个完整的表单

```cpp
void composeForm(aino::Ui& ui, AppState& s, const aino::Rect& area) {
    ui.panel(area);
    aino::Stack c = ui.vbox(area, 12.0f, 20.0f);

    ui.subheading("账号", c.next(26.0f), aino::Style().left());
    ui.input(&s.email,    c.next(36.0f), aino::Style().hint("you@example.com"));
    ui.input(&s.password, c.next(36.0f), aino::Style().hint("密码"));
    ui.muted_text("至少 8 位，含大小写与数字", c.next(18.0f), aino::Style().font(12.0f).left());

    ui.separator(c.next(1.0f));
    ui.subheading("偏好", c.next(26.0f), aino::Style().left());
    ui.checkbox(&s.remember, "记住这台设备", c.next(22.0f));
    ui.checkbox(&s.autosave, "自动保存",     c.next(22.0f));

    aino::Rect row = c.next(26.0f);          // 标签 + 开关同一行
    ui.label("桌面通知", row, aino::Style().left());
    ui.toggle(&s.notify, {row.right() - 44.0f, row.y, 44.0f, 26.0f});

    ui.segmented({"日", "周", "月"}, &s.range, c.next(34.0f));
    ui.slider(&s.zoom, 0.5f, 2.0f, c.next(20.0f));

    if (ui.button("保存", c.fill(), aino::Style().primary())) s.save();
}
```
