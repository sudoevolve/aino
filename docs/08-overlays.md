# 08 · 浮层类组件

本篇 10 个浮层都遵守同一条协议：**交互在帧内完成，绘制推迟到帧末**（`ui.deferred()`）。
前四节（`tooltip` `menu` `dialog` `toast`）是最早的四个，机制讲得最细；0.3.0 补的六个
在 §5 里，它们只差语义，不差机制。

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

### 视觉在上层 ≠ 交互在上层

"最后画"只解决视觉。**交互是按调用顺序处理的** —— 下层控件先跑，会先把输入吃掉。
这就是所谓的"穿透"，而它不止点击一种，一共有六条通道：

| 通道 | 不挡会看到什么 |
|---|---|
| 点击 / 双击 | 点遮罩、点面板，顺手点到后面的按钮 |
| 悬停（hover） | 遮罩下的按钮亮起来，还可能在跑 hover 插值动画 |
| 光标形状 | 指针移到被盖住的按钮上，照样变成小手 / I 形 |
| 滚轮 | 遮罩后面的滚动区悄悄滚动 |
| 键盘 | 焦点还在被盖住的输入框里，打的字照样进去 |
| 悬停副作用 | tooltip 从遮罩里冒出来、spinner 在遮罩下继续转 |

**只挡第一条等于没挡**：用户最先察觉的往往是第二、三条。

### 输入闸门：位置 + 层级

浮层在打开期间登记自己的矩形，以及**自己从哪个层级开始**（`z_first`）。
从下一帧起，`detail::interact()` 用两条规则判定：

```text
落在所有浮层区域之外                        → 挡住      （遮罩）
落在浮层区域内，但控件的 z 比该浮层小        → 挡住      （被面板盖住的东西）
```

第二条是关键。只按位置放行（白名单）的话，**面板矩形范围内的一切都算"允许"** ——
被面板压在底下的按钮照样吃 hover 和 click，看上去和没修一样。

`z` 就是 `interact()` 的调用序号：立即模式下"调用顺序 = 绘制顺序 = 层级"，
序号小的先画、在下面。所以每条登记必须传**浮层第一个控件的序号**（函数入口处取），
而不是登记那一刻的序号 —— `dialog` 的关闭按钮在登记之前就 `interact()` 过，
传晚了它会把自己挡住（表现为"关闭按钮点了没反应"）。

| 浮层 | 登记的可交互区 | 层级起点 |
|---|---|---|
| `dialog` | 面板本身 | 关闭按钮的序号 |
| `menu` | 触发按钮 + 下拉列表 | 触发按钮的序号 |
| `select` | 主体 + 下拉列表 | 主体的序号 |
| `toast` / `tooltip` | 不登记（纯展示，不该截输入） | —— |

### 挡住的不只是 interact

不经过 `interact()` 的读取也要过同一道判定（按调用点的 `seq` 判断）：

| 位置 | 走哪个判定 |
|---|---|
| `interact()`（所有组件的 hover / pressed / clicked / 双击 / 光标） | `gate_blocked(z, x, y)` |
| `scroll()` 两处滚轮 | `gate_blocked_pointer()` |
| `tooltip()` 的悬停命中 | `gate_blocked_pointer()` |
| `spinner()` / `skeleton()` 的悬停动画 | `gate_blocked_pointer()` |
| `input()` / `textarea()` 的键盘焦点 | `gate_blocked_here(box)`，被盖住就 `drop_focus` |

焦点那条尤其容易漏：块本身已经收不到点击，但**焦点还留着**，键盘照收。
现在的行为是"浮层打开 → 被盖住的输入框失焦"，和模态语义一致。

三条由此派生的规则：

1. **关闭后自动释放**：没有浮层登记的那一帧就不挡，不存在"忘了解除模态"这种事。
2. **差一帧**：浮层在帧末才登记，所以打开的那一帧不挡。反过来，点遮罩关闭的那一帧
   照样挡着 —— 否则"点遮罩关窗"会顺手点到后面的按钮，这是最容易复现的穿透。
