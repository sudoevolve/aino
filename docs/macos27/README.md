# macOS 风格设计资料与实现映射

本目录保存名为 **macOS 27 (Community)** 的 Figma 文件提取快照。
这些资料是设计参考，不等同于运行系统的实时控件参数，也不据文件名认定其官方来源。
当前颜色与组件行为以 `aino.hpp` 及对应 API 文档为准。

| 文件 | 内容 |
|---|---|
| [variables.json](variables.json) | 原始变量、模式与取值 |
| [styles_text.json](styles_text.json) | 文字样式 |
| [styles_effect.json](styles_effect.json) | 投影、模糊与内阴影样式 |
| [styles_fill.json](styles_fill.json) | 填充样式 |
| [components.json](components.json) | 组件名称、尺寸、圆角、布局信息 |

## 当前实现

- 正文 13，标题 22/700，副标题 15/600；Medium 控件高度 24。
- 五档高度 16/20/24/28/36；开关宽高 36×16、44×20、54×24、64×28、80×36。
- 系统符号由 Windows 的 Segoe Fluent Icons/MDL2、macOS 的 SF Symbols 提供，X11 使用 Unicode 回退。
- 背景材质通过 `backdrop_blur/glass` 近似；没有液态玻璃的折射与色散。
- 深色工具栏、侧栏和窗口背景统一为 `#1E1E1E`；下方原始表格中的 Title Bar `#3C3C3C` 是资料值。
- `Theme::set_tint()` 同步强调色及兼容字段，普通页签采用淡底，分段选择采用凹槽与强调色选中段。
- 面板、表格与提示默认减少边框；对话框无全屏黑色遮罩，操作按钮采用圆角。
- 画廊固定侧栏 252、工具栏高 60，15 个分类、43 张卡片，连续滚动与搜索。
- 图元示例单独展示几何、透明合成、阴影、裁剪、变换、文字、系统图标与背景模糊。

完整主题字段见 [02](../02-style-and-theme.md)，组件见 [06](../06-interactive.md)，
浮层见 [08](../08-overlays.md)，图元见 [09](../09-primitives.md)。

## 提取工具

`tools/` 是离线 Figma 检查脚本，不参与库或示例的构建。
需安装支持 `zstdDecompressSync` 的 Node.js 与 `tools/package.json` 中的依赖。
原始 `canvas.fig` 不在仓库中；只读 JSON 无需安装 Node。

在包含 `canvas.fig` 的工作目录运行脚本：

```sh
node /path/to/aino/docs/macos27/tools/catalog.js "Button"
node /path/to/aino/docs/macos27/tools/digest.js "Segmented Control"
node /path/to/aino/docs/macos27/tools/extract.js
```

`catalog` 列出组件，`digest` 输出匹配组件的规格，`extract` 在当前目录写出提取 JSON。
重新提取时应在独立工作目录核对差异，再更新仓库快照。

## 原始参考表

以下保留资料中的颜色、尺寸、文字与效果表；与实现有区别时按上面的映射及源码判断。

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
> 否则 `0 0 3 .55` 会被画成一圈 55% 黑的 3px 实心描边。实现说明见 [图元与度量](../09-primitives.md)。

---

## 五、形状语言

- **圆角矩形**：Mini ~ Medium（半径 = 4/5/6，等于高度的 1/4）
- **胶囊**：Large / XL 的按钮与开关（半径 1000），以及所有开关的轨道（`r=1000`）
- **同心圆角**：子元素半径 = 父半径 − 内边距（面板 10 / 菜单 10 / 气泡 10）
- **描边**：1px，颜色 = 分隔线（`#0000001A` / `#FFFFFF1A`）；暗色弹层内描边 `#FFFFFF26`
- 材质资料与当前模糊近似的区别见本文“当前实现”。

---
