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

**淡入的是 alpha，不是整条颜色**。透明底控件（ghost / outline）的底色是
`Color::none()`，`state_fill()` 对它只推进 alpha、RGB 保持目标色。早先这里写成
`base.mix(hover, t_hover)`，`mix` 连 RGB 一起从黑色插值，中间帧变成"深灰"（244 的
accent 在 t=0.5 时 blend 出来是 185，比背景 249 和终态 244 都暗）—— 表现就是
**每次悬停都先闪一下深灰**，而且只有带 hover 淡入的控件有，静态表格没有。
`hover_fade_probe` 逐帧量中间帧亮度，钉住这条。

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

### 0.6 一次按下只归一个控件

同一帧里可能有多个控件都包含指针位置：叠放的卡片、被裁剪区部分盖住的按钮、
列表里滚出去却还压在别处的行。**"按下"这个事件只属于其中一个** —— 先画的那个
先认领，后面的控件这一帧拿不到 press（`impl->press_taken`）。

少了这道闸，一次点击会被后面的控件重新认领一遍：按下和松开落在同一帧时，
两个控件会各触发一次（一个点击改两份状态）；不在同一帧时更隐蔽 —— 后画的控件
在按下帧抢走 `impl->active`，松开帧只有它算"被点中"，先画的按钮看起来完全没反应。

这条和"看得见才点得着"是一对：`interact()` 先把控件矩形跟当前裁剪区求交，
被裁掉的部分不参与命中。两条合起来才解释得清 gallery 侧边栏那个现象 ——
点页脚的主题按钮没反应、页面却跳走了。

### 0.7 一次点击会多排一帧

立即模式有个天然的顺序坑：控件按**绘制顺序**处理，导航行画在最前面，可"点它切页"
改变的状态影响的是后面画的内容 —— 于是这一帧里内容是新的、导航高亮还是旧的。
如果状态改完没人再排帧，这帧错位画面就一直挂在屏幕上，直到下一次鼠标移动才修正。
表现就是"点了导航，页面换了但高亮卡住不动"。

所以 `interact()` 一旦返回 `pressed` / `clicked`，就补一次 `request_repaint()`：
点击这种事件一帧排两次不浪费，落帧的状态才是一致的。空闲时没有任何点击，
不影响"静止 0 帧 / 0.00% CPU"。


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

### 输入法：拼音要画在框里

只处理 `WM_CHAR` 的话，**上屏前**那串拼音归系统那个悬空组合窗管，输入框里一个
字都看不见 —— 看起来就像"输入法没预填充"。后端现在会取组字串：

| 消息 | 处理 |
|---|---|
| `WM_IME_STARTCOMPOSITION` | 清空草稿 |
| `WM_IME_COMPOSITION`（非 `GCS_RESULTSTR`） | **无条件**去问 `ImmGetCompositionString(GCS_COMPSTR)`，存进 `ui.impl->ime_comp`（UTF-8） |
| `WM_IME_COMPOSITION` + `GCS_RESULTSTR` | 清空草稿（正文字符随后由 `WM_CHAR` 送到） |
| `WM_IME_ENDCOMPOSITION` | 清空草稿 |
| `WM_IME_SETCONTEXT` | 去掉 `ISC_SHOWUICOMPOSITIONWINDOW` |

`input` / `textarea` 在聚焦时把 `ime_comp` 画在 caret 处，**主色 + 下划线**，
caret 顺延到它后面；正文一个字都不动（预编辑不是 value 的一部分，写进去就等于
"打拼音时正文被污染"）。水平滚动也把草稿宽度算进去了，长拼音不会被框边切掉。

**组字期间 placeholder 必须让位**：空输入框 + hint 太常见，如果画草稿的条件里带
`!show_placeholder`，空框里永远看不到拼音。现在 `composing` 只看
`focused && !ime_comp.empty()`，hint 反过来让位。