3. **浮层自己的内容不受影响**：它的控件序号大于等于 `z_first`，正常交互。

> 闸门只管**库自己**读输入的地方。你自己在页面里写的 `box.contains(mouse)`
> 或手工 `set_cursor()` 不在其中 —— 那种判断请改用组件，或自行加 `!浮层打开` 条件。

回归用例：`tools/probe/overlay_probe.cpp`（35 项，覆盖上面六条通道 + 层级）。
拿"只挡位置"的旧实现跑同一探针会红 8 项，说明这些断言确实抓得到。

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

## 5. 0.3.0 新增的六个浮层

### 5.1 `popover` —— 点触发器弹任意内容

```cpp
bool open = false;
ui.popover("排序", &open, trigger_rect, aino::Vec2(200, 96), [&](const aino::Rect& c) {
    ui.item("按名称", "", "", {c.x, c.y, c.w, 30});
    ui.item("按时间", "", "", {c.x, c.y + 32, c.w, 30});
});
```

`Vec2` 是**内容区**尺寸（不含内边距），回调拿到的是内容区内缩后的矩形。返回"本帧是否打开"。

回调在**帧末**执行，和弹层同一时刻 —— 所以里面可以直接调 `ui.button()` / `ui.input()`，
它们画出来的东西天然盖在所有普通组件之上。这些控件到帧末才 `interact`，`seq` 已经最大，
闸门一定放行（面板区域本来就 claim 过，而它们的 `z` 更大，判定为"在上面"）。

### 5.2 `hover_card` —— 悬停出卡片

```cpp
ui.hover_card(anchor_rect, aino::Vec2(210, 78), [&](const aino::Rect& c) {
    ui.label("张伟", {c.x, c.y, c.w, 22});
});
```

和 `tooltip` 的区别是**能画任意东西**，和 `popover` 的区别是**不抢点击、不抢焦点**。

| 项 | 行为 |
|---|---|
| 触发 | 指针进 `anchor` 就出，**没有延时**（`tooltip` 有 0.4s） |
| 位置 | 锚点右侧 +8；右边放不下翻到左侧；上下夹在窗口内 |
| 尺寸 | `Vec2 + 四周各 12px` 内边距 |
| 阴影 | blur 18 / alpha 44 / dy 6 |
| 遮罩 | 无 |
| 输入闸门 | **不登记** —— 它不该拦任何输入 |

因为不登记闸门，卡片里的内容是**画给人看**的，不要在里面放按钮（放了也点不到，
而且会让人以为坏了）。要可交互就用 `popover`。

> 它靠 `impl->hover_region(anchor)` 登记锚点。不登记的话指针停在锚点上不动时框架判不出
> "这里要画东西"，卡片会冻在那儿不出现 —— 参见 [10 性能](10-performance.md) 的 §6.2。

### 5.3 `alert_dialog` —— 带确认/取消的模态框

```cpp
if (ui.alert_dialog("删除？", "删了不能恢复", "删除", "取消", &open, box,
                    aino::Style().destructive()))
    do_delete();
```

和 `dialog()` 的差别不只是多了两个按钮，**返回值语义不同**：

| | `dialog()` | `alert_dialog()` |
|---|---|---|
| 返回 | 本帧是否处于打开态 | **本帧是否点了确认** |
| 按钮 | 调用方自己画 | 内建确认 / 取消 |
| 确认键配色 | — | `Style().destructive()` → 红底 |

危险操作要的就是那一个布尔，所以别拿 `dialog()` 去拼。取消键点了会自己把 `*open` 置 false。

### 5.4 `drawer` —— 从边上出来

```cpp
ui.drawer("详情", &open, /*side=*/0, {w - 300, 0, 300, h}, [&](const aino::Rect& c) {
    ui.label("内容", c);
});
```

`side`：0 = 右、1 = 左、2 = 下、3 = 上。`box` 就是面板本身的位置和大小，库不替你算宽度。

