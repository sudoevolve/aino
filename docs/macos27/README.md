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

---

## 十二、半透明 token 不能直接量亮度

### 12.1 症状

- 头像：浅色主题下首字是白的，落在近乎白的圆上 —— 什么也看不见。
- 评分：未选中的空星几乎隐形。
- 函数显示器 / 侧边栏示例：侧边栏是一片 `#BBBBBB` 的中灰，比内容区还暗。

三处看着无关，其实是同一个错误：**拿带 alpha 的颜色当不透明色用**。

### 12.2 根因

`th.muted`（`rgba(0,0,0,13)`）、`th.label2`（`rgba(0,0,0,128)`）这些 token
是"叠在别的东西上"的填充/文字色。`Color::luminance()` 只算 RGB，不看 alpha：

```cpp
Color fg = bg.luminance() > 0.55f ? th.foreground : Color(255,255,255);
// bg = rgba(0,0,0,13) → luminance() = 0.0 → 判成"暗底" → 白字
// 实际压在 #FFFFFF 上是 #F2F2F2，白字当然看不见
```

侧边栏那处是另一种形态：`th.background.mix(th.muted, 0.55f)`。`mix` 连 alpha
一起插值，得到"45% 白 + 55% 黑"再带 ~48% alpha，落在窗口上就是 `#BBBBBB`。

### 12.3 改法

新增 `Color::over(base)`：把半透明层压到底色上，得到"实际看到的颜色"，
然后才允许调 `luminance()`。

```cpp
const Color eff = bg.over(th.window_bg);
Color fg = eff.luminance() > 0.55f ? th.foreground : Color(255, 255, 255);
```

- 头像默认底也从 `th.muted`（5%）提到 `th.fill`（10%）：5% 的圆在浅色主题下
  只剩一圈描边，10% 才像个"没照片"的占位。
- 空星的 `if (empty_c.luminance() > 0.82f)` 这个判断以前**从来没生效过**
  （半透明色亮度恒为 0），压平之后才会真的退到 `th.border`。
- 侧边栏一律用 `th.sidebar_bg` / `th.separator` 这两个 token，不再现调颜色。

### 12.4 顺手对齐的其他 demo

| | 之前 | 现在 |
|---|---|---|
| 函数显示器 · 侧边栏 | `background.mix(muted,.55)` → `#BBBBBB`；17pt App 名 + 副标占三行；类别选择器 34 高；条目 30 高 | `sidebar_bg`；顶部只放类别选择器（24 高），细线，条目 26 高 13pt |
| 函数显示器 · 内容标题 | 19pt | Title 1：22 Bold |
| 侧边栏示例 | 同一个 `mix` 写法；条目 34 高 | `sidebar_bg`；11 Semibold 分组标题 + 26 高条目 |

SSH 客户端本来就是深色 + 卡片，没有这个 token 误用，未改动。

---

## 十三、第七轮：全量复检（组件 + 版式）

这一轮不看"像不像"，而是把 kit 的符号逐个 digest 出来对参数。工具：
`tools/catalog.js`（列出 kit 里 3429 个符号的顶层分类）与
`tools/digest.js`（打印某个符号的尺寸/圆角/填充/描边/阴影 + 前两层子图层）。

```bash
NODE_PATH=.workbuddy/figma/node_modules node docs/macos27/tools/catalog.js Slider
NODE_PATH=.workbuddy/figma/node_modules node docs/macos27/tools/digest.js "Window/Segmented Control/Medium/3/2"
```

> 注意 `node_modules` 在 `.workbuddy/figma/`，直接 `node tools/digest.js` 会报
> `Cannot find module 'kiwi-schema'`。

### 13.1 仍然带 Web 味的 21 处（已改）

