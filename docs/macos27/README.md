# macOS 27 设计参数（来自 Apple 官方 Figma Kit）

数据来源：Apple **macOS 27 (Community)** kit，本地 `.fig`（`canvas.fig`，Figma 二进制 kiwi 格式，版本 106）逐字段解出。
原始提取结果在同目录：

| 文件 | 内容 |
|---|---|
| `variables.json` | 162 个变量，分 `Colors`(71) / `Sizes`(68) / `Kit`(18) / `Context`(1) |
| `styles_text.json` | 24 个文字样式（SF Pro，含字号/行高/字距） |
| `styles_effect.json` | 11 个效果样式（6 种投影 + 背景模糊 + 内阴影） |
| `styles_fill.json` | 152 个填充样式（旧版平涂色，已被变量取代） |
| `components.json` | 3429 个组件（名称 / 尺寸 / 圆角 / 内边距 / 间距） |

> 说明：本库**不实现液态玻璃**（用户要求）。Kit 里的 `Kit` 变量集（Liquid Glass 的 Depth / Dispersion / Frost / Splay / Refraction / Opacity）只作原样记录，不参与渲染。其余参数**完全按官方值**。

---

## 一、颜色（Colors 变量集，Light / Dark 双模式）

### 标签（文字）

| token | Light | Dark | 说明 |
|---|---|---|---|
| `Labels/Primary` | `#000000D9` | `#FFFFFFD9` | 正文，85% |
| `Labels/Secondary` | `#00000080` | `#FFFFFF8C` | 次要说明，50% / 55% |
| `Labels/Tertiary` | `#00000040` | `#FFFFFF40` | 占位符，25% |
| `Labels/Quaternary` | `#0000001A` | `#FFFFFF1A` | 10% |
| `Labels/Quinary` | `#0000000D` | `#FFFFFF0D` | 5% |
| `Labels/White` | `#FFFFFF` | `#FFFFFF` | 强调色上的文字 |

### 叠加填充（Opaque，用于按钮面 / 悬停层 / 轨道）

| token | Light | Dark |
|---|---|---|
| `Fills - Opaque/Primary` | `#0000001A` (10%) | `#FFFFFF1A` |
| `Fills - Opaque/Secondary` | `#00000014` (8%) | `#FFFFFF14` |
| `Fills - Opaque/Tertiary` | `#0000000D` (5%) | `#FFFFFF0D` |
| `Fills - Opaque/Quaternary` | `#00000008` (3%) | `#FFFFFF08` |
| `Fills - Opaque/Quinary` | `#00000005` (2%) | `#FFFFFF05` |

### 表面

| token | Light | Dark |
|---|---|---|
| `Window Background` | `#FFFFFF` | `#1E1E1E` |
| `Materials - Controls/Sidebar` | `#EAEAEA` | `#1E1E1E` |
| `Materials - Controls/Title Bar` | `#EAEAEA` | `#3C3C3C` |
| `Materials/Thick`（弹层底） | `#F6F6F6` | `#282828` |
| `Material/Ultra Thick` | `#F6F6F6D6` | `#282828CC` |
| `Material/Thin` | `#F6F6F67A` | `#28282880` |

### 强调色（macOS 27 新值，注意和旧 systemBlue `#007AFF` 不同）

| token | Light | Dark |
|---|---|---|
| `Accents/Blue` | `#0088FF` | `#0091FF` |
| `Accents/Red` | `#FF383C` | `#FF4245` |
| `Accents/Orange` | `#FF8D28` | `#FF9230` |
| `Accents/Yellow` | `#FFCC00` | `#FFD600` |
| `Accents/Green` | `#34C759` | `#30D158` |
| `Accents/Mint` | `#00C8B3` | `#00DAC3` |
| `Accents/Teal` | `#00C3D0` | `#00D2E0` |
| `Accents/Cyan` | `#00C0E8` | `#3CD3FE` |
| `Accents/Indigo` | `#6155F5` | `#6D7CFF` |
| `Accents/Purple` | `#CB30E0` | `#DB34F2` |
| `Accents/Pink` | `#FF2D55` | `#FF375F` |
| `Accents/Brown` | `#AC7F5E` | `#B78A66` |
| `Grays/Gray` | `#8E8E93` | `#98989F` |

### 杂项