- 关闭方式三种：**Esc**、**点面板外**、**点右上角 ×**；
- 遮罩 alpha 90 × 淡入进度，面板内缘一侧投阴影（blur 22 / alpha 40），看起来才是"贴边滑出"；
- 标题行高 `max(fs + 32, 54)`，内边距 20（`st.pad_x` 可覆盖），内容区自动裁剪；
- 面板整体 `gate_claim()`，所以它开着的时候后面点不动。

> **只做淡入，不做位移。** 位移只动画面、不动命中区（`box` 里的控件位置是固定的），
> 滑到一半去点按钮会点偏。淡入既保留"出现"的感觉，又不会让按钮看着和点着的地方不一致。

### 5.5 `context_menu` —— 右键菜单

```cpp
int picked = -1;
if (ui.context_menu(area, {"标记已读", "归档", "删除"}, &picked)) apply(picked);
```

- 右键点在 `area` 内、且该点**没被别的浮层压住**时才弹；
- 弹出位置记的是**按下那一刻**的鼠标坐标，之后手怎么动菜单都不跟 —— 跟着跑就没法点了；
- 宽度按最长一项算（至少 120），整体夹在窗口内；
- 点外面 / Esc 关闭；返回 true 表示选中了某一项并写入 `*clicked_index`。

### 5.6 `menubar` —— 横排菜单栏

```cpp
std::vector<std::string> labels = {"文件", "编辑", "视图"};
std::vector<std::vector<std::string>> items = {
    {"新建", "打开…", "保存"}, {"撤销", "重做"}, {"放大", "缩小"},
};
aino::MenuPick mp = ui.menubar(labels, items, {0, 0, w, 34});
if (mp.valid()) log(labels[mp.menu] + " → " + items[mp.menu][mp.item]);
```

返回值是 `MenuPick{ menu, item }`（**索引**，不是字符串），没选到就是 `MenuPick{}`，
用 `mp.valid()` 判。

它和 `menu()` 的关键差别是**点开一个之后，鼠标划到隔壁会自动切** —— 桌面菜单栏的老规矩，
Web 上一堆库做不到。要让这条成立，实现上必须把**整条菜单栏**一起 `gate_claim()`：

- 只登记当前触发器的话，鼠标一离开它就被闸门挡住，表现成"菜单卡住划不过去"；
- claim 登记的是**下一帧**的白名单，所以顺序必须是
  「先 `interact` 所有顶级项 → 再算最终打开的是第几个 → 最后 claim」。
  写反了联动会慢一帧，感觉是"划过去要动两下才切"。

> 测试时注意：`click_logical` 会先发一个 `WM_MOUSEMOVE`，菜单栏这种"hover 就切"的控件
> 那一帧已经切过去了，紧接着的按下/松开落在"已经是当前项"上 = 收起。看起来像"点了没反应"，
> 其实是标准行为。

---

## 6. 四者对照

| | `tooltip` | `menu` | `dialog` | `toast` |
|---|---|---|---|---|
| 返回 | — | 选中项 | 打开态 | — |
| 触发 | 鼠标悬停 | 点击 | `*open` | 调用方状态 |
| 自己管状态 | 否（纯瞬时） | 是（展开标志） | 外部 `bool*` | 外部 `bool` |
| 遮罩 | — | — | 全窗 43% 黑 | — |
| 阴影 blur | 10 | 10 | 22 | 14 |
| 常驻要帧 | 否 | 展开时 | 否 | 否 |
| 输入闸门 | 不登记 | 触发器+列表 | 面板 | 不登记 |
| 可嵌套 | 是 | 是 | 是（后画的在上） | 是 |

阴影强度是按「浮层离内容多远」定的：`tooltip`/`menu` 贴着内容，
`toast` 稍远，`dialog` 最高（22）因为它要"压住"整个页面。
早期版本是用叠 N 层透明圆角矩形做阴影，叠加处会累积成一坨黑；
现在走 SDF（距离场），峰值 alpha 严格不超过传入值。