| # | 组件 | kit 依据 | 之前（Web / shadcn） | 现在（macOS 27） |
|---|---|---|---|---|
| 1 | 标签栏 `tabs()` | `Tabs/8`、`Utility Panel/Tabs` | 整条 1px 下划线 + 会滑动的 2px 蓝色指示条，项等分 | 无下划线；选中项浮一层 3% r6 圆角底，项宽按文字宽 |
| 2 | 分段控件 | `Window/Segmented Control/Medium` | 强调色实心 + 白字，r6 方角 | 胶囊轨道（r = h/2）+ 白色胶囊（暗色 #2E2E2E 级）+ 主标签字 |
| 3 | 搜索框 | `Search/Medium`（r=100） | r6 方角输入框 + 框外放大镜 | 胶囊 + 放大镜在框内 12px 处 |
| 4 | 滚动条 | — | 3px 常驻细条，永远挂着 | 7px 圆头 overlay：滚动/悬停出现，0.9s 淡出（`scrollbar_alpha`） |
| 5 | 菜单行悬停 | `Menu Item … State=Hover` | 强调色 5% + 文字转白 | 中性灰 r8（`#E6E6E6` / `#141414`），文字不变 |
| 6 | 菜单面板 | 同上 | r10 | r12（与行 r8 同心） |
| 7 | 下拉按钮 `select()` | `_Alert Button/Pop-Up Button` | 白底 + 1px 边框 + 单个向下箭头 | 8% 实心、无描边 + **上下双箭头** |
| 8 | 折叠面板 | `NSDisclosureGroup` | 三角在行尾（Web 手风琴） | 三角在**行首**，标题缩进 22 |
| 9 | 焦点环 | HIG：3pt、画在控件外 | 2px、外扩 1.5、实际只有 27% 强调色 | 3px、外扩 2、42% |
| 10 | 提示气泡 | `Tooltip 49×18` | 12px + 8/5 内边距 + r6 + 1px 边 | 11px + 6/3 + r5，浅色无描边 |
| 11 | 抽屉 `drawer()` | `Sheet`（r10） | 整屏 35% 黑幕 + 方角 + 1px 边 | **无黑幕** + r10 + 二级投影 |
| 12 | Group Box | `Group Box`（r12、1.5% 底） | r10、不铺底 | r12 + 2% 底 + 1px 细线 |
| 13 | 示例栏宽 | — | 单控件被拉满 ~790px | 表单类 400（`demo_col`），表格/日历/图表仍整宽 |
| 14 | 验证码 `input_otp()` | 由 `Text Field` 推导 | 每格跟着 box 撑到 36 高、15 号字 | 24 高 / r5 / 13 号字，格子垂直居中 |
| 15 | 示例里的控件高 | Medium = 24 | 布局示例 36、toggle 34、分页 24 混用 | 全部 24（源码片段里的数字一起改） |
| 16 | 切换组 `toggle_group()` / `toggle_group_multi()` | `Window/Button Group`：胶囊轨道 + `#E6E6E6` 胶囊 | 强调色实心 + 白字（shadcn ToggleGroup） | 8% 凹槽轨道 + 白色胶囊，非选中 50% 字 |
| 17 | 按钮组 `button_group()` | 同上 | `secondary` 底 + 1px 外框 + 通高竖线 | 8% 凹槽、**无外框**，段间 1×14 细缝 |
| 18 | 分页当前页 | `Item/Small/Selected`（强调色 23%） | 实心蓝圆点 + 白字（iOS 页码点） | 强调色 23% 胶囊 + 主标签字 |
| 19 | 悬停光标 | — | 按钮/菜单/列表/日历格全部变手型 | 只有 `Style::Link` 变手型，其余保持箭头 |
| 20 | 焦点环 | HIG：只在键盘焦点时出现 | 鼠标点过的控件也留一圈蓝（`:focus-visible` 的反面） | 记录 `focus_by_mouse`，非文本控件只在键盘焦点时画；输入框仍点击即画 |
| 21 | 侧边栏图标 | `Item/Small`（18×18 图标 + 文字） | 纯文字条目 | 按分组给描边字形（圆环 / 方框 / 输入框 / 三行 / 两块错开 / 两栏），50% 与 85% 两档 |

第 11 条要单独说明：macOS 的 sheet / 检查器**不会把窗口压黑**（窗口只是失去
焦点），整屏半透明黑是 Radix / shadcn 的 Dialog Overlay。层级交给圆角 + 投影。

### 13.2 又一个 `with_alpha` 的受害者

函数显示器的网格线之前是 `th.border.with_alpha(150)`。`th.border` 是 10% 黑，
`with_alpha` **替换** alpha → 59% 的深灰，图纸上像用尺子画的 Excel 表。
改成直接用 `th.border`（10%）+ 坐标轴 `th.label3`（25%）之后，才像 Swift Charts。

这是同一个陷阱的第三次出现（前两次：头像白字、空星隐形）。判断"多深"时只能
用 `scaled_alpha`，`with_alpha` 只适合"我就是要这个绝对值"。

### 13.3 版式上确认"对"的几处

- 侧边栏条目：`Item/Small/Selected` = 24 高、r5、**强调色 23%** 底、字仍 85%
  —— 之前一度被写成"实心蓝 + 白字"（那是 iOS），现在与 kit 一致。
- 表格：表头 11 Semibold 50%、行高 20、只有行分隔线、数字列连表头一起右对齐。
- 日历：选中日是正圆（不是圆角方块），表头 13 Semibold 居中。
- 按钮：24 高、8% 底、无描边、13 Medium 85%（`Buttons/Bordered`）。
- 文字层级：22 Bold / 15 Semibold / 13 Regular / 11 / 10。

### 13.4 还差的（有意为之）

- 液态玻璃：`Liquid Glass` 符号是整块玻璃材质（多层填充 + GLASS 效果），
  本库没有背景模糊，按最初的约定跳过。因此分段控件用 8% 灰轨道替代玻璃轨道。
- 侧边栏的图标是**按分组**画的描边字形，不是 41 页各一枚 SF Symbol。
  `Item/Small` 里是每项一枚 18×18 图标；要做到那一层需要一张 41 项的图标表。