**上屏字符只走一条路**：库里绝不自己去插 `GCS_RESULTSTR` —— 两条路都插就是双份。

#### 三个坑（都真机复现过，别踩回去）

1. **不要用 `w & GCS_COMPSTR` 当门槛。** 微软拼音是 TSF 输入法，经 CUAS 转发过来的
   `WM_IME_COMPOSITION` 里 `w = 0x73`（只有 `GCS_COMPREADSTR/READATTR/COMPATTR/COMPCLAUSE`），
   **没有** `GCS_COMPSTR (0x08)` 这一位，可字确实在。按那一位判断 → 草稿永远是空 →
   用户看到的就是"拼音根本没有预先填充输入框"。
2. **不要信 lParam 里那个 `HIMC`。** 同一条消息里 lParam 是 `0x1b8` 这类句柄，
   `ImmGetCompositionString` 对它返回 0 字节；换成 `ImmGetContext(hwnd)` 拿到的上下文
   立刻能读到。所以先 `get_ctx(hwnd)`，拿不到才回退到 lParam。
3. **系统那个浮动组合窗要压，而且不能只靠 `SW_HIDE`。** 微软拼音会在**本进程**里
   开一个类名为 `MSCTFIME Composition` 的顶层窗口画组字串 —— 你画一份、它画一份，
   就是"框里框外各一份"。后端按 `pid + 类名`（`EnumWindows`）压它，候选窗（另一个
   类 `MSCTFIME UI`）不动。触发点有两条：
   - `WM_IME_COMPOSITION` / `WM_IME_STARTCOMPOSITION` 处理完之后（**必须在
     `DefWindowProc` 之后**，系统是在那里面把窗口显示出来的）；
   - 组字期间的 20 ms **线程计时器**。为什么非要计时器：帧不是一直在跑的 ——
     光标闪烁有 38% 的相位不请求重绘，循环会阻塞在 `WaitMessage` 里，这时输入法
     再把窗口亮出来就没人压了（多行 textarea 上实测会漏）。
     另外线程计时器的 id 是**系统分配**的，`SetTimer(nullptr, 0xA1, …)` 返回的
     不是 `0xA1`（本机是 0x63CD），`WM_TIMER` 的 `wParam` 也是那个值 —— 存返回值去比，
     拿常量比则一次都命中不了。
4. **`SW_HIDE` 会闪。** `SW_HIDE` 只是改状态，输入法每次更新组字都会再 `ShowWindow`
   一次 —— 我们 20 ms 后才压回去，于是"亮一下、藏起来、再亮一下"。实测（8 ms 一帧的
   抓图对比）：**空闲**时它是干净的（240 帧零变化），**每个按键**都会在框下露出一次
   `11x15` 的小白块；连打就是用户报的"一直闪烁"。
   做法是把它变成**全透明的分层窗**：`WS_EX_LAYERED | WS_EX_TRANSPARENT |
   WS_EX_NOACTIVATE` + `SetLayeredWindowAttributes(alpha = 0)`。之后窗口**照旧是
   可见的**（输入法不会再来抢显示，也就没有拉锯），只是画不出任何像素；对不可见的
   窗口也提前上好，等它下次亮出来时已经透明。`SW_HIDE` 保留当兜底（老主题/输入法
   重置样式的情况下分层可能没生效）。

5. **候选窗挂在组合窗底下，`ImmSetCandidateWindow` 说了不算。** 这是"候选窗离输入框
   那么远"的真正原因。我们原本报的是 `(caret_x, 输入框底边)`，测量（`ime_real_check`
   抓图 + `ime_align_probe` 打三方矩形）显示：组合窗有 22px 高，正好占在框底与候选窗
   之间，候选窗顶边比框底低 **23px**。而报给 `ImmSetCandidateWindow` 的
   `(caret, 底边+4)` 被输入法完全忽略。
   解法：让组合窗整**上移它自己的高度**，把它的**底边**落在框底上 ——
   `cf.ptCurrentPos.y = bottom - ghost_h`（`ghost_h` 从组合窗 `GetWindowRect` 实时量，
   存在 `Win32State::ime_ghost_h`）。组合窗已经被涂成全透明，挪上去没有任何视觉副作用，
   候选窗则跟着贴到框底（实测间距 23px → 约 4px）。候选窗请求位置顺手从 `+4` 收到 `+2`。

