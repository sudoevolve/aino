# 03 · 布局

立即模式没有控件树，"先画完再排版"这条路走不通 —— 只能**边画边切**。
`Stack` 就是把一个矩形按顺序切开的工具：给一个区域、一条主方向、一个间距，
然后每画一个元素就切走一块。

```cpp
aino::Stack col = ui.vbox({24, 24, 400, 300}, 10.0f, 18.0f);   // 纵向 / 间距 10 / 内边距 18
ui.heading("设置",  col.next(34.0f));
ui.input(&email,    col.next(24.0f));
ui.checkbox(&on, "通知", col.next(22.0f));
ui.button("保存",   col.next(24.0f));
```

和手写 `yy += 44` 相比，它多做了四件事：

1. `gap` **只在两个元素之间**垫（同 CSS 的 `gap`），所以 `fill()` 拿到的是精确剩余空间；
2. `next_frac` / `part` 直接做比例与等分，不用自己算 `(pw - 12) * 0.5`；
3. `next_text` 按文字的**真实像素宽度**定宽 —— 系统字体引擎能直接量宽；
4. 子布局可嵌套，父子之间继承 `gap`。

它刻意不做：内容自适应**高度**、自动换行、网格、约束求解。
这些要么需要两趟遍历，要么需要尺寸缓存依赖图，与「单头文件」的目标冲突。

---

## 1. 构造

```cpp
aino::Stack col = ui.vbox(const Rect& box, float gap = 0.0f, float pad = 0.0f);
aino::Stack row = ui.hbox(const Rect& box, float gap = 0.0f, float pad = 0.0f);
```

| 参数 | 说明 |
|---|---|
| `box` | 可用区域（逻辑坐标） |
| `gap` | 元素间距；0 = 紧贴 |
| `pad` | 内边距，`> 0` 时先对 `box` 做 `inset(pad)`；0 = 不缩进 |

`Stack` 是**纯值语义**：一个 `Rect` + 方向 + 间距 + 一个「上一元素已产出」标志。
不注册任何状态，所以复制、嵌套、丢弃都没有代价。

---

## 2. 全部方法

| 方法 | 说明 |
|---|---|
| `vertical()` | 是否是纵向 |
| `rest()` | 剩余区域。**已结算待垫的 gap**，即「下一个元素的起始位置」；只读不消费 |
| `remain()` | 剩余量（纵向返回高度，横向返回宽度） |
| `valid()` | 区域还有效（`w>0 && h>0`） |
| `next(size)` | 沿主方向切走 `size`；**超出剩余时收窄，绝不溢出** |
| `next_frac(f)` | 按**当前剩余空间**的比例切，`0.5` = 一半 |
| `next_text(s, extra=0, font_size=0, weight=-1)` | 按 `s` 的真实文字宽度 + `extra` 切（横向布局里做「刚好包住文字」的格子） |
| `fill()` | 吃掉全部剩余空间 |
| `skip(amount)` | 只推进游标，不产出矩形（做空隙） |
| `part(i, n)` | **不改游标**，把剩余区等分成 `n` 份、份间留 `gap`，取第 `i` 份（0 基） |
| `vbox(box, g=-1)` / `hbox(box, g=-1)` | 在指定区域开子布局；`g < 0` 继承当前 `gap` |
| `vbox(size, g=-1)` / `hbox(size, g=-1)` | **切走一块并立刻**在它里面开子布局（链式写两栏最省事） |

### 2.1 `gap` 的语义细节

```cpp
Stack c = ui.vbox({0, 0, 100, 200}, 10);

Rect a = c.next(30);   // y=0   h=30     ← 第一个元素不垫 gap
Rect b = c.next(30);   // y=40  h=30     ← 垫了一个 gap
Rect r = c.rest();     // y=80  h=120    ← 已结算待垫的 gap，就是下一个元素的起点
Rect f = c.fill();     // y=80  h=120    ← 拿到精确剩余，不会白少 10
```

`rest()` 和 `fill()` 都返回「下一个元素的起点」，这也是为什么
`c.remain()` 在 `next()` 之后立刻是可用的。

### 2.2 超出剩余会被收窄

```cpp
Stack c = ui.vbox({0, 0, 100, 50}, 0);
ui.button("A", c.next(40));   // h=40，剩 10
ui.button("B", c.next(40));   // h=10 ← 被收窄，不会画到 y=50 之外
ui.button("C", c.next(40));   // h=0  ← 空矩形
```

这条保证了「窗口缩小后内容不会溢出父容器」—— 内容会被压扁，而不是越界。
**代价**：压扁的组件看起来会很奇怪。所以要么给窗口设最小尺寸，要么把内容放进
滚动容器（见第 4 节）。

### 2.3 等分：`part`