### 13.5 补记：`group_cell` 是同一个 bug 的第三个出口

`toggle_group` / `toggle_group_multi` / `button_group` 共用 `detail::group_cell`，
所以"选中 = 强调色实心 + 反色字"这条错误**一次修好三处**。现在这个函数里只剩
两种面：白色胶囊（选中）和 4% 灰（悬停），与 `segmented()` 完全一致 ——
它们在 AppKit 里本来就是同一个 `NSSegmentedControl`。

`toggle_button` 单独保留 22% 的强调色底：kit 的 `Buttons - Toggle/Bordered - Tinted`
里选中态标签仍是 85% 黑（不是白字），说明那是"淡色底"而不是实心强调色。

---

## 十四、交互「卡卡的」：不是帧率低，是帧间隔被系统时钟钉住了

### 14.1 症状

鼠标划过控件、拖滑块、展开面板时，画面一顿一顿的。目测像 20~30fps，
但"动画是否顺滑"这件事不能靠目测 —— 先量帧间隔。

### 14.2 量：一帧走多久、隔多久

给 gallery 加两个诊断钩子（库里的 `AINO_FRAME_LOG` 只打渲染耗时，
看不到帧与帧之间的空档）：

```bash
# 真实主循环连排 N 帧，记录每帧开始时刻 → 帧间隔分布
AINO_BENCH_RUN=90 AINO_FRAME_LOG=out.txt build/Release/aino_gallery.exe
# 分项：填充/描边/文字/圆各占多少
AINO_PROF=out.txt AINO_BENCH_RUN=70 build/Release/aino_gallery.exe
```

第一次量出来的数字：

```
帧间隔 均 30.25 ms (33.1 fps)   >20ms 86 帧 / 90 帧
序列 ms: 14 28 28 32 31 32 31 32 33 29 32 30 ...
```

**帧间隔整齐地停在 28~32ms** —— 这不是"绘制慢"的形状（那会是抖动的长尾），
是被某个 15.6ms 的东西量化了。

### 14.3 根因一：`Sleep` 的 15.6ms 粒度（影响最大）

主循环的节流是这样写的：

```cpp
const uint64_t budget_us = uint64_t(budget_ms) * 1000u;   // 16ms
if (el_us < budget_us) Sleep(DWORD((budget_us - el_us + 999) / 1000));
```

而 Windows 默认时钟粒度是 15.6ms。本机实测：

| 调用 | 实际睡多久 |
|---|---|
| `Sleep(1)` | 15.6 ms |
| `Sleep(5)` | 15.6 ms |
| `Sleep(15)` | 16.0 ms |
| `Sleep(16)` | **30.9 ms** |

于是"补足 16ms 预算"这条逻辑整个失效：绘制 12.5ms 的一页 + `Sleep(4)`
= 15.6ms 的等待 → 28ms/帧。也就是说**画面被系统时钟钉在 33fps**，
而渲染其实还有 4ms 余量。

改法：`CreateWaitableTimerExW` + `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`
（Win10 1803+），精度约 1ms，**只影响本进程等的那一下**。不用
`timeBeginPeriod(1)` —— 那是全局把系统时钟中断调快，代价是待机耗电。

```cpp
HANDLE h = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /*HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
LARGE_INTEGER due; due.QuadPart = -LONGLONG(us * 10);   // 相对，100ns 单位
SetWaitableTimer(h, &due, 0, nullptr, nullptr, FALSE);
WaitForSingleObject(h, INFINITE);
```

结果：**30.25ms → 16.27ms（33.1 → 61.5 fps）**，最差 18.28ms，没有一帧超过 20ms。

### 14.4 根因二：每帧 12.7ms 的绘制

帧调度修好之后余量只剩 4ms，概览页（14.7ms）还是会掉帧，所以得把绘制本身压下来。

`AINO_PROF` 分项（Button 页，1180×760）：

| 项 | 耗时 | 量 |
|---|---|---|
| 填充 | 6.86 ms | 71 次 / 2064 kpx（301 Mpx/s） |
| 文字 | 3.19 ms | 100 段 / 853 字（31.9 µs/段） |
| 描边 | 0.44 ms | 57 次 |
| 上屏 | 0.77 ms | BitBlt |

**填充是最大头，而且慢得没有道理**：2064 kpx 里大部分是"半透明的大面积"——
Group Box 的 2% 底、代码块底、侧边栏材质。这些像素走的是圆角矩形的"实心内区"
路径，那里对 `alpha<255` 是**逐像素调 `blend()`**：

```cpp
for (int x = x0; x < x1; ++x) blend(x, y, c, 1.0f);   // 每像素：边界+裁剪+浮点混合
```

行裁剪、左右边界、`sa` 的浮点换算每像素都重算一遍。改成行级：