什么时候**不**压：没有任何输入框接管 IME 时（`Ui::Impl::ime_owner` 为 false，
它由 `ime_pos` 回调每帧置位）。焦点在按钮上打拼音时，系统那份是唯一可见的反馈，
压掉等于把字吞了。

平台诚实度：**Windows 已验证**（`ime_comp_probe` 走真实消息路径，含 CUAS 那条怪消息；
`ime_real_check` 用 `SendInput` 真打拼音 + 抓屏核对过内联草稿和浮动窗；
`ime_flicker_probe` 按帧抓图对比，确认打字过程中框下不再露出小白块；
`ime_align_probe` / `ime_gallery_drive` 在真实 gallery 里量出候选窗与输入框的间距）；
X11 / macOS 后端没有组字串，`ime_comp` 恒为空，行为回到"只有系统组合窗"。

> 量"候选窗离输入框多远"时**别用坐标推**：gallery 窗口在 150% DPI 下客户区是
> 1680x1080 物理像素，而 `GetClientRect` / `ClientToScreen` 从**没开 DPI 感知**的工具
> 进程里调用拿到的是虚拟化坐标，一乘一除就能差出两百像素（这个坑踩了半小时）。
> 抓一张屏、按已知输入框矩形做锚点换算，或者直接读被查进程自己算出来的值。

> `ime_flicker_probe` / `ime_real_check` **不进 regress**：它们会抢键盘、切中文输入法、
> 还要人看着屏幕，只能手动跑（`tools/cl_one.py` 编译，命令行见文件头）。

### 剪贴板：谁来绑回调

`Ctrl+C / X / V` 不直接调 Win32，而是走 `Ui::Impl` 上两个 `std::function`：

| 回调 | 签名 | 作用 |
|---|---|---|
| `set_clipboard` | `void(const std::string&)` | 把选中的 UTF-8 文本写进系统剪贴板 |
| `get_clipboard` | `std::string()` | 读回系统剪贴板（UTF-8） |

用 `Window` 时**不用管** —— 构造函数里已经绑好了（Windows 写 `CF_UNICODETEXT`，
读时先试 `CF_UNICODETEXT`，没有再退到 `CF_TEXT` 并转 UTF-8；剪贴板被别的进程
占着时重试几次，不会"时灵时不灵"）。只持有 `Ui` 的场合（离屏渲染、单元测试、
把组件画进自己的窗口）才需要自己绑：

```cpp
ui.impl->set_clipboard = [](const std::string& s) { my_clipboard = s; };
ui.impl->get_clipboard = [] { return my_clipboard; };
```

**没绑也不许崩**（以前就是这里漏了判空：调一个空的 `std::function` 抛
`std::bad_function_call`，没人接，`std::terminate` —— 按一下 `Ctrl+C` 程序直接
退出）。降级行为是有意设计的：

| 情况 | 行为 |
|---|---|
| `set_clipboard` 未绑 | `Ctrl+C` 不动作；`Ctrl+X` **不删内容**（剪了却没地方放 = 丢数据） |
| `get_clipboard` 未绑 | `Ctrl+V` 不动作 |
| 无选区按 `Ctrl+C` | 不碰剪贴板（不会把用户刚复制的东西清空） |
| 剪贴板为空按 `Ctrl+V` | 不删选区（先删后插空 = 白删） |

平台诚实度：**Windows 已验证**；X11 后端只声明剪贴板所有权、读取返回空串；
macOS 后端是空实现。跨平台生产使用前请自行补全。

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