| token | Light | Dark |
|---|---|---|
| `Miscellaneous/Tables/Column Separator`（分隔线） | `#0000001A` | `#FFFFFF1A` |
| `Miscellaneous/Scrollbar` | `#00000080` | `#FFFFFF8C` |
| `Miscellaneous/Progress Bars/Track - Stroke` | `#00000012` | `#FFFFFF0A` |
| `Miscellaneous/Tables/BG - Selected + Active` | `#0165E2` | `#0158D2` |
| `Miscellaneous/Tables/BG - Selected + Inactive` | `#00000024` | `#FFFFFF2E` |
| `Miscellaneous/Tables/Alternating Row Color` | `#0000000D` | `#FFFFFF0D` |
| `Miscellaneous/Tooltips/Inner Stroke` | `#FFFFFF00` | `#FFFFFF26` |
| `Miscellaneous/Alerts/Overlay` | `#ABABAB99` | `#1717179E` |

---

## 二、尺寸（Sizes 变量集，5 档：Mini / Small / **Medium** / Large / XL）

`Medium` 是 macOS 默认档（与 `NSControl.ControlSize.regular` 对应）。

| token | Mini | Small | **Medium** | Large | XL |
|---|---|---|---|---|---|
| `Global/Height` 控件高 | 16 | 20 | **24** | 28 | 36 |
| `Global/Radius` 圆角 | 4 | 5 | **6** | 7 | 9 |
| `Global/Font Size` | 10 | 11 | **13** | 13 | 13 |
| 行高（由文字样式） | 13 | 14 | **16** | 16 | 16 |
| `Button/Padding - Horizontal` | 7 | 10 | **16** | 16 | 16 |
| `Button/Vertical Padding` | 2 | 3 | **4** | 6 | 10 |
| `Button/Radius` | =Global/Radius | | **6** | 1000（胶囊） | 1000 |
| `Toggle (Switch)/Track - Width` | 36 | 44 | **54** | 64 | 80 |
| `Toggle (Switch)/Track - Height` | =Global/Height | | **24** | 28 | 36 |
| `Toggle (Switch)/Knob - Width` | 21 | 26 | **32** | 38 | 47 |
| `Toggle (Switch)/Knob - Height` | 13 | 16 | **20** | 24 | 30 |
| `Toggle (Switch)/Knob - Inset` | 1.5 | 2 | **2** | 2 | 3 |
| `Toggle (Switch)/Knob - Clicked - W` | 33 | 40 | **50** | 59 | 73 |
| `Toggle (Switch)/Knob - Clicked - H` | 20 | 26 | **31** | 37 | 46 |
| `Checkboxes/Width`（=Radio Button/Width） | 12 | 14 | **16** | 18 | 18 |
| `Checkboxes/Radius` | 3.5 | 4.5 | **5.5** | 6.5 | 6.5 |
| `Checkboxes/Width - Checkmark` | 7.78 | 9.31 | **9.31** | 11.72 | 11.72 |
| `Checkboxes/Height - Checkmark` | 7.55 | 8.93 | **8.93** | 11.30 | 11.30 |
| `Radio Button/Width - Dot` | 4 | 4.8 | **4.8** | 5 | 5 |
| `Slider/Knob - Non-ticked/Width` | 16 | 18 | **20** | 24 | 24 |
| `Slider/Knob - Non-ticked/Height` | 12 | 14 | **16** | 20 | 20 |
| `Slider/Knob - Clicked - W × H` | 20×15 | 23×18 | **25×20** | 30×25 | 30×25 |
| `Slider/Track/Inset` | 1 | 1 | **1** | 0 | 0 |
| `Segmented Control/Margins` | 6 | 8 | **10** | 12 | 14 |
| `Segmented Control/Height - Separator` | 10 | 12 | **14** | 18 | 20 |
| `Menu/Height` | 19 | 22 | **24** | 24 | 24 |
| `Menu/Font Size` | 10 | 11 | **13** | 13 | 13 |
| `Menu/Inset - Left - Header` | 14 | 18 | **20** | 20 | 20 |
| `Popup/Inset - Left` | 7 | 10 | **12** | 14 | 18 |
| `Fields/Inset - Left/Right` | 6 | 6 | **8** | 8 | 10 |
| `Stepper/Width` | 13 | 17 | **20** | 23 | 30 |
| `Stepper/Font Size` | 8 | 9 | **12** | 12 | 12 |
| `Combo Button/Width - Button` | 16 | 20 | **24** | 24 | 30 |
| `Combo Button/Radius - Button` | 2.5 | 3.5 | **4.5** | 5.5 | 6.5 |
| `Color Well`（宽 × 高） | 32×16 | 40×20 | **48×24** | 56×28 | 72×36 |

实测组件尺寸（`components.json`，Medium 档一致性核对）：

| 组件 | 尺寸 | 圆角 |
|---|---|---|
| `Segmented Control/2/1` | 108×24 | 6 |
| `Push Button/*` | 48×22 | 5 |
| `Text Field/Rounded` | 206×22 | 5 |
| `Menu Item`（10pt 档） | 208×19 | 5（hover 态） |
| `List Item` | 150×20 | — |
| `Group Box/Rounded Scroll Box` | 200×200 | 10 |
| 对话框按钮（胶囊款） | — | 100（= 高度一半） |

