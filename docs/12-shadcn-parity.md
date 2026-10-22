# 与 shadcn/ui 的对照

这个库的视觉语言照着 shadcn/ui 做（`Theme` 的 token 名、`Style` 的 variant 名、
控件高度 36px、圆角 8px 都是）。但 shadcn 是 React 组件库，它的一部分组件在
立即模式的 C++ 库里没有对应物 —— 有些是**不该有**，有些是**还没做**。

这一页按 2026-10 的官方组件清单（64 项）逐条对一遍，说清楚三种情况：

- **一对一** —— 名字和行为都能对上，可以直接照 shadcn 的用法写；
- **名字不同** —— 功能有，但名字被别的东西占了（下面单独解释）；
- **没有** —— 没做，或者在这个库里不成立。

---

## 一、名字被占用的三个

这三个最容易踩，写在最前面：

| shadcn | aino | 为什么 |
|---|---|---|
| `Switch` | `ui.toggle()` | `toggle` 这个名字最早给了开关。**它一直是 Switch，不是 Toggle** |
| `Toggle` | `ui.toggle_button()` | 0.3.0 补的才是 Toggle（按下态按钮） |
| `Empty` | `ui.empty_state()` | `empty` 是 `Rect::empty()` 的成员名，全局函数重名会让人写错 |

所以看到 `ui.toggle(&flag, box)` 时，脑子里要翻译成 **Switch**：

```cpp
bool on = true;
ui.toggle(&on, Rect(x, y, 44, 26));      // ← 这是 shadcn 的 Switch
ui.toggle_button("粗体", &bold, r);       // ← 这才是 shadcn 的 Toggle
```

改名会破坏所有已有代码，所以选择「新增一个名字更明确的」而不是重命名。
`toggle()` 会一直保留，语义不变。

---

## 二、逐条对照

「状态」一列：✅ 一对一 · ⚠️ 功能有但语义/范围不同 · ➕ 0.3.0 新补 · ❌ 没有

### 表单与输入

| shadcn | aino | 状态 | 说明 |
|---|---|---|---|
| Button | `button()` | ✅ | variant 走 `Style().outline()/ghost()/secondary()/destructive()/link()` |
| Button Group | `button_group()` | ➕ | 相邻共享外框，各自独立触发（**没有**持久选中态） |
| Input | `input()` | ✅ | 带选区、拖选、撤销栈、IME 组字 |
| Input Group | `input_group()` | ➕ | 左右挂前缀/后缀；点前后缀也会聚焦输入框 |
| Input OTP | `input_otp()` | ➕ | 只收数字、退格回退、超长截断 |
| Textarea | `textarea()` | ✅ | |
| Checkbox | `checkbox()` | ✅ | |
| Radio Group | `radio_group()` | ✅ | |
| Switch | `toggle()` | ✅ | 见上面「名字被占用」 |
| Toggle | `toggle_button()` | ➕ | |
| Toggle Group | `toggle_group()` / `toggle_group_multi()` | ➕ | 单选再点可取消（**比 shadcn 多这一条**）；多选用 `uint32_t` 位掩码 |
| Select | `select()` | ✅ | 自绘下拉，不是系统下拉 |
| Native Select | `select()` | ⚠️ | 没有独立的系统原生下拉 |
| Combobox | `combobox()` | ➕ | 输入即过滤，↑↓ 选，回车确认 |
| Slider | `slider()` | ✅ | |
| Stepper | `stepper()` | ✅ | 数字增减（shadcn 没有对应物，本地扩展） |
| Rating | `rating()` | ✅ | 星级（本地扩展） |
| Calendar | `calendar()` | ➕ | 纯整数日期运算，不依赖 `<ctime>` |
| Date Picker | `calendar()` | ⚠️ | 没有「输入框 + 弹日历」的组合件；自己把两者摆一起即可 |
| Field | `field()` / `field_height()` | ➕ | 标签 + 控件槽 + 说明/错误；高度先算后排 |
| Form | — | ❌ | 依赖 react-hook-form 那套校验编排，不属于纯 UI 层 |

### 布局与容器