```cpp
Stack c = ui.vbox({0, 0, 400, 100}, 12);
ui.button("左", c.part(0, 3));   // 宽度已按 3 等分并扣掉 2 个 gap
ui.button("中", c.part(1, 3));
ui.button("右", c.part(2, 3));
```

`part` **不改游标**，所以可以先用它拿几何、再另外决定内容。

### 2.4 嵌套

```cpp
Stack root = ui.hbox({0, 0, 800, 500}, 0);       // 左右两栏，无间距
Stack side = root.vbox(200.0f);                  // 切 200 宽，并立刻在里面开子布局
    ui.nav_item("概览", true, side.next(34.0f));
    ui.nav_item("设置", false, side.next(34.0f));

Stack main = root.vbox(0.0f);                    // 剩余全部宽度给主区
    ui.heading("标题", main.next(40.0f));
```

`root.vbox(200.0f)` 是「切一块 + 在其中开子布局」的合并写法，等价于：

```cpp
Rect side_box = root.next(200.0f);
Stack side = root.vbox(side_box);   // 注意：用 root 开，能继承 gap
```

### 2.5 跨轴居中：别写 `y + 17`

布局器切出来的盒子是**贯穿整条横轴 / 纵轴**的：`hbox` 里 `next(34)` 得到的是
「34 宽 × 整行高」，`vbox` 里得到的是「整行宽 × 34 高」。所以在 hbox 里放一个
34×34 的方形按钮，或放一行 18 高的文字，都要自己在**另一根轴**上居中。

手写 `Rect(r.x, r.y + 9, 34, 34)` 是错的开始 —— 行高一改（52 → 40）、字体一换、
图标一大，偏移就成了常量谎言，而且和旁边那个元素不在一条水平线上。

```cpp
Rect tb = row.next(34.0f);              // 34 宽 × 整行高
ui.icon_button("", tb.center_y(34.0f)); // 在行里纵向居中，宽度照抄
ui.muted_text("浅色", f.rest());        // 文字连拆都不用拆：样式默认就是垂直居中
```

- `center_y(h)` / `center_x(w)` / `center(w, h)`：把一个给定尺寸的盒子摆到中间。
- **文字不需要这三个**：`Style` 的 `valign` / `halign` 默认 `Middle` / `Center`，
  盒子给足就自动居中。给 18 高的小盒子反而会把自己顶到上面去。

---

## 3. 宽度自适应，高度不能

`next_text` 能量出文字宽度，是因为系统字体引擎直接给宽度。
**高度没有对应能力** —— 理由很实际：折行结果依赖宽度，而宽度又要先排版才知道，
两趟遍历与「边画边切」直接冲突。

所以正文类组件的高度必须**显式给**，但可以**量**：

```cpp
const float h = ui.text_height(long_text, 380.0f, aino::Style().wrap_text());
ui.label(long_text, col.next(h), aino::Style().wrap_text());
```

`text_height` 和 `draw_text` 用的是同一套折行代码（`layout_wrapped_lines`），
不是估算 —— 给多少就画多少，不会裁掉最后一行。`alert` 同理用 `alert_height`：

```cpp
const float h = ui.alert_height(title, desc, 380.0f, st);
ui.alert(title, desc, col.next(h), st);
```

### 一整个面板的高度：`begin_measure` / `end_measure`

单个文本能量，一整个「标题 + 表单 + 按钮」的块就没法一个个量了。
这时用测量模式：把那段绘制代码**空跑一遍**，它把碰到的所有盒子并成一个外接矩形。

```cpp
ui.begin_measure();
draw_my_panel(ui, app, aino::Rect(x, y, w, 0.0f));   // 高度先给 0
const aino::Rect used = ui.end_measure();            // 内容实际占到的大小
ui.panel(aino::Rect(x, y, w, used.h));               // 用真实高度再画一遍
draw_my_panel(ui, app, aino::Rect(x, y, w, used.h));
```

测量模式下：图元只记账不落像素、命中测试一律返回"没碰"、`deferred()`
不入队、不请求帧、动画不推进、**画布指针被摘成 null**。所以空跑一遍
没有副作用，代价只是那段布局代码每帧多跑一次（不碰光栅化，通常几十微秒）。

> 摘画布是第二重保险。图元与命中测试自己会判 `measure_on`，但总有控件绕过图元
> 直接 `raster_poly` / `raster_arc`（`sparkline` 的面积填充、`spinner` 的弧、
> `rating` 的星）。漏掉一个的后果不是"高度算错"，而是**屏幕上从 (0,0) 起多出一块
> 真图形** —— 因为测量跑的是 box 原点的绘制代码。`tools/probe/measure_probe.cpp`
> 逐字节盯着这条：测量前后画布必须完全一致。