---

## 三、文字（SF Pro）

| 样式 | 字号 | 行高 | 字重 |
|---|---|---|---|
| Large Title | 26 | 32 | Regular / Bold |
| Title 1 | 22 | 26 | Regular / Bold |
| Title 2 | 17 | 22 | Regular / Bold |
| Title 3 | 15 | 20 | Regular / Semibold |
| Headline | 13 | 16 | **Bold** |
| Body | 13 | 16 | Regular / Semibold |
| Callout | 12 | 15 | Regular / Semibold |
| Subheadline | 11 | 14 | Regular / Semibold |
| Footnote | 10 | 13 | Regular / Semibold |
| Caption 1 | 10 | 13 | Regular / Medium |
| Caption 2 | 10 | 13 | Medium / Semibold |

字距全部 `0%`；`SF Pro` 缺字回退 `SF Pro Text`/系统无衬线。

---

## 四、投影（Effect 样式）

| 样式 | 参数 |
|---|---|
| Shadows - Light/Primary | `0 0 3 rgba(0,0,0,.55)` + `0 36 100 rgba(0,0,0,.40)` |
| Shadows - Light/Secondary | `0 0 3 rgba(0,0,0,.55)` + `0 8 40 rgba(0,0,0,.25)` |
| Shadows - Light/Tertiary | `0 0 3 rgba(0,0,0,.55)` + `0 36 100 rgba(0,0,0,.25)` |
| Shadows - Light/Quaternary | `0 0 3 rgba(0,0,0,.55)` + `0 4 20 rgba(0,0,0,.25)` |
| Window Inner Shadow（Light） | `inset 0 0 3 rgba(255,255,255,.10)` |
| Shadows - Dark/Primary | `0 36 100 rgba(0,0,0,.70)` + `0 0 1 spread 1 #000` |
| Shadows - Dark/Secondary | `0 8 40 rgba(0,0,0,.70)` + `0 0 1 spread 1 #000` |
| Shadows - Dark/Tertiary | `0 36 100 rgba(0,0,0,.45)` + `0 0 1 spread 1 #000` |
| Shadows - Dark/Quaternary | `0 4 20 rgba(0,0,0,.45)` + `0 0 1 spread 1 #000` |
| Window Inner Shadow（Dark） | `inset 0 0 1 spread 1 rgba(255,255,255,.20)` |
| Medium Background Blur | 半径 30 |

用法：菜单/下拉 = Quaternary；弹层/浮窗 = Secondary；模态 = Primary；卡片 = Tertiary。

> ⚠ 这些数值是 Figma 的语义（`blur` = 模糊半径，高斯 σ = blur/2），**不是**"轮廓
> 处满 alpha 再往外平方衰减"。渲染器的覆盖率曲线必须是 `α(d) = A·[1−Φ(2d/blur)]`，
> 否则 `0 0 3 .55` 会被画成一圈 55% 黑的 3px 实心描边。详见第九节。

---

## 五、形状语言

- **圆角矩形**：Mini ~ Medium（半径 = 4/5/6，等于高度的 1/4）
- **胶囊**：Large / XL 的按钮与开关（半径 1000），以及所有开关的轨道（`r=1000`）
- **同心圆角**：子元素半径 = 父半径 − 内边距（面板 10 / 菜单 10 / 气泡 10）
- **描边**：1px，颜色 = 分隔线（`#0000001A` / `#FFFFFF1A`）；暗色弹层内描边 `#FFFFFF26`
- 不做液态玻璃：所有材质取上表的**不透明等价色**，不加模糊/折射/高光

---

## 六、逐组件对照（本轮已落地）

这一节是**每个组件到底按哪条官方符号画的**，以及它的关键数值。数值全部来自
`macos27.fig` 里对应 SYMBOL 的图层树（`docs/macos27/tools/q.js` 可以直接再查一遍）。