| shadcn | aino | 状态 | 说明 |
|---|---|---|---|
| Card | `card()` / `panel()` | ✅ | `card` 是 `panel` 的别名语义 |
| Separator | `separator()` | ✅ | 横竖自动判（看 box 的宽高比） |
| Resizable | `split_pane()` | ➕ | 一个分隔条 + 两块矩形，拖动实时改 ratio |
| Scroll Area | `scroll()` / `begin_scroll()` | ✅ | 唯一入口，滚动条自己画 |
| Sidebar | `nav_item()` | ⚠️ | 只有条目，没有 Sidebar 容器；`examples/sidebar_demo.cpp` 手搭 |
| Sheet | `drawer()` | ✅ | `side` 参数控制从哪边出来 |
| Drawer | `drawer()` | ➕ | 只做淡入，不做位移（见下文「取舍」） |
| Aspect Ratio | `Ui::aspect_rect()` | ➕ | 没有图片管线，所以它就是个算 `Rect` 的 helper（立即模式里本来也不需要容器组件） |
| Collapsible | `accordion()` | ⚠️ | 传空标题就是纯折叠面板 |
| Accordion | `accordion()` | ✅ | 每一项一个 `bool*`，多开/互斥由调用方决定 |

### 展示

| shadcn | aino | 状态 | 说明 |
|---|---|---|---|
| Avatar | `avatar()` | ✅ | 取首字，没有图片版本 |
| Badge | `badge()` | ✅ | |
| Kbd | `kbd()` | ✅ | |
| Label | `label()` | ✅ | |
| Typography | `heading()` `subheading()` `label()` `muted_text()` `code_text()` | ✅ | 拆成五个函数而不是一个 `Typography` 组件树 |
| Table | `table()` | ✅ | 等宽列 + 斑马纹 |
| Data Table | `table()` | ⚠️ | 静态表。排序/勾选/分页要自己接（用 `pagination()` + `checkbox()`） |
| Progress | `progress()` | ✅ | |
| Skeleton | `skeleton()` | ✅ | 默认静止，`.animate()` 才呼吸 |
| Spinner | `spinner()` | ✅ | 默认静止（空闲 0% CPU 的承诺），`set_anim_fps()` 才转 |
| Empty | `empty_state()` | ➕ | 虚线框 + 标题 + 描述 |
| Item | `item()` | ➕ | 标题 + 描述 + 尾部文字，整行可点 |
| Chart | `sparkline()` / `bars()` | ⚠️ | 只有折线和柱状，没有坐标轴/图例/tooltip |
| Code Block | `code_text()` | ⚠️ | 单段等宽文本，没有行号与复制按钮 |
| Carousel | — | ❌ | 没做 |
| Marker | — | ❌ | 没做 |
| Questionnaire | — | ❌ | 没做 |
| Trace | — | ❌ | 没做（AI trace 视图，场景太专） |
| Message | — | ❌ | 没做（聊天气泡） |
| Message Scroller | — | ❌ | 没做 |
| Attachment | — | ❌ | 没做（聊天附件） |
| Bubble | — | ❌ | 没做（聊天气泡） |

### 导航

| shadcn | aino | 状态 | 说明 |
|---|---|---|---|
| Tabs | `tabs()` | ✅ | |
| Breadcrumb | `breadcrumb()` | ✅ | |
| Pagination | `pagination()` | ➕ | 槽位数**恒定** 7 个，翻页时位置不动 |
| Navigation Menu | `nav_item()` + `tabs()` | ⚠️ | 没有带下划线指示器的顶部导航组合件 |
| Menubar | `menubar()` | ➕ | 横排顶级菜单，**划到隔壁会自动切**（桌面菜单栏的老规矩，Web 上一堆库做不到） |
| Sidebar（条目） | `nav_item()` | ✅ | 高亮 + 点击 |

### 浮层

| shadcn | aino | 状态 | 说明 |
|---|---|---|---|
| Dialog | `dialog()` | ✅ | 返回「本帧是否打开」，按钮自己画 |
| Alert Dialog | `alert_dialog()` | ➕ | 返回「本帧是否点了确认」，按钮内建 |
| Dropdown Menu | `menu()` | ✅ | |
| Context Menu | `context_menu()` | ➕ | 在指定区域右键弹出 |
| Popover | `popover()` | ➕ | 内容用回调画，可以继续调 `ui.*` |
| Hover Card | `hover_card()` | ➕ | 悬停出卡片，不抢点击 |
| Tooltip | `tooltip()` | ✅ | 只有文字 |
| Toast | `toast()` | ⚠️ | 只能画一条；多条排队要调用方自己管 |
| Sheet / Drawer | `drawer()` | ✅ | |

### 其他

