# 06 · 交互类组件

本篇介绍按钮、输入与选择组件。它们共享同一套交互机制，先读完第 0 节再读具体组件。

---

## 0. 通用交互机制

### 0.1 组件 ID 与位置

组件 ID 由种类、半逻辑像素量化的位置和额外键生成。文本按钮还会使用文本键。
宽高不参与主位置键。滚动作用域将位置还原为内容坐标，因此正常滚动不会创建新的组件状态。
容器整体移动、重排、修改按钮文本可能改变 ID；同类控件不要在同一内容位置使用相同键。
应用通过 bool、字符串等外部值持有数据，组件内部仅缓存焦点、悬停和过渡等交互状态。

### 0.2 hover / press 是插值出来的

```cpp
t_hover = animate_value(id, 目标 0/1, 速率 18)   // 多数组件
t_press = animate_value(id, 目标 0/1, 速率 34)   // 更快
```

速率单位是「每秒趋近的指数系数」，18 ≈ 55ms 到 63%，34 ≈ 30ms。
所以按下比悬停更"跟手"。

这些插值**只在值变化时请求重绘**，值稳定后就停了 —— 不会一直吃 CPU。

透明底 Ghost 按钮的 `state_fill()` 只推进填充 alpha，保持目标 RGB，避免从透明黑
插值造成中间帧变暗。Outline 使用中性填充和细描边。`hover_fade_probe` 检查淡入亮度。

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
- 按钮表面与描边 alpha 为原值的 **55%**，标签为 **45%**；其它控件按自身规则降低透明度
- 返回值恒为 `false`

### 0.5 返回值

| 语义 | 组件 |
|---|---|
| 本帧被点击（按下并在框内松开） | `button` `icon_button` `nav_item` |
| 值发生了变化 | `checkbox` `toggle` `slider` `radio_group` `stepper` `rating` `textarea` `input` |
| 选中索引变了 | `tabs` `segmented` |
| 提交了选择（重复选择同一项也返回 true） | `select` |

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

所以 `interact()` 一旦返回 `pressed` / `clicked`，就由控件内部排一次后续帧：
点击这种事件一帧排两次不浪费，落帧的状态才是一致的。空闲时没有任何点击，
不影响"静止 0 帧 / 0.00% CPU"。


「值变化」类组件**每次变化都返回 true**，所以拖动 slider 时会连续返回 true ——
这是设计如此，不是重复触发。要「松手才处理」就自己判断 `!ui.input().down[0]`。

### 0.8 按下状态的释放与取消

`active` 只表示鼠标仍在按住的控件。刷新列表、切页或改变布局可能使原控件消失，
所以 `Ui::end_frame()` 在普通控件和延迟弹出层都处理完输入后，只要左键已松开，
就统一释放残留的 `active`。鼠标仍按住时保留它，保证跨帧拖动正常。

Windows 在失焦、捕获被其他窗口拿走或收到 `WM_CANCELMODE` 时取消按下状态，
同时丢弃未消费的鼠标 press / release，避免取消操作触发点击。
正常松手引发的 `WM_CAPTURECHANGED` 会保留那次 release，按钮仍能按预期点击。
X11 订阅焦点事件，在 `FocusOut` 时清理按键和鼠标状态。

`tools/probe/capture_probe.cpp` 覆盖控件消失、有无 release 的取消、正常拖动、
延迟控件的松手，以及 Windows 原生捕获消息和 X11 焦点事件订阅。
它已加入默认 `python tools/regress.py` 和三平台 CI；macOS 执行共享状态机回归。

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
| 圆角 | `theme.radius`（6） |
| 字号 | `theme.font_size`（13） |
| 字重 | `st.font_weight` → 否则 500 |
| 光标 | `Arrow`；Link 变体使用 `Hand` |

**生效的属性**：全部 7 个变体 + `bg` `fg` `border_color` `border_width` `radius`
`font_size` `font_weight` `italic` `align` `valign` `disabled` `press_scale`