```cpp
// 裁剪/量化只做一次，循环里只剩三次整数乘加（alpha 用 0..256 定点）
int sa = int(float(c.a) * coverage * (256.0f / 255.0f) + 0.5f);
uint8_t* p = px + y * stride + x0 * 4;
for (int i = x1 - x0; i > 0; --i, p += 4) {
    p[CH_R] = uint8_t((sr + p[CH_R] * inv + 128) >> 8);
    ...
}
```

**填充 6.86 → 2.06 ms（301 → 1003 Mpx/s）**，绘制 12.65 → 6.68 ms。

### 14.5 文字为什么只降了 3%

先试了"缓存 GDI 状态"（字体/颜色/背景模式在相邻两段之间 90% 相同，
省掉每段 6 次 `SelectObject`/`SetTextColor`/`SetBkMode` 往返 + 还原）：
文字 3.19 → 3.02ms。

**几乎没动** —— 说明那 30µs/段不是状态往返，而是 `ExtTextOutW` 本身
（GDI 要锁 DIB、跑 hinting/AA）。想再快只能自己做字形缓存
（`GetGlyphOutline` + 小位图着色），那是另一个量级的工程，
本轮不做。状态缓存留着（便宜且无害），但别指望它。

顺带记一个坑：`Win32` 下 `stderr` 在 **WIN32 子系统**程序里是无效句柄，
`AINO_FRAME_LOG=1` 一行输出都没有。所以诊断支持"值不是 `1` 就当成文件路径"：

```bash
AINO_FRAME_LOG=build/frames.txt build/Release/aino_gallery.exe
```

### 14.6 结果

| | 之前 | 之后 |
|---|---|---|
| 帧间隔均值 | 30.25 ms | **16.27 ms** |
| 帧率 | 33.1 fps | **61.5 fps** |
| 最差帧 | 37.6 ms | 18.3 ms（无 >20ms） |
| 绘制（Button 页） | 12.65 ms | **6.68 ms** |
| 填充吞吐 | 301 Mpx/s | **1003 Mpx/s** |
| 概览页绘制 | 14.74 ms | 9.54 ms |

代价：60fps 的交互比 33fps 贵（扫导航时单核 40~60%，16 核约 3%）。
想省电用 `set_move_fps(30)`，别把它当性能指标。

### 14.7 顺手抓到的一个真 bug：步进器上下反了

`stepper` 的 `minus_r` 拿的是**上半**、`plus_r` 拿的是下半 ——
点上面的三角数字变小。NSStepper 从上到下永远是 ＋ / －。
改完之后 `interaction_probe` 的"点上部三角 +1 / 点下部三角 -1"才立得住。

### 14.8 这一轮顺带修的过时断言

改版式之后一批断言还在按旧几何/旧 token 判，全都会假红。都改成"跟着 token 走"：

| 断言 | 旧假设 | 现在 |
|---|---|---|
| panel 中心色 | 写死 `card`（0x252525） | `fill5.over(bg)` |
| 滚动条宽 | 3px 常驻 | 7px overlay / 热区 15px |
| 阴影"blur 外透明" | 距边 `blur` | `1.4×blur`（高斯拖尾） |
| 遮罩基线 | 写死 110 | `overlay.over(白)` |
| 菜单行高 | 32 | 24（kit Menu/Height） |
| tabs 点击位置 | 等分 `w/3` | 按文字宽（`text_width + 36`） |
| 组控件/步进器点击 | 左半减、右半加 | 胶囊分段 / 右侧竖条上+下− |
| 头像底色 | `muted`（5%） | `fill`（10%），且必须**先铺白底**再比 |
| 交互 CPU | 光标横扫的 A/B 比值 | 帧日志的**帧数**（3 秒内扫导航 ≥2 块、同行 0 块） |

最后一条要单独说：`move_probe` 原来拿"单核 CPU 百分比"判红绿，
而这台机器上同一个二进制能读出 3% / 19% / 43%（窗口被遮挡、DWM 合成、
后台进程都会掺进来）。现在改成读帧日志数**帧块**（每 60 帧一块）——
"同一行抖动 3 秒 = 0 块"才是这条断言真正要证明的东西。
本地自动化环境抢不到前台（`SetForegroundWindow` 失败、`WindowFromPoint`
返回别的窗口）时明确 **SKIP** 并打印原因，不判红也不伪造。

### 14.9 取证命令

```bash
# 帧间隔（真实主循环）
AINO_BENCH_RUN=90 AINO_START_PAGE=Button AINO_FRAME_LOG=o.txt build/Release/aino_gallery.exe

# 分项
AINO_PROF=o.txt AINO_BENCH_RUN=70 AINO_START_PAGE=Button build/Release/aino_gallery.exe
```

---

## 十五、第八轮：把 INSTANCE 解到主组件，逐控件重对一遍

这一轮的关键不是"再看一遍截图"，而是补上了一个取数能力：**Figma 的实例不存
圆角/填充，存在主组件上**。之前的 `digest.js` 只能读到实例这一层，所以分段控件
的选中段底色、圆角一直是"看着像"估出来的。新写的
`.workbuddy/figma/inst.js` 顺着 `symbolData.symbolID` 往主组件跳一跳，
就能拿到真实值：

