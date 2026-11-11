# 05 · 容器类组件

容器只负责绘制底面、分隔、标记或数值，不分配内容布局，也不返回点击状态。
内容在容器后绘制，用 `Stack` 提供内边距。

## 1. `panel` / `card`

```cpp
ui.panel(box);
auto col = ui.vbox(box, 10, 16);
ui.subheading("统计", col.next(24));
```

`card` 是 `panel` 的别名。默认填充 `theme.fill3`，圆角 `radius_xl=12`，无描边。
显式 `Style().background(c)` 覆盖填充；未指定背景且 `muted` 为 true 时用 `fill4`。
描边需显式设置颜色，默认宽 1；如 `Style().stroke(1, ui.theme().separator)`。
`Style().muted_text()` 也会设置 `muted` 字段，panel 会读取此字段。

## 2. `separator`

```cpp
ui.separator({24, 100, 400, 1});
ui.separator({300, 24, 1, 200});
```

按宽高判断方向，画在盒子中线上，粗细 1 逻辑单位。
颜色为 `st.bg`，未指定时用 `theme.border`。

## 3. `badge`

```cpp
ui.badge("已同步", {24, 24, 64, 20});
ui.badge("失败", {96, 24, 48, 20}, aino::Style().destructive());
```

默认圆角 4.5、字号 10、字重 500，文字居中，左右内缩 6。
Default 使用强调色约 15% 的底与同色文字；Destructive 使用危险色对应的浅底与文字。
Secondary/Outline 沿用皮肤表。显式 `bg` 会覆盖默认浅色处理。
宽度由调用方给出，建议 `text_width(text, 10, 500) + 12`，并留出少量余量。

## 4. `progress`

```cpp
ui.progress(.68f, {24, 70, 300, 10});
```

`value` 夹取至 0..1，无自动计时或动画。默认轨道高为 `min(box.h, 10)`，在盒子内垂直居中，
两端半圆。`st.radius >= 0` 时使用整个盒高，圆角仍按半高计算。
轨道为浅色黑 alpha 13 / 深色白 alpha 26；`border_color` 可覆盖轨道，`bg` 可覆盖强调色填充。
非零进度的填充有一个轨道高度的最小宽度。

## 5. `progress_ring`

```cpp
ui.progress_ring(.68f, {24, 100, 36, 36});
```

确定进度环，与 spinner 的时间驱动不同。使用盒子短边为直径、居中绘制，从顶部开始顺时针填充。
`bg` 覆盖轨道，`fg` 覆盖填充，`border_width` 覆盖线宽，`off()` 降低透明度。
默认线宽 `max(2, size * .075)`，轨道 `theme.track`，填充 `theme.tint`。

## 6. `skeleton`

```cpp
ui.skeleton({24, 160, 260, 16});
ui.skeleton({24, 186, 180, 16}, aino::Style().animate());
```

默认静态，底色 `theme.muted`，圆角 `theme.radius=6`。
`.animate()` 允许悬停时推进局部呼吸相位，移开或被浮层遮住时停止请求帧。
它不提供常驻动画选项；帧回调中没有通用重绘请求入口。
画廊加载预览统一只在悬停时动。

## 7. 背景材质

`ui.glass(box, radius, Style().blur(12))` 在已绘制背景上做模糊，再叠加材质与边缘。
它是背景模糊近似，没有液态玻璃的折射和色散。绘制顺序和基础 API 见 [09](09-primitives.md)。
完整示例：[primitives_demo.cpp](../examples/primitives_demo.cpp)。