| 组件 | 官方来源符号 | 关键数值 | 之前错在哪 |
|---|---|---|---|
| 圆角（全局） | 所有 SYMBOL 的 `cornerSmoothing` | `0.6` | 用纯圆弧，曲率在切点突变 → "发尖"，Web 味的主因 |
| 推按钮 · 标准 | `Buttons/Bordered/.../Content Area` | 67×24，8% 黑填充，**无描边**；按下 16%；标签 13px Medium 85% | 10% 底 + 1px 描边 + hover 变深 |
| 推按钮 · 强调 | `Buttons/Bordered - Prominent (Default) D` | 强调色实心 + 白字 + 顶部 1px 亮边 | 无亮边、hover 变深 |
| 开关 | `Switch/Idle/{True,False}/True/Content Area Form` | 轨道 54×24 胶囊；滑块 **32×20 白色胶囊**，内缩 2；关闭 5% 黑 | 滑块画成 20px 圆点 |
| 复选框 | `_Checkbox/_Checkbox Background/Light` | 14×14，r3.5，填 `#F6F6F6`@84%，描边 0.5px@10%，内阴影 (0,2,8%)+(1.5,1.6,10%) | 16×16 + 平涂 + 1px 边，没有内阴影 → 不"凹" |
| 单选 | 同复选框配方（圆形） | 14 圆，选中 = 强调色 + 白点 | 空心圆描边 |
| 输入框 | `Text Field/Rounded/Enabled` | 206×22，r5，纯白底，内阴影 + 淡描边；占位符 25% 黑 13px | 平底 + 1px 边 |
| 进度条 | `Determinate/Medium/*` | 100×10 胶囊，轨道 5% 黑；填充最小宽度 = 高度 | 4px 轨道 |
| 滑块 | `Slider/Idle/*`（100×24） | 轨道 **6px** 胶囊；把手 20×16 白色胶囊 + 阴影 | 4px 轨道 + 圆点把手 |
| 分段控件 | `Segmented Control/3/1/True` | 容器 8% 黑，选中段内缩 2、**强调色实心 + 白字**，标签统一 Medium | 选中 = 白底黑字 |
| 侧边栏项 | `Item/Small/Selected` | 24 高，r5；选中 = 强调色 **23%** 叠加，文字仍 85% 黑 | 实心蓝底 + 白字（那是 iOS） |
| 表格 | — | 去外框、去列竖线，只保留表头 + 行分隔线（1px 分隔线色） | 带边框的盒子 + 竖线 |
| 代码块 | — | 凹陷面（7% 黑），**无描边**，无行号竖线 | 灰底 + 1px 边框 + 竖线 |
| 页面版式 | — | 标题在上、内容在下、无卡片边框；侧边栏用 source list 灰 | 带描边的卡片 + 底部脚注标题 |

### 渲染器为此新增的能力

`aino.hpp` 里新增（都在 `detail::` 命名空间，组件直接调用）：

| 能力 | 函数 | 说明 |
|---|---|---|
| 连续圆角 | `corner_geo` / `corner_inset` / `rr_span` | 三段式（贝塞尔+圆弧+贝塞尔），常数与 Figma `figma-squircle` 逐项对齐 |
| 竖直渐变填充 | `raster_rrect_paint`（`RowPaint::grad`）、`Ui::fill_rect_grad` | 一列一色，保留整行快填 |
| 内阴影 | `raster_rrect_inner`、`Ui::inner_shadow` | 沿内边缘向内平方衰减，macOS 的"凹陷"全靠它 |
| 顶部高光 | `Ui::stroke_rect_top` | 只画上半圈 1px，整圈描边会变成"描边框" |
| 显式圆角强度 | `Ui::fill_rect_s` / `stroke_rect_s` | 胶囊类控件传 0，其余走主题 0.6 |

---

## 七、第二轮：逐组件复检（修掉"白块"与浮层形状）

第一轮只换了数值，第二轮机检时**四个组件同时暴毙**：日历的某一格、Toggle 的开关、
分段控件的选中段、Combobox 的选中行，全部变成一块**不透明纯白矩形把文字盖死**。
根因不在配色，在一个被当成"淡入"用错的基础 API。

### 7.1 根因：`with_alpha` 是"替换"不是"缩放"

深色主题里 `th.accent` = `rgba(255,255,255,13)`（5~13% 白的叠加色）。组件里到处写着

```cpp
fill_rect(r, th.accent.with_alpha(uint8_t(255.0f * t + 0.5f)));   // t=1 时 → 纯白 100%
```

`with_alpha()` 是**把 alpha 换成新值**，不是按比例缩放。于是"悬停/选中淡入到 100%"
= 一块纯白实心矩形。浅色模式下 `th.accent` 是 5% 黑，同一行代码又会变成纯黑块 ——
症状看起来完全不同，实际是同一个 bug。

修法：新增 `detail::fade(c, t)`（= `c.scaled_alpha(t)`，缩现有 alpha），全库 12 处
`*.with_alpha(255*t)` 全部替换。**规则：只有不透明色（tint / label / 强调色）能
用 `with_alpha`，叠加色一律用 `fade`。**

### 7.2 各组件这一轮改了什么