```bash
cd .workbuddy/figma && NODE_PATH=node_modules MAXD=3 node inst.js "Segmented Control/3/1/True"
```

第一行就推翻了上一轮的结论。

### 15.1 最大的一个错：分段的 True/False 不是"选中/未选中"

kit 里 `Segmented Control/3/1/True` 和 `/3/1/False` 是**两个变体**，而 `1/2/3`
才是"第几段被选中"。跳进主组件才看清 `True/False` 指的是 **Active（窗口是否活动）**：

| 符号 | 选中段 BG | 标签 | 未选中标签 |
|---|---|---|---|
| `/3/1/True`（活动窗口） | `#0088FF` + 3% 黑 | `#FFFFFF` | `#000000D9` |
| `/3/1/False`（非活动窗口） | `#0000001A` | `#000000D9` | `#000000D9` |

上一轮照着 `Window/Segmented Control`（工具栏那套**玻璃图标按钮**，选中是
`#E6E6E6` 玻璃胶囊、内容是 SF Symbol）把内容区的文字分段改成了"白胶囊 + 主标签字"
—— 那是另一个控件。内容区的分段在活动窗口里就是**强调色实心 + 白字**，
而未选中标签是 **85%**（三段一样深，选中只靠那块蓝），不是 50%。

### 15.2 这一轮的逐项修正

| # | 组件 | kit 依据（真实值） | 之前 | 现在 |
|---|---|---|---|---|
| 1 | 分段控件选中段 | `Segmented Control/3/1/True`：BG `#0088FF`、Label `#FFFFFF` | 白胶囊 + 主标签字 | 强调色胶囊 + 白字 |
| 2 | 分段未选中标签 | 三段皆 `#000000D9` | `label2`（50%） | `label`（85%） |
| 3 | 按钮组 / 切换组选中 | `Button Group/3`：实体 `#E6E6E6` 胶囊（玻璃轨道上**压深**一档） | 纯白胶囊 | `#E6E6E6` / 暗色 `#3A3A3A` |
| 4 | 滑块轨道 | `Slider/Idle/50%`：Track `150×4`、r2、两道内阴影 | 6px | **4px** |
| 5 | 单选圆环 | `Type=Radio Buttons`：`16×16`，中心点 `6`，标签 13 Medium | 15×15，点直径 8.3，选中换 500 字重 | 16 + 6，字重恒定 |
| 6 | Group Box（`panel`） | `Group Box`：r12、`#00000008`@50%、**图层里没有任何 stroke** | 2% 底 + 1px 分隔线 | 5% 底（`Fills/Tertiary`）、**无描边** |
| 7 | 对话框 | `Alert/3 Options + Cancel`：260×360、r10、pad 16/20、标题 13 Bold、正文 **11 Regular 85%**、按钮**通宽 28 高竖排**、取消 `#E6E6E6` 胶囊、**无遮罩** | r12、右下两颗 36 高胶囊、正文 13/50%、整屏 35% 黑幕 | 按 kit 重排 |
| 8 | Popover | `Popover`：容器 r20（玻璃）、Content Area pad **20/20** | 面板 r4 + 1px 描边 | 面板 r12 + pad 20、只留投影 |
| 9 | 输入框占位符 | `Text Field/Rounded`：Placeholder `#000000`@0.25 | 50%（`muted_foreground`） | 25%（`label3`） |
| 10 | Combobox 触发器 | `Combo Box/False`：纯白底 + 两道向下投影，**没有 1px 硬边框** | `theme.input` 深边框（1~1.4px）+ 焦点时染蓝 | 白底 + 内阴影 + 分隔线色细边 + 3px 焦点光环 |
| 11 | Toast | — | 一圈 1px 描边 | 只有投影 |
| 12 | 进度条 | `Determinate/Medium`：`100×10` 胶囊 | 示例给 8 高 | 10 |
| 13 | 下拉按钮圆角 | `Window/Pop-Up Button` r6 | `radius_sm`（4） | `theme.radius`（6） |
| 14 | 输入组（prefix/suffix） | 同 `Text Field` | 白底 + `input` 深边框 | 与 `input()` 同一套皮 |

第 6 条要单独说：kit 的 Group Box 裸值是 **1.5%**（3% 再乘 50% 不透明度），
在 `#FFFFFF` 上等于 `#FBFBFB` —— 白底上根本看不见。所以容器取系统的
"grouped background"（`Fills - Opaque/Tertiary`，5%），暗色下也正好落在
`#1E1E1E → #292929` 这一档。剥掉那圈 1px 描边之后，示例卡不再是"白卡片 + 灰底
+ 边框"的 Web 三件套，而是一块比页底深一档的面 —— 这才是系统分组的表达方式。

### 15.3 连带修好的三处判定

- **`widget_probe` 的对话框点击点**：按钮从"右下角 36 高胶囊"变成"通宽 28 高行"，
  点击坐标必须跟库同源算，不能写死。