| shadcn | aino | 状态 | 说明 |
|---|---|---|---|
| Direction（RTL） | — | ❌ | 全部按 LTR 写死。要做 RTL 得先把 `Align`/`pad_x` 的左右语义反过来 |

### 数一下

按上面表格的行数（个别控件因为跨两类各出现一次，所以行数比 64 项多一点）：

- **✅ 一对一**：31 项
- **⚠️ 功能有但范围不同**：9 项
- **➕ 0.3.0 / 0.3.1 新补**：19 项（20 个 API，`field_height` 与 `aspect_rect` 是辅助）
- **❌ 明确没有**：10 项

那 10 项里，5 项是聊天场景（Message / Bubble / Attachment / Message Scroller / Marker），
其余是 RTL、Form、Carousel、Command、Trace、Questionnaire。

都补到 19 项了，剩下这几个为什么不再补：

| 没做的 | 为什么 |
|---|---|
| Message / Bubble / Attachment / Message Scroller / Marker | 聊天界面不是「基础控件」，是**一整个场景**。塞进基础库会把 456 KiB 撑成什么样先不说，它们还都需要气泡箭头、流式追加、滚动锚定一套自己的规矩 |
| Command | 它是**模式**不是控件：一个遮罩 + 一个输入框 + 一个过滤列表。用 `dialog()` + `input()` + `menu()` 十行就拼出来了，做成控件反而限制住布局 |
| Carousel | 要时间轴动画（自动轮播 / 惯性滑动），和「空闲必须 0 帧」的承诺正面冲突 |
| Form | shadcn 的 `Form` 是 react-hook-form 的胶水层，管的是校验编排。纯 UI 层给不出它，`field()` 已经是这一层能做的全部 |
| Trace / Questionnaire | AI 场景的专用视图，太专 |
| RTL | 全部按 LTR 写死。要做 RTL 得先把 `Align` 与 `pad_x` 的左右语义整个反过来，是**一次重写**，不是补一个控件 |

---

## 三、0.3.0 / 0.3.1 新补的 20 个 API

下面每一段在 `examples/gallery.cpp` 里都有对应的示例页：左侧是可点的预览，
右侧就是生成它的源码，直接照着抄。

### 表单

```cpp
// Toggle：按下态按钮（不是开关）
bool bold = true;
ui.toggle_button("粗体", &bold, rect);

// Toggle Group：单选 / 多选
int seg = 1;                      // -1 = 一个都不选
ui.toggle_group({"日", "周", "月"}, &seg, rect);

std::uint32_t mask = 0b101;       // 第 i 位对应第 i 项
ui.toggle_group_multi({"粗", "斜", "下划线"}, &mask, rect);

// Button Group：点了就返回索引，没有持久态
int hit = -1;
ui.button_group({"复制", "粘贴", "剪切"}, &hit, rect);

// Input Group：前后缀
std::string amount = "128";
ui.input_group(&amount, "¥", "元", rect);

// Input OTP：6 格验证码
std::string code;
ui.input_otp(&code, 6, rect);

// Field：标签 + 控件 + 说明（高度要先算）
const float h = ui.field_height("邮箱", "不会公开", "", 36.0f);
ui.field("邮箱", "不会公开", "", Rect(x, y, w, h), [&](const Rect& r) {
    ui.input(&mail, r);
});
```

### 展示

```cpp
ui.item("张伟", "zhangwei@example.com", "管理员", rect);   // 整行可点
ui.empty_state("还没有通知", "有事时会出现在这里", rect);

int page = 0;
ui.pagination(&page, 20, rect);      // 20 页，槽位恒定 7 个

int y = 2026, m = 10, d = 22;
ui.calendar(&y, &m, &d, rect);       // 点日期 / 翻月都会改这三个
```

### 浮层

