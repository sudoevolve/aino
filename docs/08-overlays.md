# 08 · 浮层类组件

四个组件都遵守同一条协议：**交互在帧内完成，绘制推迟到帧末**（`ui.deferred()`）。

```cpp
void tooltip(const Rect& anchor, const std::string& text, const Style& st = Style());
bool menu   (const std::string& label, const std::vector<std::string>& items,
             int* clicked_index, const Rect& box, const Style& st = Style());
bool dialog (const std::string& title, const std::string& body, bool* open,
             const Rect& box, const Style& st = Style());
void toast  (const std::string& text, const Rect& box, const Style& st = Style());
```

---

## 0. 为什么浮层一定在最上层

普通组件是「调用即绘制」。而浮层做了两件事：

1. **帧内**：读输入、算几何、决定要不要显示、登记一个绘制回调；
2. **帧末**：在 `end_frame` 里按登记顺序执行这些回调。

所以你在页面**任何位置**调用浮层，它都画在所有普通内容之上 ——
不需要手工分层，也不需要在最后一帧"补画"。

代价（也是唯一的规则）：

> 想在浮层**之上**再画东西（比如 dialog 里的按钮），必须紧接着浮层调用之后画。
> 那时已经处在帧末绘制阶段，后面的绘制自然压在它上面。

```cpp
if (ui.dialog("确认", "确定要删除吗？", &s.confirm_open, panel)) {
    // ↓ 这两个按钮画在遮罩和面板之上，不会被压暗
    if (ui.button("确定", {panel.x + 24, panel.bottom() - 60, 100, 36},
                  aino::Style().destructive())) { do_delete(); s.confirm_open = false; }
    if (ui.button("取消", {panel.x + 136, panel.bottom() - 60, 100, 36},
                  aino::Style().outline())) s.confirm_open = false;
}
```

---

## 1. `tooltip` —— 悬停提示

| 项 | 默认值 |
|---|---|
| 返回 | 无 |
| 触发条件 | **鼠标在 `anchor` 内**，且 `text` 非空、未 disabled |
| 气泡底 | `st.bg` → 否则 `theme.popover` |
| 气泡文字 | `st.fg` → 否则 `theme.popover_foreground` |
| 圆角 | 固定 **6** |
| 内边距 | 水平 8 / 垂直 5 |
| 字号 | `theme.font_size - 1`（13），字重 500，居中 |
| 位置 | `anchor` 上方 7px；上方放不下就翻到下方 |
| 水平居中 | 以 `anchor` 中心对齐，并 clamp 到窗口内（左右各留 4px） |
| 阴影 | `blur 10`、`offset 3`、alpha 34 |

**生效的属性**：`bg` `fg` `font_size` `disabled`

```cpp
ui.button("删除", box, aino::Style().destructive());
ui.tooltip(box, "永久删除，不可恢复");
```

### 两个注意点

1. **宽度不设上限**：气泡宽 = 文字宽 + 16。一段 200 字的中文会得到一个
   横跨整个屏幕的气泡。要长文案请自己截断，或改用 `alert` 常驻显示。
2. **不主动请求重绘**：源码里刻意没有 `repaint()`。理由是悬停期间鼠标每动
   一下窗口本来就会重画，常驻要帧会把空闲 CPU 从 0% 抬起来。
   副作用：鼠标**完全静止**地停在 anchor 上时，如果期间没有其它事件，
   气泡可能会因为"没有新的帧"而延迟出现——移动一下鼠标即可。

---

## 2. `menu` —— 下拉菜单

