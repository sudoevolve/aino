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

### 还没做

- `badge` / `avatar` / `tooltip` / `alert` / `dialog` / `popover` / `menu` 的**形状**细节
  （配色已随主题走，但行高、内边距、圆角尚未逐个对官方值）
- 表格的表头字重与列对齐（数字列右对齐）还没按官方列表做了
- 液态玻璃按约定跳过