- **`verify_gallery` 的对话框三条断言**：「整窗变暗 / 面板比遮罩亮 / 点遮罩后变亮」
  全部作废 —— 遮罩已经没有了。换成「导航亮度不变（不压暗）」「面板内一条竖带的
  同色占比 ≥0.95（是一整块实心面）」「点面板外后该占比掉 0.25 以上（面板消失）」。
  用平均亮度判"面板在不在"是错的：面板 `#F6F6F6`（246）和示例卡 `#F2F2F2`（242）
  只差 4 级，平均下来分不开。
- **`ci_pixels` 的 S3 面板断言**：期望值从 `fill5` 改成 `fill3`。
- **`shadow_text_probe` 的阴影基线**：原来把 `overlay` 的压暗量当基线（50），现在
  基线是 0；判据改成"面板右侧 +3px 变暗 >8、+34px <40"，量的是投影本身。

### 15.4 验证

```bash
python tools/regress.py          # 49 项 / 0 失败（1 项环境 SKIP）
python tools/verify_gallery.py   # 16 项 / 0 失败
```

像素实测（浅色）：按钮组选中胶囊 `#E6E6E6`、卡片面 `#F2F2F2`（页底 `#FFFFFF`）、
分段选中段 `#0088FF` 且段内 215 个近白像素（白字）、进度条 10 高、滑块轨道 4 高。

### 15.5 仍然没做

- 液态玻璃（按约定跳过）：分段轨道用 8% 凹槽替代玻璃，Popover 用 r12 实心面
  替代 r20 玻璃。
- 侧边栏逐项 SF Symbol：`Item/Small` 是每项一枚 18×18 图标，现在按分组画字形。

---

## 十六、第九轮：外壳（标题栏/侧边栏）与"同一套皮"

上一轮把控件本体的数值对齐了，这一轮补的是**外壳**和**跨控件一致性**——
同一个东西在五个地方各画一遍，最后一定有一处是 Web 味。

### 16.1 标题栏：换材质，不是压一根线

原来标题栏铺的是窗口底（`window_bg`）+ 下沿 1px 分隔线。这是网页头部的写法：
`border-bottom: 1px solid #e5e5e5`。

kit 里标题栏是**一种材质**（`Materials - Controls/Title Bar`：`#EAEAEA` / `#3C3C3C`），
系统靠"两块面不同色"分界，不画线。改成 `th.title_bg` 之后，浅色下它正好和侧边栏
（`Materials - Controls/Sidebar` = `#EAEAEA`）连成一条灰带 —— 这就是 macOS 的
统一工具栏。实测：标题栏 `#EAEAEA`、内容 `#FFFFFF`、侧边栏 `#EAEAEA`。

断言也跟着换了：原来是"标题栏下沿有一条 1px 分界线（命中 ≥15/22 个采样点）"，
现在改成"标题栏与内容区的平均亮度差 ≥8"，判的是材质差不是线。

### 16.2 侧边栏行：r8、左右各缩 10

`Item/Small/Selected` 的实际结构是两层：整行 `198×24 r5`，里面的选中块是
**`178×24 r8`**（左右各缩 10，纵向撑满），填充是强调色 23%。

原来行底用的是按钮半径 6、左右缩 8。差 2px 看着不起眼，但一排行叠起来，
整条侧边栏的"圆角节奏"就和系统不一样了。现在 `nav_item` 默认 r8、
画廊的条目盒左右各缩 10，文字从 44 起（图标 20 宽 + 间隔 6）。

### 16.3 推按钮的面是 8%，不是 10%

`Buttons/Bordered` 的 BG 是 `Fills - Opaque/Secondary`（**8%**）。`make_skin` 里
`Secondary` 和 `Destructive` 之前都取 `fill`（10%）—— 一整排按钮会比工具栏重一档。
README 第二节那张表写的是 8%，代码写的是 10%，两处对不上已经很久了。

### 16.4 `textarea` 之前是另一个控件

多行输入框还留着旧皮：`th.background` 底 + `th.input` 深边框 + 焦点时把边框
染蓝加粗（`bw + 0.4`）+ 圆角 4。这是 Web 的 `<textarea>`。

它跟 `input()` 本来就是**同一个 Text Field 长高了**：现在是同一套皮 ——
`window_bg` 底、顶部内阴影、分隔线色细边、焦点走 3px 光环、占位符 25%。

### 16.5 凡是"浮起来的面"，只有投影

这一轮统一了四处的画法（菜单面板、Popover、Hover Card、Toast）：

| 组件 | 之前 | 现在 |
|---|---|---|
| 菜单面板 | r12 + 1px `separator` 描边 | r12，只留投影 |
| Popover 面板 | r4 + 1px `border` | r12，只留投影 |
| Hover Card | r4 + 1px `border` | r12，只留投影 |
| Toast | r10 + 1px `separator` | r10，只留投影 |