| 组件 | 改前 | 改后（依据） |
|---|---|---|
| 日历日期格 | 选中/悬停是**圆角方块**，悬停还会变纯白块 | 正圆（直径 = min(格宽,格高) − 6，22~30），选中 = 强调色实心圆 + `on_color()` 白字；悬停 = 5% 标签色圆 |
| 开关 | 54×24 写死，box 再小也照画 | 按 box 高度选官方四档 Mini/Small/Regular/Large（36/44/54/64 宽），宽高比恒 2.25；滑块 = `0.593×轨道宽` × `轨道高−4` |
| 分段控件 / Toggle 组 | 轨道是 `th.background` 平铺 + 1px 描边；选中段 = 13% 白叠加 + 白字（不可读） | 轨道 = **凹槽**（浅色 8% 黑 / 深色 8% 白，`Theme::track_well()`，无描边）；选中段 = 强调色实心 + `on_color()`+粗体；段间分隔线 1×14 @25%（相邻段有选中时不画） |
| 按下态按钮 `toggle_button` | 选中 = 实心强调色（当成主按钮） | 选中 = 强调色 22% 淡底 + **加粗标签**（格式栏 B/I 的样子），不抢主操作 |
| Combobox / Select 弹出列表 | 行高 32，悬停 = 13% 灰底，对勾在行尾 | 行高 **24**（kit `Menu Item`），悬停 = 强调色实心 + 白字 r5，对勾移到**行首 16px 勾位**（所有行统一留位，不跳动）；容器圆角 8 |
| Menu / Context Menu | 悬停 = 13% 灰 | 强调色实心 + 白字，r5；容器 8 |
| 表格 | 有外框 + 表头 36 高 + 表头铺底 | 无外框、表头 24 高、表头只留一行灰字 + 分隔线（传 bg 才铺） |
| Alert | danger 变体一律套一圈**红描边** | 面中性（3% 标签色），告警只靠红色符号 + 红色标题 |
| 破坏性按钮 | 红底白字（iOS 主按钮） | 8% 面 + **红字**（macOS 告警里的"删除"就是这个） |
| Badge | 实心强调色 + 白字 | 强调色 15% 底 + 强调色字（tag 的写法） |
| 分页当前页 | 圆角方块 | 胶囊/圆（`inner.h * 0.5`） |
| Dialog 按钮排布 | 面板底边二分之一摊开 | 右下角右对齐、定宽 86（示例层） |
| 所有弹层 | 圆角 4 | 容器 8、行高亮 5 |

### 7.3 截图工具链的两个坑（已修）

- **帧内取后备缓冲会漏掉所有弹层**：下拉/菜单/对话框都是 `deferred()` 到帧末才画，
  在 `win.run()` 回调里立刻 `dump_canvas_bmp` 拿到的是"没有弹层"的帧。改为
  下一帧开头取上一帧的缓冲（`AINO_SHOT_BMP` + 可选 `AINO_SHOT_EVERY`）。
- **下拉的展开态点不动**：合成鼠标事件的 press/release 跨帧时 `clicked` 会丢。
  示例里加了 `AINO_OPEN=select|combobox|dialog|popover|drawer` 钩子直接点亮
  `impl->flags`（只影响示例，库里仍然是"点出来的"）。

### 还没做

- ~~数字列右对齐~~（第八节已做）
- 液态玻璃按约定跳过

---

## 八、第三轮：全量对齐（逐控件，含没有对应物的）

这一轮的原则变了：不再"挑几个明显的改"，而是**把库里每个控件都过一遍**，
能在一一映射到 kit 符号的就照符号画；kit 里没有对应物的（alert 条、badge、
pagination、empty、kbd、toast、drawer、hover card……），按**同一套语法**推导：
同一个半径尺度、同一套叠加填充档位、同一套文字样式、同一条分隔线。

### 8.1 先把重复的画法收成一处

对齐做不到位的真正原因是**同一个控件在五个地方各画一遍**：菜单面板在
`menu` / `menubar` / `context_menu` / `select` / `combobox` 里各有一份，
行高 32 / 24 不一、悬停有的 5% 灰有的实心蓝、有的有勾有的没有。
这一轮抽出 `detail::paint_menu()`，五处全部改调它，参数只剩"行矩形 + 悬停位"。

同理，分段控件（`segmented` → `tabs(pill)`）和 Toggle 组（`group_cell`）现在
共用一套轨道 + 内缩 2 的滑块 + 1×14 段间细缝；它们在 AppKit 里本来就是同一个控件。

### 8.2 逐控件改了什么