```cpp
// Popover：内容用回调画，帧末上屏
bool open = false;
ui.popover("排序", &open, trigger_rect, Vec2(200, 96), [&](const Rect& c) {
    ui.item("按名称", "", "", Rect(c.x, c.y, c.w, 30));
    ui.item("按时间", "", "", Rect(c.x, c.y + 32, c.w, 30));
});

// Hover Card：悬停就出，不抢点击
ui.hover_card(anchor_rect, Vec2(210, 78), [&](const Rect& c) {
    ui.label("张伟", Rect(c.x, c.y, c.w, 22));
});

// Alert Dialog：返回值就是"点没点确认"
if (ui.alert_dialog("删除？", "删了不能恢复", "删除", "取消", &open, box,
                    Style().destructive())) {
    // 用户点了删除
}

// Drawer：side 0=右 1=左 2=下 3=上
ui.drawer("详情", &open, 0, Rect(w - 300, 0, 300, h), [&](const Rect& c) {
    ui.label("内容", c);
});

// Context Menu：在 area 里右键
int picked = -1;
ui.context_menu(rect, {"标记已读", "归档", "删除"}, &picked);

// Combobox：输入即过滤
int idx = 0;
ui.combobox({"C++", "Rust", "Go"}, &idx, rect);

// Menubar：横向一排顶级菜单，**划到隔壁会自动切**
std::vector<std::string> labels = {"文件", "编辑", "视图"};
std::vector<std::vector<std::string>> items = {
    {"新建", "打开…", "保存"},
    {"撤销", "重做"},
    {"放大", "缩小"},
};
MenuPick mp = ui.menubar(labels, items, Rect(0, 0, w, 34));
if (mp.valid())
    log(labels[mp.menu] + " → " + items[mp.menu][mp.item]);
```

### 布局

```cpp
float ratio = 0.42f;
Rect a, b;
ui.split_pane(&ratio, box, &a, &b, /*vertical=*/true, /*min_ratio=*/0.2f);
ui.panel(a);  ui.panel(b);

// Aspect Ratio：给宽度，高度按 16:9 算出来
const Rect thumb = Ui::aspect_rect(Rect(x, y, 160.0f, 0.0f), 16.0f / 9.0f);   // h = 90
```

---

## 四、几个刻意的取舍

**Drawer 只做淡入，不做位移。**
位移只动画面、不动命中区（`box` 里的控件位置是固定的），滑块滑到一半去点
按钮会点偏。淡入既保留了「出现」的感觉，又不会让按钮看着和点着的地方不一致。

**Pagination 的槽位数是恒定的。**
一开始按「当前页附近三页」写，结果翻页时槽位数从 8 变 6，整排按钮左右跳，
用户点第二下就点偏。现在页数 > 7 时固定 7 个槽，翻页只有内容变、位置不动。

**Toggle Group 支持"再点取消"。**
shadcn 的 `type="single"` 不允许取消，但桌面端这个更顺手。这是唯一一处
刻意和 shadcn 行为不一致的地方。

**Combobox 的搜索框不是 `input()`。**
`input()` 自己会拿一个焦点，和 `combobox` 抢焦点会互相把对方踢掉。
搜索框只是画出来的样子，字符处理在 `combobox` 内部。

**Calendar 不用 `<ctime>`。**
`mktime` / `localtime` 走本地时区与夏令时，同一份代码在 UTC 的 CI runner 和
东八区的机器上会差一天。月历要的是「日历上第几格」，跟时区无关，所以用纯
整数算（Sakamoto 求星期）。

**Menubar 把整条菜单栏都登记进输入白名单，而不是只登记当前触发器。**
浮层的闸门默认只放行「自己那几块矩形」，所以鼠标一离开当前顶级项就被挡住 ——
看起来就是「菜单卡住了，划不过去」。要让联动成立，必须把整条菜单栏一起
`gate_claim()`。注意 claim 登记的是**下一帧**的白名单，所以顺序是
「先 interact → 再算最终打开的是第几个 → 最后才 claim」，写反了联动就少一帧，
表现为划过去要动两下才切。

**Aspect Ratio 不是控件，是 `static` helper。**
立即模式里没有「容器组件」这回事 —— 没有东西需要被包裹、继承、或维护生命周期。
一个 `Rect` 进去、一个 `Rect` 出来，就是它的全部语义。

---

## 五、想自己加一个控件

照这一节做，不用改任何框架代码：

1. 在 `struct Ui` 里加一行声明（放对应分区）；
2. 在 `aino.hpp` 第 8.16 节加实现；
3. 写一个探针断言它的行为（`tools/probe/widget_probe.cpp` 里照抄一节）；
4. 加进 `tools/regress.py` 的清单。

四条硬约定（违反任意一条都会被回归挡住）：

- **交互走 `detail::interact()`**，动画走 `detail::animate_value()`；
- **浮层用 `deferred()` 画到帧末**，并在帧内 `gate_claim()` 登记下一帧的输入白名单；
- **不做无条件 `repaint()`** —— 只有状态真的变了才重画，否则空闲 CPU 就不是 0%；
- **尺寸全从 `box` 推**，不写死像素。