macOS 的浮层靠"材质 + 投影"离开背景，描边只出现在**控件**上（输入框、按钮组），
不出现在**浮层**上。一圈 1px 线会把浮层钉回页面，立刻变成"下拉面板"。

### 16.6 概览页的统计数字改成现算

"组件 47 个 / 回归断言 45 项"是写死的，加了三页规范页、回归涨到 49 项之后
两个数字全错。现在组件页数用 `kPageCount - 1` 现算（前向声明一行，
因为页表在文件后半段），断言数保持字面量并注明"改 `tools/regress.py` 时一起改"。

### 16.7 一条判据的教训：别贴着边缘采样

对话框的"面板是一整块实心面"用的是"一条竖带的同色占比"。第一版贴着面板左边缘
（内缩 5~15px）采，纵向还贯穿了上下圆角 —— 机器忙的时候窗口晚一帧、面板偏 1px，
占比就从 0.95 掉到 0.53，看着像"面板是透明的"。

现在采的是**左侧内边距那一条**（面板 pad 20，正文从 20 开始，所以
`-184..-176` 是纯空白），纵向只取中段（上下各留 30 避开 r10 的弧）。
连跑三次稳定 0.95 以上。

### 16.8 验证

```bash
python tools/regress.py          # 49 项 / 0 失败
python tools/verify_gallery.py   # 16 项 / 0 失败（连跑三次）
```

> 这台机器上 `verify_gallery` 偶发"点了没反应"（窗口抢不到前台，点击被系统吞掉），
> 表现是一整屏采样全等值。同一份代码连跑三次都过，就按通过算；但**同一项连续两次
> 红**必须当成真问题查，不能拿"环境抖动"糊过去。

---

## 十五、第九轮：把"库的面"改成 kit 的组织方式

前八轮改的是**每个控件怎么画**。这一轮改的是**库由哪些页面组成、每页怎么排** ——
因为 Figma 的 kit 不只是一堆组件，它还是一张"macOS 有哪些控件"的清单。抽查
kit 的页面表（`canvas.fig` 的 CANVAS 顺序）得到 30 个组件页：

```
Alerts · Toggles · Color Wells · Combo Boxes · Pointers · Menu Bar and Dock
Disclosure Controls · Forms · Group Boxes · Image Wells · Menus · Notifications
Popovers · Pop-up and Pull-down Buttons · Progress Indicators · Buttons · Dialogs
Search Fields · Segmented Controls · Sheets · Sliders and Dials · Sidebars
Steppers · Lists and Tables · Text Fields · Toolbars · Tooltips · Windows
```

对照之下，本库原来有 45 个页面，其中 16 个在 kit 里**没有对应物**，而且分组的
名字（基础 / 表单 / 数据展示 / 浮层 / 布局）是文档站的分类，不是 AppKit 的分类。

### 15.1 侧边栏：按 kit 的页面顺序重排

| 组 | 页面（字母序，与 kit 一致） |
|---|---|
| 开始 | 概览 |
| 外观 | Colors · Text Styles · Materials |
| 控件 | Buttons · Color Wells · Combo Boxes · Disclosure Controls · Image Wells · Pop-up and Pull-down Buttons · Search Fields · Segmented Controls · Sliders and Dials · Steppers · Text Fields · Toggles |
| 容器 | Forms · Group Boxes · Lists and Tables · Sidebars · Split Views · Toolbars · Windows |
| 浮层 | Alerts · Dialogs · Menu Bar and Dock · Menus · Notifications · Popovers · Sheets · Tooltips |
| 反馈 | Progress Indicators · Scrollbar |

合并的页面（kit 本来就是同一页，AppKit 里也是同一族控件）：

- `Toggles` = Checkbox + Radio + Switch
- `Segmented Controls` = Tabs + Toggle Group + Button Group
- `Text Fields` = Input + Textarea + Input Group
- `Menus` = Menu + Context Menu

### 15.2 删掉的 16 个"没有 AppKit 对应物"的页面

`Badge`、`Avatar`、`Input OTP`、`Calendar`（月视图是 iOS 的日历，不是 NSDatePicker）、
`Pagination`（NSPageController 不是页码条）、`Empty`、`Chart`（Swift Charts 是框架）、
`Breadcrumb`、`Hover Card`（与 Popover/Tooltip 重叠）、`Drawer`（mac 用 sheet）、
`Layout`、`Clip`、`Aspect Ratio`、`Primitives`、`Transform`、`Tween`。

> 前 11 个是**控件**层面的多余（Web 组件库的语汇）；后 5 个是库的**能力**，不是控件 ——
> 它们在 `docs/03-layout.md` / `09-primitives.md` 里有完整说明，只是不该占组件页。

### 15.3 页面版式：Header + section，不再是"卡片 + 代码"

kit 的每个组件页是同一个骨架：一个 Header（大标题 + 一行说明 + 指向 HIG 的链接），
下面若干 **section** —— 只有标题和内容，没有边框、没有底板。