| 控件 | 官方来源 / 推导依据 | 关键数值 |
|---|---|---|
| `heading` | Title 1 | 22 / **Bold**（原 24 semibold，两个值都不是官方值） |
| `subheading` | Title 3 | 15 / Semibold（原 18） |
| `label` / `muted_text` | Body | 13 Regular；次要色 50%/55% |
| `badge` | 推导（tag） | 高 16、r**4.5**（不是胶囊）、字 10、左右 6 |
| `kbd` | 推导（快捷键提示） | 5% 面 + r4 + 11px，去掉"机械键帽"的立体下沿 |
| `panel` / `card` | Group Box | **1px 细线 + r10，内部不铺底**（Web 的白卡片+灰底+阴影在 mac 里不存在） |
| `button` · Outline | Bordered | 8% 面 **+ 1px 细线**（原与 Secondary 完全同形，六个变体只剩五种视觉） |
| `button` 全体系 | Push Button | 高度 24（Medium），r6，标签 13 Medium |
| `input` / `textarea` | Text Field/Rounded | 暗色下面比窗口**亮一档**（原来直接用 window_bg，暗色等于没有框）；描边 = 分隔线色；焦点环 3px @25% |
| `tabs(pill)` / `segmented` | Segmented Control | 轨道 = 凹槽 8%，选中 = 强调色实心内缩 2、同心圆角，段间 1×14 @25% |
| `slider` | Slider/Non-ticked | 轨道 6、把手 20×16；**悬停不变形**，按下才 25×20 |
| `stepper` | Stepper/No Field | **20 宽竖条**：上下两个半高按钮 + 实心小三角，数值右对齐（原来是 `[−] 值 [+]` 的 Web Number Input） |
| `table` | Column Header + List | 表头 11 Semibold @50%；行高 20；**数字列右对齐**（含表头，按列首个非空单元格判定，支持 `12 ms` 这种带单位的值） |
| `menu` / `menubar` / `context menu` / `select` / `combobox` | Menu Item | 容器 r10 + Quaternary 投影；行高 **24**（原 32）；悬停 = 强调色实心 + 反色文字 r5；选中勾在**行首勾位**，未选中行也留位 |
| `combobox` 搜索框 | Text Field | r5、5% 面、行首放大镜、占位 25%（原来是全宽灰条 + 1px 边框） |
| `dialog` / `alert_dialog` | Alert | r12、**无描边**、无右上角 ×；标题 Headline 13 **Bold**；正文 13 |
| `toast` | 推导（通知条） | r10 + Secondary 投影；去掉左侧色带，告警改用文字颜色 |
| `accordion` | Disclosure Button | 行高 24（原 38），**实心三角**（折叠朝右/展开朝下），悬停 5% |
| `empty_state` | 推导 | 去掉虚线占位框，只留居中说明 |
| `item` | List Item | 行内边距 10、r5、副标题 11px；新增 `Style().row_lines()` 画行底线 |
| `nav_item` | Item/Small/Selected | 选中 = 强调色 23% 叠加（实测像素 `#173852`，深色窗口上正是 23% 蓝） |
| `pagination` | 推导 | 当前页 = 强调色胶囊 + 反色字；行高 24 |
| `calendar` | 推导 | 选中/悬停正圆（上一轮已改），本轮机检无回归 |

### 8.3 示例本身也是"参数"

控件改对了，示例还写着 28 高度、40 行的手风琴、50 行的列表项，整页仍然不像 mac。
本轮把示例里的控件高度统一到 `Global/Height Medium = 24`，列表项 44（两行）/
24（单行），弹层触发器 24，代码块里的示例代码同步更新。

### 8.4 新增的工具

- `tools/digest.js`：给定符号名，直接打印"尺寸/圆角/平滑/填充/描边/阴影 + 前两层
  子图层的相对几何与文字"，输出控制在一屏内，供逐控件比对时随手查。
- `.workbuddy/px.py`：读 gallery 的 PNG（filter 全 0）按坐标取色，用来验证
  "23% 而不是实心蓝"这类肉眼看不出的事。

### 8.5 仍与官方有差距的地方

- kit 里的 `Push Button` 有 5 档尺寸（Mini…XL），本库只按 box 高度取 Medium 一档
- `Switch` 的按下形变（滑块横向拉长到 50×31）未做
- 液态玻璃（按约定跳过）

---

## 九、第四轮：投影渲染器（"雷霆阴影边框"）

### 9.1 症状

下拉/菜单/右键菜单/弹层打开后，四周是一圈**深灰发黑的硬边**，边缘 α 约 56%，
往外 16px 才淡出 —— 看起来像给面板加了一道"描边"，而不是投影。

实测（`0 0 3 rgba(0,0,0,.55)` + `0 4 20 rgba(0,0,0,.25)`，白底）：
贴边像素 `#6D6D6D`（109 / 255），且 3px 内从 209 直接掉到 109，是硬台阶。

### 9.2 根因：把 Figma 的 blur 当成了"衰减距离"

旧 `raster_shadow`：

```cpp
if (d >= blur) continue;
const float t = d <= 0 ? 1.0f : (1.0f - d * inv_blur);
cv.blend(x, y, c, t * t);        // 轮廓上 t = 1 → 满 alpha
```