### 按压缩放

`press_scale` 默认 1.0（不缩放）。想要「按下去小一圈」：

```cpp
ui.button("按我看看", box, aino::Style().primary().scale_on_press(0.97f));
```

### 建议尺寸

高度用 `theme.control_h`（24）。宽度按内容：
`ui.text_width(text, 13.0f, 500) + 32.0f`；想占满一行就直接把 `box.w` 给足
（`button` 天生按传入的 `box` 铺满，不会只包住文字）。

```cpp
if (ui.button("保存", col.next(24.0f), aino::Style().primary())) save();
if (ui.button("普通操作", col.next(24.0f), aino::Style().standard())) run();
if (ui.button("取消", col.next(24.0f), aino::Style().outline())) close();
if (ui.button("删除", col.next(24.0f), aino::Style().destructive())) remove();
```

> 返回值只在「按下 + 在同一次按下期间于框内松开」时为 true。
> 按住后把鼠标拖出框再松开**不会**触发 —— 这样才符合按钮的常规预期。

---

## 2. `icon_button` 与系统图标按钮

`icon_button(glyph, box, st)` 保留 UTF-8 字符接口，默认 Ghost，按调用方给的盒子绘制。
它不会修正盒子的宽高：要正方形应传 24×24、20×20 等正方形区域。

系统图标按钮使用普通按钮处理交互，再绘制平台符号：

```cpp
const aino::Rect icon_box(24, 24, 24, 24);
if (ui.button("", icon_box, aino::Style().ghost())) close_panel();
ui.system_icon(aino::SystemIcon::Close, icon_box.inset(4), ui.theme().label);
```

`system_icon` 只负责绘制，图标区域应包含在按钮热区内；禁用时也应相应降低图标透明度。

---

## 3. `nav_item` —— 侧栏条目

```cpp
if (ui.nav_item("设置", selected == 1, row)) selected = 1;
```

返回本帧是否点击。默认标签字号 13、字重 500，水平内边距 8，选中背景为约 23% 强调色、
圆角 8；悬停使用淡中性填充。选中状态由调用方传入，组件不负责切页或保存路由。
建议行高 28，图标可另外使用 `system_icon` 绘制，并为文字预留位置。

---

## 4. `input` —— 单行输入