```cpp
bool menu(const std::string& label, const std::vector<std::string>& items,
          int* clicked_index, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = **本帧选中了某一项**（同时写入 `*clicked_index`） |
| 触发按钮 | 变体 `Outline`，圆角 `theme.radius_sm`，`pad_x` 12 |
| 按钮文字 | 500 字重，左对齐 |
| 箭头 | 自绘 V 形（不依赖字体） |
| 弹层底 | `theme.popover`，描边 `theme.border` 1px |
| 弹层阴影 | `blur 10`、`offset 3`、alpha 34 |
| 项高 | `max(box.h, 32)` |
| 列表高 | `项高 × 项数 + 8` |
| 列表宽 | `max(box.w, 最长项宽度 + 34)` |
| 项内边距 | 水平 10 |
| 项 hover 底 | `accent` 与前景色混合，圆角 `rad - 2` |

**生效的属性**：`bg` `fg` `border_color` `border_width` `radius` `font_size`
`font_weight` `pad_x` `disabled`

```cpp
int picked = -1;
if (ui.menu("选择操作", {"重命名", "复制", "删除"}, &picked, col.next(36.0f))) {
    apply(picked);
}
```

### 行为细节

- 点触发按钮 → 展开/收起（状态存在 `impl->flags[id]`）
- **点列表外部收起**（判据：本帧有按下且不在列表范围）
- 选中项后写入 `*clicked_index`、收起、返回 `true`
- 展开期间每帧 `repaint()` —— 因为要持续响应项的 hover 高亮
- 收起后回到 0 帧

`*clicked_index` 只在真的选中时被写；收起/展开不改变它。

---

## 3. `dialog` —— 模态框

```cpp
bool dialog(const std::string& title, const std::string& body, bool* open,
            const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | **本帧是否处于打开态**（不是"刚刚关闭"） |
| 遮罩 | 全窗口 `Color(0,0,0,110)`（约 43% 黑） |
| 面板底 | `st.bg` → 否则 `theme.background` |
| 面板描边 | `theme.border` 1px |
| 面板圆角 | `theme.radius_lg`（10） |
| `pad` | **24**（同时用于水平与垂直内边距） |
| 标题 | 字号 `fs + 3`、字重 600、**自动折行** |
| 正文 | 字号 `fs`、`muted_foreground`、`line_gap 3`、**自动折行** |
| 关闭按钮 | 右上角 22×22，× 自绘，hover 时 4.7% 前景色底 |
| 面板阴影 | `blur 22`、`offset 8`、alpha 46 |

**生效的属性**：`bg` `fg` `font_size` `pad_x` `radius`

### 三种关闭方式

| 方式 | 判据 |
|---|---|
| 点遮罩 | 鼠标在面板外 + 本帧左键**或**右键按下 |
| `Esc` | `key_pressed[KEY_ESC]` |
| 点右上角 × | 22×22 热区 |

关闭时置 `*open = false` 并请求重绘，但**当帧仍然按打开态画完** ——
这样不会出现"遮罩闪一下"。

### 返回值与写法

```cpp
bool shown = ui.dialog("确认操作", "删除后不可恢复。", &s.open, {60, 70, 340, 170});
if (shown) {
    // 只有打开时才需要处理面板内的按钮
}
```

> `dialog` 返回的是**打开态**，所以习惯写法是：
> ```cpp
> if (ui.dialog(...)) { /* 画面板内按钮 */ }
> ```
> 而不是 `if (ui.dialog(...)) { /* 刚被关闭 */ }`。

### 正文裁剪

正文被 `push_clip(box)` 裁在面板内。这条是必需的：正文一长就会横着爬出面板，
而**溢出到遮罩上的字最难发现** —— 遮罩本身是半透明黑，看起来只是"有点脏"。

面板高度要给够。正文折行需要多高可以自己量：

```cpp
float need = ui.text_height(body, panel.w - pad * 2, aino::Style().line_spacing(3.0f));
panel.h = 24 + title_lh + 10 + need + 24;   // 上下 pad + 标题 + 间距 + 正文
```

---

## 4. `toast` —— 轻提示条

| 项 | 默认值 |
|---|---|
| 返回 | 无 |
| 底色 | `st.bg` → 否则 `theme.popover` |
| 文字色 | `st.fg` → 否则 `theme.popover_foreground` |
| 描边 | `theme.border` 1px |
| 圆角 | `theme.radius`（8） |
| 字号 | `theme.font_size`，字重 500 |
| 左内边距 | 14；`variant == Destructive` 时 **18** + 3px 红竖条 |
| 阴影 | `blur 14`、`offset 5`、alpha 38 |
| 布局 | **单行**，垂直居中，超出裁剪 |

**生效的属性**：`bg` `fg` `border_color` `radius` `font_size` `variant` `disabled`

```cpp
if (s.toast_timer > 0.0f) {
    ui.toast("已保存", {r.right() - 200.0f, r.bottom() - 60.0f, 180.0f, 44.0f});
}
```

### 生命周期由你管

`toast` **不自动消失**，也没有进出场动画。要"显示 2 秒后消失"就自己倒计时：

```cpp
// AppState 里
float toast_left = 0.0f;   // 剩余秒数

// 触发时
s.toast_left = 2.0f;

// 帧回调里
if (s.toast_left > 0.0f) {
    ui.toast("已保存", toast_box);
    s.toast_left -= ui.frame_dt();
    ui.repaint();          // 倒计时需要帧，所以这里必须自己请求
}
```

注意最后那行 `ui.repaint()`：没有它，鼠标不动就不会有新帧，倒计时会停住。
这是「常驻动画必须显式要帧」规则的又一个例子。

位置通常自己摆在右下角：`{win.w - 200, win.h - 60, 180, 44}`。
`toast` 不知道窗口尺寸，全部由 `box` 决定。

---

## 5. 四者对照

| | `tooltip` | `menu` | `dialog` | `toast` |
|---|---|---|---|---|
| 返回 | — | 选中项 | 打开态 | — |
| 触发 | 鼠标悬停 | 点击 | `*open` | 调用方状态 |
| 自己管状态 | 否（纯瞬时） | 是（展开标志） | 外部 `bool*` | 外部 `bool` |
| 遮罩 | — | — | 全窗 43% 黑 | — |
| 阴影 blur | 10 | 10 | 22 | 14 |
| 常驻要帧 | 否 | 展开时 | 否 | 否 |
| 可嵌套 | 是 | 是 | 是（后画的在上） | 是 |

阴影强度是按「浮层离内容多远」定的：`tooltip`/`menu` 贴着内容，
`toast` 稍远，`dialog` 最高（22）因为它要"压住"整个页面。
早期版本是用叠 N 层透明圆角矩形做阴影，叠加处会累积成一坨黑；
现在走 SDF（距离场），峰值 alpha 严格不超过传入值。