Figma / CSS 的 drop shadow 是"把形状**高斯模糊**后垫在下面"：σ = blur/2，在形状
轮廓线上，模糊后的覆盖率**正好是一半**，往内收敛到 1、往外衰减到 0。旧实现让轮廓
线取满 alpha，再花 `blur` 像素衰减到 0 —— 于是同一条 `0 0 3 .55`：

| | 轮廓处 α | 可见宽度 | 观感 |
|---|---|---|---|
| 应该 | 27.5% | ~3px 高斯拖尾 | 一层贴边细晕 |
| 旧实现 | 55% | 3px 平方衰减 | **一圈实心黑描边** |

两层叠起来（0.55 与 0.25 都取满值）边缘 α = 66%，就是用户看到的那道黑框。
`dy` 也被放大了一倍：轮廓上就开始算，等于阴影整体往外顶了 `dy/2`。

### 9.3 改法

`detail::shadow_cov(u)`：按 `u = d/blur` 查表求高斯 CDF（logistic 近似 Φ，
`1/(1+e^{3.404u})`，误差 < 0.5%，65 项 LUT，无逐像素 exp）：

```cpp
u = 0  → 0.500      // 轮廓线：半覆盖
u = 1  → 0.032      // 模糊半径外
u = −1 → 0.968      // 形状内部
```

`raster_shadow` 只负责 `cov = shadow_cov(d / blur)`，拖尾截断到 `1.35·blur`。

### 9.4 紧轮廓层：0 0 3 / 55% 进一步压到 16%

按 9.3 忠实渲染后，边缘 α = 36%（`#9A9A9A`），仍然是"可见的一圈暗边"——
因为 20px 的柔光对 24 行高的菜单来说**半径太小**，光晕撑不开，眼睛就把它读成边框。

AppKit 的真实观感是：没有一圈可辨的暗边，层次全靠大半径柔光，边缘定义交给面板
自己的 1px 细线。所以 `pop_shadow` 改成：

| 层级 | Light | Dark |
|---|---|---|
| 0 菜单/右键 | `0 .5 5 rgba(0,0,0,.16)` + `0 5 30 rgba(0,0,0,.20)` | 1px `#000` 描边 + `0 5 30 rgba(0,0,0,.47)` |
| 1 弹层/浮卡 | `0 .5 5 rgba(0,0,0,.16)` + `0 10 44 rgba(0,0,0,.22)` | 1px `#000` 描边 + `0 10 44 rgba(0,0,0,.55)` |
| 2 模态 | `0 1 6 rgba(0,0,0,.22)` + `0 30 90 rgba(0,0,0,.32)` | 1px `#000` 描边 + `0 30 90 rgba(0,0,0,.59)` |

`Dark` 的 `0 0 1 spread 1 #000` 用**1px 描边**画（`r.expand(0.5)`），不能拿
`blur = 1` 的柔光冒充：高斯之后边缘只剩半格，暗色下"切"不出面板。

同样的下拉，改完贴边 α 从 56% 降到 13%，渐变从 3px 硬台阶变成 ~30px 连续过渡。

### 9.5 取证方法（可复用）

```bash
# 打开态截图（AINO_OPEN 钩子直接置 flags[id]，不靠模拟点击）
python .workbuddy/shot_open.py Select shot.png select
# 沿一行取灰度，看"贴边值 + 过渡宽度"
python .workbuddy/png2px.py shot.png row 360 395 440
# 放大 6 倍看圆角（阴影最脏的地方就是圆角）
python .workbuddy/crop.py shot.png crop.png 395 285 520 360 6
```

判定标准：贴边灰度与面板底色的差 ≤ 15%（浅色下约 `#D0D0D0` 以上），过渡宽度
≥ 20px 且单调 —— 出现"前 3px 陡降"就是又画成描边了。

---

## 十、第五轮：gallery 自己也得是 mac 的

控件改到位了，容器还是个文档站：没有标题栏、没有红绿灯、侧边栏顶上是网站
logo，页头横着一条分隔线，示例外面套的是"卡片"。这轮改的是**外壳**。

### 10.1 版式对照