```cpp
bool input(std::string* value, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 文本发生了变化；仅移动光标或选区不返回 true |
| 底色 | `st.bg` → 否则浅色 `window_bg`，深色白色 alpha 20 |
| 描边 | `st.border_color` → 否则 `separator`，1px；顶部内阴影，聚焦时叠外焦点环 |
| 圆角 | `theme.radius`（6） |
| `pad_x` | **8** |
| 字号 | `theme.font_size` |
| 字重 | 400（固定） |
| 占位符色 | `label3`（25%） |
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

### 输入法

Windows 输入框将组字串绘制在光标处，带下划线；组字期间隐藏占位文字，
未提交内容不提前写入应用字符串。候选窗位置随光标、DPI 与滚动更新。

`imm32.dll` 运行时加载，无需额外链接。输入框接管内联组字时，后端抑制重复的系统组合窗；
未接管输入时保留系统反馈。相关断言为 `ime_probe`、`ime_comp_probe`，
真实输入法定位与闪烁检查为 `ime_real_check`、`ime_align_probe`、`ime_flicker_probe`。

真实输入法探针会操作键盘与输入法，需要人工运行。定位时使用物理截图或被测进程自身坐标，
不要混用未开启 DPI 感知的外部进程坐标。X11/macOS 当前没有内联组字串实现，
应在目标环境验证系统输入法行为。

### 剪贴板

`Window` 自动绑定剪贴板回调。Windows 优先读写 Unicode 文本，读取旧式文本时转为 UTF-8。
离屏 `Ui` 或自定义窗口需要自行绑定 `impl->set_clipboard/get_clipboard`；
这些内部回调只在实现定义可见的编译单元中可直接访问。

| 情况 | 行为 |
|---|---|
| 未绑定写回调 | 复制不动作；剪切不删除文本 |
| 未绑定读回调 | 粘贴不动作 |
| 无选区复制 | 保留现有剪贴板 |
| 剪贴板为空时粘贴 | 保留现有选区文本 |

Windows 路径已有探针覆盖；X11 当前只声明剪贴板所有权、读取返回空串，macOS 为占位实现。
跨平台应用接入前需要补齐，不能把 Windows 的验证结果外推。

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

### 快捷键修饰状态

`Input::ctrl()/shift()/alt()` 表示当前按住状态；快捷键应使用
`ctrl_of(k)/shift_of(k)/alt_of(k)` 读取按键按下时锁存的修饰位。
同一批事件可能已包含修饰键松开消息，直接读取当前状态会漏掉快速组合键。

Windows 后端在快捷键后短暂抑制字符输入，防止输入法或布局将快捷键字符写入文本。

### 光标闪烁 = 常驻动画

聚焦时由控件内部请求光标闪烁帧。**未聚焦的 input 静置是 0 帧、0% CPU**，
只有一个聚焦的输入框才会持续要帧（实测约 1~2%）。

```cpp
ui.input(&email, col.next(24.0f), aino::Style().hint("you@example.com"));
```

> `box` 的宽度决定可见区域，文本超出后**自动横向滚动**（跟随光标）。
> 高度建议 24（`theme.control_h`）。

### 中文输入法（IME）

Windows 输入框支持内联组合串、下划线与候选窗定位，组合过程不提前写入应用字符串。
候选窗坐标随光标、DPI 和滚动位置更新；`imm32.dll` 使用运行时加载，不增加链接参数。
Linux/macOS 的输入法行为尚未真机核对，不能从 Windows 测试结果推断。
相关断言见 `tools/probe/ime_probe.cpp` 和编辑探针。

---

## 5. `checkbox` —— 复选框

```cpp
bool checkbox(bool* value, const std::string& label, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 勾选态变了 |
| 方框尺寸 | 固定 14×14，垂直居中；默认圆角 3.5 |
| 方框底（未选） | 浅色 `#F6F6F6` / 深色 `#3C3C3C`，alpha 214，配细描边与内阴影 |
| 方框底（选中） | `theme.tint` |
| 对勾色 | `theme.tint_foreground` |
| 标签字号 | `theme.font_size` |
| 焦点环 | `theme.ring` |

**生效的属性**：`fg` `font_size` `radius` `disabled`

```cpp
ui.checkbox(&remember, "记住这台设备", col.next(22.0f));
```

点**标签也算点方框**（整个 `box` 都是热区）。建议高度 22，方框会自动居中。

---

## 6. `toggle` —— 开关

```cpp
ui.toggle(&notifications, {x, y, 44, 20});
```

值变化返回 true，轨道与滑块过渡在初始化时直接落到当前状态。
组件选取能同时放入 `box.w/box.h` 的档位并居中，盒子过小时等比缩小。

| 档位 | Mini | Small | Medium | Large | XL |
|---|---|---|---|---|---|
| 轨道宽高 | 36×16 | 44×20 | 54×24 | 64×28 | 80×36 |

打开使用 `theme.tint`，关闭使用中性填充，滑块为白色。
默认滚动不会重启动画，只有值变化、按下或悬停等交互会触发相应过渡。
`Style().off()` 禁用交互。

---

## 7. `slider` —— 滑杆

```cpp
ui.slider(&volume, 0, 1, {x, y, 280, 24});
```

拖动时将鼠标位置线性映射到最小值与最大值之间，值变化返回 true。
输入与绘制使用同一段轨道：两端各留把手半宽 10，点击可见端点可达到范围两端。
轨道高 4，`border_color` 覆盖轨道色，`bg` 覆盖强调色填充。
白色胶囊把手默认 20×16，按下过渡到 25×20；悬停不会改变把手几何。
建议盒高 24，为按下形态和阴影留空间。`off()` 禁用交互。

---

## 8. `tabs` —— 页签

```cpp
ui.tabs({"概览", "按钮", "表单"}, &tab, col.next(28));
```

普通页签按文字宽度加 36 计算项宽，字重均为 500。选中项使用淡中性圆角底与主标签色，
未选项使用次要标签色，无下划线。普通重载不会自动折行，应给足所有标签需要的宽度；
绘制与点击都限制在传入的 `box` 内。
返回选中索引是否变化。

可关闭的会话页签使用同一控件的扩展重载，SSH 示例复用此接口：

```cpp
int closed = -1;
aino::TabOptions options;
options.close_index = &closed;
options.max_width = 200;
ui.tabs(labels, &tab, box, options, aino::Style());
if (closed >= 0) {
    labels.erase(labels.begin() + closed);
    if (closed < tab) --tab;
    tab = labels.empty() ? -1 : std::min(tab, int(labels.size()) - 1);
}
```

`close_index` 每次调用先写入 `-1`，点击关闭时返回对应索引，控件不移除应用数据，
也不会因为关闭未选项而切换选中项。`tabs()` 的布尔返回值仍只表示选择变化。
状态色向量可短于标签列表，缺少对应颜色或颜色为 `Color::none()` 时不显示标记。
两个指针只在本次调用期间借用，应用负责其生命周期。
启用关闭、状态标记或宽度限制后，普通页签溢出时显示左右翻页按钮；
程序更改选中索引后自动将该项移入视口。
`max_width = 0` 表示自然宽度，正值限制单项宽度（最少 70）；超长标签文字裁剪显示。
`Style().off()` 同时禁用选择、关闭与翻页。关闭及翻页功能不用于 `segmented` 胶囊变体。

## 9. `segmented` —— 分段选择

```cpp
ui.segmented({"日", "周", "月"}, &range, col.next(24));
```

默认通过 tabs 的 Secondary 变体实现：各段等宽，中性凹槽底，内缩的强调色胶囊表示选中项。
选中项使用对比文字色。显式传其它变体会覆盖这一默认选择。
`tabs` 通常切换内容，`segmented` 通常选择一个值的不同范围或模式。

---

## 10. `select` —— 下拉选择

```cpp
bool select(const std::vector<std::string>& items, int* index, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 本帧提交了选择，重复选择同一项也返回 true |
| 底色 | `theme.fill2`（8% 叠加填充，NSPopUpButton） |
| 描边 | **无**（8% 面自己分层）；`st.border_color` 才画 |
| 圆角 | `theme.radius`（6） |
| `pad_x` | **12**（硬编码，= `Popup/Inset - Left` Medium） |
| 标记 | 右侧**上下双三角**（不是单个向下箭头） |
| 弹层底 | `theme.popover_bg`，行高 `max(box.h, 24)` |
| 弹层阴影 | 菜单层级的双层投影 |
| 悬停项底色 | 强调色；选中项显示勾选符号 |

**生效的属性**：`bg` `fg` `border_color` `border_width` `font_size` `disabled`

```cpp
ui.select(items, &idx, col.next(24.0f));
```

### 行为细节

- 点主体 → 展开/收起；展开状态存在 `impl->flags[id]` 里
- **点外部自动收起**（判据：本帧有按下且不在弹层范围内）
- 选中项后立即收起并写回 `*index`
- 弹层宽度与 `box.w` 相同，优先向下展开，底部空间不足时移到触发器上方
- `*index` 越界时显示为空（不崩）

展开后的悬停过渡结束即停止请求帧；鼠标移动、点击和按键事件驱动高亮与选择。
静置展开的列表不会持续重绘。

---

## 11. `radio_group` —— 单选组

```cpp
bool radio_group(const std::vector<std::string>& items, int* index, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 选中项变了 |
| 布局 | **纵向等分 `box.h`**，每行一个圆环 + 标签 |
| 圆环 | `max(16, fs + 3)`，默认 16×16；淡底、细描边与内阴影 |
| 圆环色（选中） | `theme.primary` 实心 |
| 中心点 | 直径 **6**（不是按半径比例长到 8），`t*(2-t)` ease-out 弹出 |
| 标签 | 13 Medium 85%，**选中不换字重**（换字重会让整行宽度跳一下） |

**生效的属性**：`fg` `font_size` `disabled`

```cpp
ui.radio_group({"选项一", "选项二", "选项三"}, &choice, col.next(100.0f));
```

高度决定每行的间距：`box.h / items.size()` 就是行高。3 个选项给 100 ≈ 每行 33。

---

## 12. `stepper` —— 数值步进器

```cpp
ui.stepper(&count, 1, 0, 99, {x, y, 120, 24});
```

左侧为数值，右侧竖条分上下两个系统箭头按钮，上增下减。
按钮条宽 `clamp(box.h * .83, 16, 20)`，圆角默认 `theme.radius`。
值夹取至范围，变化返回 true；整数不显示小数，其它值显示两位小数。
`bg/fg/border_color/border_width/radius/font_size/disabled` 可覆盖对应属性。

---

## 13. `textarea` —— 多行文本

```cpp
bool textarea(std::string* value, const Rect& box, const Style& st = Style());
```

| 项 | 默认值 |
|---|---|
| 返回 | `true` = 文本变了，光标或选区移动不返回 true |
| 底色 | 浅色 `window_bg`，深色白色 alpha 20；`st.bg` 可覆盖 |
| 描边 | `theme.input` 1px |
| 圆角 | `theme.radius`（6） |
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
| 光标 | `Arrow` |

**生效的属性**：`bg`（实星色）`disabled`

```cpp
ui.rating(&stars, 5, col.next(28.0f));
```

### 交互

- 点第 i 颗星 → `*value = i + 1`
- 点**当前已选的最后一颗**→ 清零（`*value = 0`）
- 悬停时给星星勾一圈细边，提示"可点"

星形由多边形光栅化绘制，悬停时增加轮廓；此组件当前未使用系统图标通道。

---

## 15. 组合示例：一个完整的表单

```cpp
void composeForm(aino::Ui& ui, AppState& s, const aino::Rect& area) {
    ui.panel(area);
    aino::Stack c = ui.vbox(area, 12.0f, 20.0f);

    ui.subheading("账号", c.next(26.0f), aino::Style().left());
    ui.input(&s.email,    c.next(24.0f), aino::Style().hint("you@example.com"));
    ui.input(&s.password, c.next(24.0f), aino::Style().hint("密码").secret());
    ui.muted_text("至少 8 位，含大小写与数字", c.next(18.0f), aino::Style().font(12.0f).left());

    ui.separator(c.next(1.0f));
    ui.subheading("偏好", c.next(26.0f), aino::Style().left());
    ui.checkbox(&s.remember, "记住这台设备", c.next(22.0f));
    ui.checkbox(&s.autosave, "自动保存",     c.next(22.0f));

    aino::Rect row = c.next(26.0f);          // 标签 + 开关同一行
    ui.label("桌面通知", row, aino::Style().left());
    ui.toggle(&s.notify, {row.right() - 44.0f, row.y, 44.0f, 20.0f});

    ui.segmented({"日", "周", "月"}, &s.range, c.next(34.0f));
    ui.slider(&s.zoom, 0.5f, 2.0f, c.next(20.0f));

    if (ui.button("保存", c.fill(), aino::Style().primary())) s.save();
}
```

---

## 16. 分组按钮与复合表单

这些组件复用前述交互、动画与焦点机制。共同约定：

- 尺寸全从 `box` 推，**没有一个组件有「默认宽度」**；高度靠调用方给（`field` 例外，它有
  `field_height()`）；
- 字号默认 `theme.font_size`；圆角按组件使用 `theme.radius_sm`、`theme.radius` 或胶囊规则，可用 `Style` 覆盖；
- 都吃 `Style().off()`，禁用态降低透明度；
- 多数组件返回值表示值变化；`button_group` 返回本帧点击，并写入点击索引。

### 16.1 `toggle_button` —— 按下态按钮

```cpp
bool bold = true;
ui.toggle_button("粗体", &bold, {x, y, 88, 32});
```

`toggle_button()` 保持按钮的按下状态，适合工具栏 B / I / U；`toggle()` 用于开关。

| 项 | 行为 |
|---|---|
| 点击 | `*value = !*value`，返回 true |
| 选中底 | `tint`，alpha 为 `56 * t_on`（约 22%） |
| 悬停底 | 无选中过渡时使用 `fill` 随悬停淡入；静止未选中时使用 `st.bg`，默认透明 |
| 前景 | `st.fg` → 否则 `foreground`，两态默认字重均为 500 |
| 内边距 | `st.pad_x`，否则 10px |
| 动画 | 选中 22 / 悬停 18（`animate_value` 速率） |

### 16.2 `toggle_group` —— 单选组

```cpp
int seg = 1;                          // -1 = 一个都不选
ui.toggle_group({"日", "周", "月"}, &seg, {x, y, 240, 32});
```

- 各项等分 `box.w`，共用 `theme.track_well()` 凹槽轨道，无外框，默认胶囊圆角；
- 选中面浅色为 `#E6E6E6`、深色为 `#3A3A3A`；选中标签用 `label`，其余用 `label2`；
- 再点已选中的项会取消选择（`*index` 回到 -1）；
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

底色为 `theme.track_well()`（可用 `st.bg` 覆盖），无外框；悬停面用 `fill4` 的 70% alpha。
所有按钮标签使用主前景色。它与单选/多选组共用单元格绘制，但不保留选中状态。

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
int year = 2026, month = 11, day = 11;
if (ui.calendar(&year, &month, &day, {x, y, 260, 240})) reload(year, month, day);
```

- **点日期、点翻月箭头都算"变了"**，返回 true 时三个值已经写好；
- 月分越界自动进位（12 月 → 次年 1 月），选中的日会夹到新月份的天数内；
- 网格固定 **6 行 × 7 列**，周日开头；表头高度 `max(fs + 26, 42)`、星期行
  `max(fs + 12, 26)`，剩下全给格子，所以 `box.h` 给多少都能排得下；
- 日期运算全部是**纯整数**（`detail::weekday_of` 走 Sakamoto 求星期，`days_in_month` 自己算），
  **不碰 `<ctime>`**：`mktime` / `localtime` 带本地时区与夏令时，同一份代码在 UTC 的 CI runner
  和东八区的机器上会差一天，而那是最难查的一类「只在我的机器上错」。

## 17. 搜索框与颜色池

### `search_field`

```cpp
ui.search_field(&query, {24, 24, 240, 24}, aino::Style().hint("搜索"));
```

复用单行输入的编辑、选区、撤销、密码掩码与 IME 逻辑，默认圆角为半高、水平内边距 26。
左侧显示系统搜索符号，文本非空时右侧显示清除按钮。返回文本是否变化，点击清除也返回 true。
禁用状态同时降低符号透明度并禁止输入和清除。

### `color_well`

```cpp
if (ui.color_well(ui.theme().purple, {24, 64, 48, 24})) {
    auto theme = ui.theme();
    theme.set_tint(theme.purple);
    win.set_theme(theme);
}
```

绘制传入颜色与细边，返回本帧是否点击；默认圆角 `theme.radius`，支持 `radius/disabled`。
它不保存选择，也不打开系统颜色面板。主题色切换由调用方使用 `Theme::set_tint()` 完成。