滚动区里量到的只有**视口内**的部分 —— 内嵌长列表天生超出视口，不能算进
外层高度，否则一张卡片会被撑成几千像素。

画廊布局报告通过 `AINO_LAYOUT_REPORT=<路径>` 输出当前卡片的名称、分类、位置和尺寸。
未搜索时为 15 个分类、43 张卡片；它与测量 API 的用途不同。
卡片内容高度、窄窗口与搜索结果由 `macos_gallery_probe.cpp` 检查。

---

## 4. 滚动

滚动只有一件事：**一个视口 + 一段内容高度**。虚拟列表不是第二个 API，只是
「内容高度 = 行数 × 行高」的特例 —— 所以两者共用一个函数、一份偏移（按 `box`
记忆）、一根滚动条、一套裁剪。早先它们各存一份偏移，同一个 `box` 换个写法
位置就丢了，那是设计缺陷，0.2.1 已经合并。

**嵌套时滚轮只归最内层。** 滚轮事件是整窗一个，而滚动区可以套（内容区里再放一个
列表）。只按「鼠标在不在框里」判定的话，鼠标在内层列表上滚，外层内容区会一起滚 ——
两个滚动条同时动。判定用的是**上一帧**记录的滚动区矩形与嵌套深度：更深的一层含指针，
本层就让路。布局每帧基本一致，这个近似足够；第一帧没有记录，退化成老行为。

### 4.1 通用：内容高度自己给

```cpp
// 作用域即视口：析构自动收尾（弹裁剪、还原布局偏移、把滚动条画在最上面）
{
    auto sc = ui.scroll(viewport, content_h);
    aino::Stack c = ui.vbox({viewport.x, viewport.y, viewport.w, content_h}, 10.0f);
    for (int i = 0; i < 40; ++i)
        ui.label("第 " + std::to_string(i + 1) + " 行", c.next(28.0f));
    // sc.y() 读当前偏移；sc.to(0) 回到顶部；sc.content(h) 事后改内容高度
}
```

老写法 `ui.begin_scroll(...) ... ui.end_scroll();` 与上面完全等价
（只是不返回句柄，收尾交给 `end_scroll()`）。

| 项 | 说明 |
|---|---|
| 触发条件 | 滚轮**只有在鼠标位于视口内**时生效 |
| 步长 | 每格 `44px`（`wheel * 44`） |
| 滚动条 | 收尾时画在内容之上：视觉 3px、**热区 11px**，`muted_foreground`（悬停或拖动时加深到 78%） |
| 拖滑块 | 按比例改偏移；拖动中鼠标离开视口也继续跟（后端已 `SetCapture`） |
| 点轨道 | 滑块中心跳到点击处（Windows 传统行为，比"翻一页"少一次点击） |
| `scroll_y()` | 当前偏移；收尾之后也能读（返回最近一次的值） |
| `scroll_to(off)` | 程序化滚动，clamp 到合法范围；也可以 `sc.to(off)` |
| 裁剪 | 视口自带 `push_clip`，内容不会画到外面 |

**裁剪同时限制绘制与命中。** 滚出视口的组件不能拦截视口外按钮。
组件在滚动作用域内按内容坐标生成 ID，滚动偏移不会重置 bool 状态、焦点和动画。
使用 `vbox` 等库布局器时由作用域处理偏移；直接传绘图坐标时从 y 减去 `scroll_y()`，
不要对同一盒子重复减偏移。手写动画的 ID 也应保持稳定，不要使用随滚动变化的屏幕坐标。

### 4.2 虚拟：固定行高，只画可见行

```cpp
int drawn = ui.scroll(list_box, 100000, 26.0f, [&](int i, const aino::Rect& row) {
    ui.label("第 " + std::to_string(i + 1) + " 行", row);
});
// drawn ≈ 视口高度 / 行高，与 100000 无关
```

| 项 | 说明 |
|---|---|
| 回调 | **只对可见项调用**；不可见的项连布局都不做 |
| `row` | 已经算好屏幕位置（含滚动偏移），可直接当组件的 `box` |
| 返回值 | 本帧实际画了几项 —— 可以直接断言「只有一屏的量」 |
| 回顶/跳转 | `ui.scroll_to_index(box, 99999, row_h, n)`，只改偏移，不全量遍历 |
| 偏移记忆 | 和通用滚动**是同一份**：先 `scroll(box, h)` 再 `scroll(box, n, h, cb)`，滚到哪继续在哪 |

**为什么固定行高**：可见区间 = `floor(off/item_h) .. ceil((off+h)/item_h)`，
是 O(1)。行高不固定就得先累加前 k 项高度才能定位第 k 项，那是另一套
（前缀和 + 版本号）机制，本库没做 —— 要可变行高就把行高对齐到统一值，
或者把「前缀和」算在调用方。