| 元素 | 之前 | 现在（kit） |
|---|---|---|
| 页标题 | Title 1 · 22 Bold | **Large Title · 26 Bold**（1400 宽的 Header 折算） |
| 页说明 | 13 | **Callout · 14**，次要色 |
| 条目 | Group Box（1px 线 + r10）+ 表头 | **section：只有 15 Semibold 标题**，没有说明行 |
| 版面 | 单列铺满整宽 | **等宽多列**（默认 2 列，宽内容 1 列），瀑布式按列高排 |
| 源码 | 默认展开第一条 | 默认全收起，点标题展开 |

> 第一版把内容改成"整宽单列"，结果 400 宽的控件摊在 900 宽的画布上，一页只有一个
> 控件时像没做完。现在是多列：每格 ~430（正好是这些示例原本的宽度），控件成组出现。
> 控件内部怎么排（竖排还是网格）是每个示例自己的事 —— 按钮的六种变体已经改成 3×2
> 网格，其余页面还多是竖排，属于下一轮要逐个改的。

### 15.4 液态玻璃：用"背景模糊"代替折射与色散

kit 的玻璃由 `Depth 30 / Refraction 70 / Dispersion 20 / Frost 6~16 / Opacity 25`
定义，那是逐像素光线模拟。CPU 光栅化的库做不了，这一轮实现的是它**看得见的那一半**：

```
backdrop_blur(rect, radius, blur)
  1. 采样范围外扩 1.5·blur（边缘也要有"外面的内容"可糊）
  2. 降采样 1/2 或 1/4（模糊本身是低通，降采样不改变观感）
  3. 三趟可分离盒式模糊（≈ 高斯，σ = blur/2，代价与半径无关）
  4. 8.8 定点双线性放大 + 圆角 SDF 遮罩写回（直边段走无遮罩快路径）

glass(rect, radius, style)
  = 投影 + backdrop_blur + 25% 冷白材质 + 顶部高光边 + 内侧白雾 + 外描边
```

实测（`tools/probe/blur_bench.cpp`，1180×760 后备缓冲，60 次取平均）：

| 区域 | frost 6 | frost 16 | frost 24 |
|---|---|---|---|
| 170×148（一块面板） | 0.45 ms | 0.29 ms | — |
| 900×300（整条） | 4.39 ms | 2.73 ms | 2.77 ms |
| 940×740（满窗） | — | 6.41 ms | — |

代价只在**重绘帧**付一次；静态页面依旧空闲 0% CPU（回归里的空闲断言仍是 0.00%）。
不做折射与色散 —— 那两样要么上 GPU，要么逐像素光线求交，不属于这个库的取舍。

### 15.5 新增的四个控件

| 控件 | AppKit | 关键实现 |
|---|---|---|
| `search_field` | NSSearchField | 胶囊皮 + 框内放大镜 + 清除徽标；编辑逻辑整套复用 `input` |
| `color_well` | NSColorWell | 48×24，色块 + 内侧白边 + 外侧分隔线（白块也有轮廓） |
| `image_well` | NSImageView | 5% 凹面 + 图片符号 + 说明；不做文件拖放 |
| `sheet` | NSWindow · sheet | 上平下圆：面板往上多画 12px 再裁掉，窗口不压黑 |

### 15.6 这一轮修掉的三处"看着像、其实不是"

| 位置 | 之前 | kit 实际 |
|---|---|---|
| `stepper` 指示符 | 实心小三角 | **chevron**（􀆇/􀆈，20×12，12px Medium 85%） |
| `stepper` 中缝 | 通栏 1px 分隔线 | **14×1、r10、12%**，左右各留 3 |
| `disclosure` 三角 | 实心三角（注释还写着"不是 V 形箭头"） | **chevron**（Bold 13），macOS 26/27 的披露行已换成 V 形 |

### 15.7 仍与 kit 有差距的地方（明确不做或待做）

- **Color Well 没有取色器**：点开只是返回 true，颜色面板由调用方提供。
- **Image Well 没有拖放**：本库没有文件拖放通道。
- **菜单项右侧 chevron**（子菜单指示）与**快捷键列**未画：kit 的 `Menu Item/*/Chevron`
  与 `/*/Shortcut` 是两个变体，本库的菜单还没有子菜单。
- **Tabs 的图标 + 标签**：kit `Tabs/7/1` 是 45×45 的"符号在上、11px 标签在下"，
  本库的 tabs 只有文字。
- **kit 每页的 Light/Dark 双例并排**：本库靠窗口右上角的外观开关切换，不并排。
- **Push Button 的五档尺寸**、**Switch 的按下形变**（50×31）：仍未做。
- 组件仍保留在库里但**没有页面**：`badge` / `avatar` / `toast` / `hover_card` /
  `drawer` / `pagination` / `empty_state` / `breadcrumb` / `kbd` / `sparkline` /
  `bars` / `calendar` / `input_otp`。删 API 会连带改 12 个探针与一批回归断言，
  留作单独一轮（先让"面"对上，再动 API）。