| | 之前（shadcn / Fluent 文档站） | 现在（macOS App） |
|---|---|---|
| 窗口 | 无标题栏，内容从 y=0 铺起 | 52 高的标题栏：红绿灯（12 直径、间距 20）+ 居中的窗口标题 + 右侧外观分段控件 |
| 侧边栏 | 232 宽，顶部是 "aino / 单头文件 GUI 库 · 0.4.14" 品牌块 | 整窗高 220 宽，顶部是搜索框，下面是 source list；版本号挪进「关于」框 |
| 导航 | 组头三角在右边，条目 24 高 | 三角在**左**边（Finder / Xcode 的位置），条目 26 高、13pt，选中 = 强调色 23% |
| 页头 | 20pt 标题 + 12.5 说明 + **一条贯穿的 1px 线** | Title 1（22 Bold）+ Body（13）；不画线，靠留白分级 |
| 元信息 | 右上角 "shadcn/ui · Dropdown Menu" | 右上角 "AppKit · NSPopUpButton"（41 页全部换成系统控件名） |
| 概览页 | 2×2 白卡片 + 描边 + 品牌色块 | 「关于 / 快速开始 / 分类 / 实测」四段，每段 = 11 Semibold 灰标题 + Group Box |
| 源码块 | 与预览区等高，一行代码也撑成 200px | 按内容定高，左边缘仍对齐 |
| 示例高度 | 声明值当上限，37/50 条偏大（最多多 72px） | 只用实测高度，声明值降级成兜底 |

### 10.2 为什么红绿灯只做"红"

`Window` 有 `close()`，没有最小化/全屏。红点了真关（`a.quit` → `win.close()`），
黄绿就只是画着的圆 —— 画一个能点却什么都不做的按钮，比不画更糟。

### 10.3 顺手接的三件事

- **搜索**：侧边栏搜索框过滤 `kPages`，整组没命中的连组头一起消失；搜索期间强制
  展开（否则"搜到了却在折叠组里看不见"）。
- **侧边栏收展**：工具栏左端的 `sidebar.left` 图标切换，宽度走 200ms 补间，
  内容区跟着动；收起时按钮让到红绿灯右边（`max(nav_w + 14, 92)`），否则压在灯上。
- **截图钩子**：`AINO_QUERY=<词>` 预置搜索词、`AINO_NAV=0` 预置收起 —— 脚本点不动
  输入框，只能让程序自己带上初始状态。

### 10.4 两个踩到的坑

- `Stack::part(i, n)` 是"n 等分里的第 i 份"，不是"第 i 份占多少"。写成
  `part(0,3) / part(1,3)` 想拿 60/40，实际各得 1/3，右栏下面空一大片。
- 概览页不滚动，所以高度必须自己夹：`min(area.bottom() - y, 内容自然高)`，
  代码块超了会自己截断（`code_block` 按 box 高度截），不会顶穿页底。

---

## 十一、第六轮：内容区也是文档站

上一轮只改了外壳（标题栏 / 侧边栏 / 页头），内容区还是 shadcn 的
「左预览底板 + 右源码块」两栏并列。macOS 里没有这种东西。

### 11.1 结构对照

| | 之前 | 现在 |
|---|---|---|
| 一条示例 | 左预览底板（自带底色 + 圆角）+ 右源码块，两块并列 | 一个 Group Box：表头行 → 细线 → 预览 → 细线 → 源码 |
| 表头 | 标题在卡片上方，说明右对齐在 45% 处 | 表头**在框内**：披露三角 + 13 Semibold 名字（左）／11 说明（右），下行内缩 14 的细线 |
| 源码 | 永远可见，和预览并排，宽度 42% | 表头整行可点展开／收起；默认只展开当页第一条 |
| 预览宽度 | 页面宽 − 源码列 − 40 ≈ 460 | 页面宽 − 40 ≈ 776（表格、日历这类终于铺得开） |
| 示例高度 | body_h + 56 + 18 | box_h + 18，box_h 由实测内容算 |
| 概览页 | 不滚动，窗口一矮代码块压到按钮行上 | 两栏走 `ui.scroll`，高度先算总量再画 |

### 11.2 为什么把源码收起来

「预览 + 常驻源码」是文档站的默认动作。macOS 的对应物是**披露三角**：
一进来看到的是干净的组件列表（像系统设置），想看用法点开表头。默认展开
当页第一条，让「可以点开」这件事自己说出来；`AINO_CODE=1` 全展开供截图。

### 11.3 一个坐标坑

`ui.scroll()` 里 `ui.row()` 返回的是**已减掉偏移**的屏幕坐标，而
`fill_rect` / `label` 这些吃的是屏幕坐标、不会自动跟着滚。所以绝对坐标
排版的页面（概览页）要用 `ui.scroll_y()` 自己减：

```cpp
auto sc = ui.scroll(area, total);
const float off = ui.scroll_y();
float y = area.y - off;      // 不减：滚轮一动，内容不动，只被裁剪
```

### 11.4 高度必须在进滚动作用域**之前**算完

概览页两栏是「从上往下排」的确定高度，所以第一遍只算数、第二遍才画。
之前是边画边 `min(剩余高度, 自然高)` 夹一下——窗口 760 高时没问题，
截图的 700 高就露馅：18 行代码直接画到按钮行里。现在总量已知，夹的逻辑
整个删掉了。