实测（`tools/probe/bigfeat_probe.cpp`）：**1000000 项，本帧绘制 10 项**。

### 4.3 只要区间，自己画

```cpp
int   first, last;
float off = ui.scroll_visible(box, n, row_h, &first, &last);  // 算区间 + 处理滚轮
ui.scroll_bar(box, n * row_h);                                 // 自己补滚动条
float cur = ui.scroll_offset(box);                             // 当前偏移
```

这三个是「我已经有一套绘制方式，只想借偏移」时用的零件，和 `scroll()` 共用同一份状态。

---

## 5. 裁剪

```cpp
ui.push_clip(box);
    // 这里的几何与文字都被限制在 box 内
ui.pop_clip();
```

裁剪对**文字也生效**（连同 `clip ∩ box` 一起裁），长英文串不会冲出面板。
这是踩过的坑：早期只裁几何，长串会横穿整个窗口。

`scroll()` / `begin_scroll` 内部已经做了裁剪，不需要再包一层。

---

## 6. 早期接口（保留兼容）

`row()` / `column()` / `push_padding()` / `pop_padding()` / `begin_group()` / `end_group()`
是 Stack 之前的接口，仍可用但**新代码建议直接用 Stack**：

| 旧接口 | 等价写法 |
|---|---|
| `Rect row(h)` | 在「当前容器」里切一行 |
| `Rect column(w)` | 切一列 |
| `push_padding(x, y)` / `pop_padding()` | `Stack(box, gap, pad)` 的内边距 |
| `begin_group(box)` / `end_group()` | `Stack` 的 `box` 参数 |

`begin_group` 仍然有用：它压入一个「当前容器」，让 `row()`/`column()` 相对它切分。
但 `Stack` 的显式 `box` 更清晰，不必依赖隐式容器状态。

---

## 7. 布局组合示例

```cpp
void composePage(aino::Ui& ui, AppState& s) {
    const aino::Rect page = {292, 92, ui.width() - 292 - 32, ui.height() - 92 - 24};
    ui.panel(page);

    aino::Stack col = ui.vbox(page, 14.0f, 24.0f);
    ui.heading("标题", col.next(34.0f), aino::Style().left());
    ui.muted_text("说明文字", col.next(20.0f), aino::Style().left());
    ui.separator(col.next(1.0f));

    // 两栏：左 1/2、右 1/2（part 不改游标，所以先取两份再用 fill 收尾）
    aino::Rect left  = col.part(0, 2);
    aino::Rect right = col.part(1, 2);
    aino::Stack lc = ui.vbox(left,  10.0f);
    aino::Stack rc = ui.vbox(right, 10.0f);
    ui.input(&s.email, lc.next(24.0f));
    ui.toggle(&s.on,   rc.next(26.0f));

    ui.button("保存", col.fill(), aino::Style().primary());   // 吃到底部
}
```

该示例展示 Stack 的嵌套与切片。画廊采用固定侧栏、工具栏和连续滚动的卡片区域。

---

## 8. `split_pane` —— 可拖动的两栏

```cpp
float ratio = 0.42f;
aino::Rect a, b;
if (ui.split_pane(&ratio, area, &a, &b, /*vertical=*/true, /*min_ratio=*/0.2f)) relayout();
ui.panel(a);
ui.panel(b);
```

`ratio` 是分隔线的位置（0..1），拖动时由库写回。`first` / `second` 是**算出来的两块矩形** ——
它不接管绘制，你想在里面放什么就放什么。

| 参数 | 含义 |
|---|---|
| `vertical` | true = 竖着分（左右两栏、分隔线竖直）；false = 上下两栏 |
| `min_ratio` | 两侧各自最少占比，默认 0.15。`ratio` 会被夹在 `[min_ratio, 1 - min_ratio]` |
| 返回 | 本帧 `ratio` 是否变了（拖动中每帧都可能 true） |

两个矩形之间**让出了 7px 的缝**（`bar`），所以两块内容不会贴着分隔线。

> **视觉 1px、热区 7px。** 分隔线只画 1px，但命中区给到 7px —— 和滚动条同一个取舍：
> 1px 是画给人看的，不是能拖得中的尺寸。鼠标移上去会变成 `ResizeH` / `ResizeV` 光标。
> 它靠 `impl->hover_region(handle)` 登记，**不登记的话指针停在分隔线上不动时框架不重绘，
> 光标形状不会变**（看起来像"拖不动"）。

拖动用的是「按下那一刻记一个 flag，直到松开」，不是"每帧比较鼠标是否按下" ——
鼠标拖出窗口再松手也能正确结束拖动。