## 8. `tabs` —— 页签（下划线式）

```cpp
bool tabs(const std::vector<std::string>& labels, int* index, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 选中项变了 |
| 容器 | **不铺底色**，只画一条 1px 的底边线（`st.border_color` → `theme.border`） |
| 选中标记 | 底边上方一条 **2px** 的高亮横线，`st.fg` → `theme.primary` |
| 选中文字 | `foreground` / 字重 **600** |
| 未选文字 | `muted_foreground` / 字重 500 |
| 悬停 | 一层 9% 的前景色（再重就变成胶囊了） |

那条横线是**一条**会滑动的线，不是每项各自淡入淡出 —— 后者会出现「两条线同时在」
的中间帧。

```cpp
ui.tabs({"概览", "按钮", "表单"}, &tab, col.next(38.0f));
```

各项**等分**容器宽度（`box.w / labels.size()`），不支持自定义每项宽度。
高度给 38 比较合适：横线占 2px，其余是点击区。

---

## 9. `segmented` —— 分段控件（胶囊式）

```cpp
bool segmented(const std::vector<std::string>& items, int* index, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 容器 | `st.bg` → `theme.muted`，圆角 `theme.radius` |
| 内边距 | **3px**（四周） |
| 选中项 | 浮起的一块圆角块：`st.border_color` → `theme.background`（深色主题下自亮 7%） |

`segmented` 走的是 `tabs` 的 **Secondary 变体**（凹槽 + 滑块），所以两者看起来不再是
同一个东西：

```cpp
ui.tabs({"概览", "按钮", "表单"}, &page,  col.next(38.0f));   // 下划线
ui.segmented({"日", "周", "月"},   &range, col.next(32.0f));  // 胶囊
```

语义区别：`tabs` 切的是「页面」（内容完全不同），`segmented` 选的是「同一个值的不同
表达」（日 / 周 / 月）。**0.3.3 之前两者是同一个实现**，画出来一模一样 —— 那次是文档
写了「两套视觉」、代码里根本没做。

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

---

## 16. 0.3.0 新增的表单控件

这一节的九个组件都是照着 shadcn/ui 补的。它们和前面那些的差别不在机制 ——
`interact()` / `animate_value()` / 焦点环 / 点击补排帧全都一样，只是语义更专用。
共用的事实先说在这里，后面不再重复：

- 尺寸全从 `box` 推，**没有一个组件有「默认宽度」**；高度靠调用方给（`field` 例外，它有
  `field_height()`）；
- 圆角默认 `theme.radius_sm`，字号默认 `theme.font_size`，两者都能用 `Style` 覆盖；
- 都吃 `Style().disabled()`，禁用态靠把前景 alpha 乘 0.45 表示，不另配颜色；
- 返回 `true` 一律表示**持久值变了**（不是"被点了一下"）。

### 16.1 `toggle_button` —— 按下态按钮

```cpp
bool bold = true;
ui.toggle_button("粗体", &bold, {x, y, 88, 32});
```

**它不是开关**。开关是 `toggle()`（shadcn 的 Switch）。这个是按下会"陷进去"的按钮
（shadcn 的 Toggle），典型用途是工具栏上的 B / I / U。

| 项 | 行为 |
|---|---|
| 点击 | `*value = !*value`，返回 true |
| 选中底 | `theme.accent`，**按 `t_on` 推 alpha** |
| 悬停底 | 未选中时才浮出来，强度只有 55% |
| 前景 | 选中用 `accent_foreground`，否则 `foreground` |
| 内边距 | `st.pad_x`，否则 10px |
| 动画 | 选中 22 / 悬停 18（`animate_value` 速率） |

> 它的底色是从"完全透明"淡进去的，所以实现里写的是
> `th.accent.with_alpha(255 * t_on)` 而不是 `Color::none().mix(accent, t)`。
> 后者会把 RGB 和 alpha 一起从黑拉上来，中间帧比两端都暗 —— 就是 0.4.1 修的那个
> "hover 闪一下深灰"。自己写同类控件时照抄前一种写法。

### 16.2 `toggle_group` —— 单选组

```cpp
int seg = 1;                          // -1 = 一个都不选
ui.toggle_group({"日", "周", "月"}, &seg, {x, y, 240, 32});
```

- 各项**等分 `box.w`**，整组共享一个外框（组内不各画边框），底是 `theme.background`；
- **再点已选中的项 = 取消选择**（`*index` 回到 -1）。这是全库唯一一处刻意偏离 shadcn
  的行为 —— shadcn 的 `type="single"` 不允许取消，桌面端这么做更顺手；
- 初始化就写 `-1` 才表示"没选"，写 0 是"选了第一项"。

### 16.3 `toggle_group_multi` —— 多选组

```cpp
std::uint32_t mask = 0b101;           // 第 i 位对应第 i 项
ui.toggle_group_multi({"粗", "斜", "下划线"}, &mask, {x, y, 300, 32});
```

`mask` 的第 i 位就是第 i 项，点击即 `^=` 翻转。**最多 32 项**（`uint32_t` 的位宽），
超出部分直接不画。用位掩码而不是 `std::vector<bool>` 是为了让调用方把它直接塞进自己的
状态结构体里，不用管生命周期。

### 16.4 `button_group` —— 连成一片的按钮

```cpp
int hit = -1;
if (ui.button_group({"复制", "粘贴", "剪切"}, &hit, {x, y, 260, 32}))
    do_action(hit);
```

和 `toggle_group` 长得像，差别是**没有持久选中态**：每个按钮都是一次性动作，
点了只写 `*clicked_index` 并返回 true。

底是 `theme.secondary`，而悬停高亮用的是 `theme.background` —— 不是 `accent`。原因很实在：
浅色主题里 `secondary` 和 `accent` 是同一个色，用 `accent` 做悬停会完全看不出来。

### 16.5 `input_group` —— 带前后缀的输入框

```cpp
std::string amount = "128";
ui.input_group(&amount, "¥", "元", {x, y, 200, 36});
```

实现方式是「自己画外框 + 中间那个 `input()` 传透明底、0 边框」，所以 `input()` 的选区、
拖选、撤销栈、IME 全都还在，只是它不再画第二层框。三个细节：

- 前缀与输入内容之间留 **10px**，不是 4px。写 4 的时候 `¥` 和 `128` 会挤成 `¥128`；
- **点前后缀区域也会聚焦输入框**。看着是一个框，点在"元"上却没反应，是最容易被当成
  "控件坏了"的那种细节。抢焦点必须等 `input()` 跑完再抢 —— 它内部遇到"按下了但不在
  自己框内"会主动丢焦点，先抢会被它丢回去（晚一帧生效，肉眼无感）；
- **聚焦时外框变 `theme.ring`**。外框既然是自己画的，`input()` 那套聚焦变色就得自己补，
  否则两个框上下排着时，点进去根本看不出光标落在哪一个。画法要和 `input()` 对齐
  （边框换 `ring` 并加粗 0.4，再叠一层 `paint_focus_ring` 的 55% 外环）—— 只画一根深色
  描边的话，同一个页面上 `input()` 和 `input_group` 的焦点态轻重不一，看着像后者报错了。

> **坑：`Color(0,0,0,0)` 表达不了「别画底」。** `Color::is_none()` 只判 `a == 0`，
> 所以"显式要透明"和"根本没传"在 `Style` 里长得一模一样。`input()` 拿到 a=0 会回退成
> `theme.background`，在传进去的矩形上**按整高**铺一层白 —— 如果外壳是调用方先画的，
> 这层白就把外框的**上下两条边从中间吃掉**，只剩前后缀那两小段。表现是"两个上下排列的
> 输入框中间那条横线不见了"（0.4.3 修的）。
>
> 现在 `input()` 里有一条约定：**`border_width == 0` 且没给 `bg` = "外壳我自己画"**，
> 那时它不铺内层底。自己写同类组合控件时按这个约定传，或者干脆**把外框放到 `input()`
> 之后画**（谁最后画谁在上面）。`tools/probe/widget_probe.cpp` 的 `[4] Input Group`
> 用两条像素断言钉住「边框中线处必须比框内更暗」。

### 16.6 `input_otp` —— 分格验证码

```cpp
std::string code;
if (ui.input_otp(&code, 6, {x, y, 320, 48})) verify(code);
```

| 项 | 行为 |
|---|---|
| 格子数 | `length`，**上限 12**（超出截断） |
| 格子尺寸 | `min(box.h, (box.w - 8×间隙) / length)`，整排水平居中 |
| 输入 | **只收数字** —— 粘 "12a3" 也只吃 1、2、3 |
| 退格 | 光标退一格并删掉该格 |
| ← / → | 移动当前格 |
| 当前格 | 描边换成 `theme.ring`、1.6px；空格子显示**静止**竖线（闪烁太费帧） |

它**不复用 `input()`**：那个输入框带选区、拖选、双击选词、IME、撤销栈一整套，
塞进一个 40×40 的格子既不合适也画不进去。

### 16.7 `field` —— 表单一行

```cpp
const float h = ui.field_height("邮箱", "不会公开", "", 36.0f);
ui.field("邮箱", "不会公开", "", {x, y, w, h}, [&](const aino::Rect& r) {
    ui.input(&mail, r);
});
```

结构是「标签 → 控件槽 → 说明/错误」，槽由回调画（`field` 只给矩形，不猜你要放什么）。

| 段 | 高度 | 字号 / 字重 |
|---|---|---|
| 标签（`label` 非空时） | `line_height(fs + 1)` + 6 | `fs + 1` / 500 |
| 控件槽 | 剩下的全给它（至少 1px） | 由回调决定 |
| 说明或错误（二者有其一） | `line_height(fs - 1)` + 6 | `fs - 1` / 400 |

**`error` 优先于 `hint`**：两个都传只画 error，色用 `theme.destructive`；hint 用
`theme.muted_foreground`。`field_height()` 与 `field()` 用的是同一组数字，改一处必须改另一处
（源码里就挨着放，就是为了这个）。

### 16.8 `combobox` —— 可搜索的下拉

```cpp
int idx = 0;
ui.combobox({"C++", "Rust", "Go"}, &idx, {x, y, 220, 36});
```

输入即过滤、↑↓ 移动、回车确认、Esc 收起。

> **它里面的搜索框不是 `input()`。** `input()` 自己会拿一个焦点，两个控件抢焦点会互相
> 把对方踢掉。所以那个框只是画出来的样子，字符处理全在 `combobox` 内部。

### 16.9 `calendar` —— 月历

```cpp
int y = 2026, m = 10, d = 22;
if (ui.calendar(&y, &m, &d, {x, y, 260, 240})) reload(y, m, d);
```

- **点日期、点翻月箭头都算"变了"**，返回 true 时三个值已经写好；
- 月分越界自动进位（12 月 → 次年 1 月），选中的日会夹到新月份的天数内；
- 网格固定 **6 行 × 7 列**，周日开头；表头高度 `max(fs + 26, 42)`、星期行
  `max(fs + 12, 26)`，剩下全给格子，所以 `box.h` 给多少都能排得下；
- 日期运算全部是**纯整数**（`detail::weekday_of` 走 Sakamoto 求星期，`days_in_month` 自己算），
  **不碰 `<ctime>`**：`mktime` / `localtime` 带本地时区与夏令时，同一份代码在 UTC 的 CI runner
  和东八区的机器上会差一天，而那是最难查的一类「只在我的机器上错」。
