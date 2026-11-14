# 09 · 基础图元与度量

所有坐标均为逻辑单位，内部处理 DPI。几何由 CPU 光栅化并使用覆盖率抗锯齿，
颜色 alpha 与边缘覆盖率共同参与合成。完整交互示例见
[primitives_demo.cpp](../examples/primitives_demo.cpp)：

```sh
cmake --build build --config Release --target aino_primitives_demo
```

程序提供圆角、线宽、不透明度调整，浅深主题切换，以及可滚动的八组图元展示。

## 1. 几何

```cpp
void clear(const Color& c);
void fill_rect(const Rect& r, const Color& c, float radius = 0);
void stroke_rect(const Rect& r, const Color& c, float radius = 0, float width = 1);
void fill_rect_s(const Rect& r, const Color& c, float radius, float smooth);
void stroke_rect_s(const Rect& r, const Color& c, float radius, float width, float smooth);
void fill_circle(float cx, float cy, float radius, const Color& c);
void fill_crescent(float cx, float cy, float radius,
                   float cut_cx, float cut_cy, float cut_radius, const Color& c);
void draw_line(float x0, float y0, float x1, float y1, const Color& c, float width = 1);
```

`clear` 填满后备缓冲。圆角矩形半径限制到短边的一半，描边在边界内侧。
显式连续圆角接口以 `smooth` 控制曲率，负值取主题的 `corner_smoothing`；0 为圆弧圆角。
线段两端为圆头。透明色、空矩形或无效半径不绘制。

`fill_crescent` 是圆减圆的几何差集，挖空部分保留原背景；不需要用背景色覆盖。
它适合展示几何构造；应用里的月亮图标使用 `SystemIcon::Moon`。

```cpp
ui.fill_rect_s({24, 24, 120, 64}, ui.theme().tint, 12, .6f);
ui.stroke_rect({160, 24, 120, 64}, ui.theme().tint, 12, 2);
ui.fill_circle(320, 56, 24, ui.theme().orange);
ui.draw_line(24, 110, 360, 130, ui.theme().label, 2);
```

## 2. 渐变与边缘

```cpp
void fill_rect_grad(const Rect& r, const Color& top, const Color& bottom,
                    float radius = 0, float smooth = -1);
void stroke_rect_top(const Rect& r, const Color& c, float radius, float width, float height,
                     float smooth = -1);
```

渐变从上向下插值颜色，包含 alpha。`stroke_rect_top` 只保留圆角描边顶部指定高度，
用于表面高光，不改变其余部分。

## 3. 阴影

```cpp
void drop_shadow(const Rect& r, float radius, float blur, const Color& c, float dy = 1);
void inner_shadow(const Rect& r, const Color& c, float radius, float blur, float dy = 0,
                   float smooth = -1);
```

外阴影使用距离场和高斯累积分布近似的衰减，而非多层透明矩形叠加。
alpha 不超过传入颜色的 alpha。`dy` 为纵向偏移；绘制成本随外扩扫描区域和 DPI 增加。
先画投影，再画主体。内阴影只作用于轮廓内部。

```cpp
ui.drop_shadow(box, 12, 10, aino::Color(0, 0, 0, 40), 3);
ui.fill_rect(box, ui.theme().control_bg, 12);
ui.inner_shadow(box, aino::Color(0, 0, 0, 20), 12, 3, 1);
```

## 4. 背景模糊与材质

```cpp
void backdrop_blur(const Rect& r, float radius, float blur);
void glass(const Rect& r, float radius = 12, const Style& st = Style());
```

`backdrop_blur` 读取调用时已画进后备缓冲的区域，做三趟可分离盒式模糊并按圆角写回。
`glass` 组合背景模糊、材质、边缘与投影，`Style().blur(12)` 控制模糊半径，
`background(c)` 可覆盖材质色。第二个参数是圆角半径。
先画背景，再调用模糊/材质，最后绘制前景内容。

这是一种背景模糊近似，不具备液态玻璃的折射、色散或系统合成效果。
没有先绘制背景时不会凭空产生背景细节。

## 5. 文字与系统图标

```cpp
void draw_text(const std::string& text, const Rect& box, const Color& c,
               const Style& st = Style());
bool system_icon(SystemIcon icon, const Rect& box, const Color& color);
void draw_mono_text(const std::string& text, float x, float y_top, const Color& c,
                    float font_size, int weight = 400);
```

正文默认字号 13、字重 400，`Style` 控制字号、字重、斜体、对齐、折行和行距。
`draw_text` 裁剪到当前有效裁剪区与传入盒子的交集。
等宽文本以左上角定位，使用独立等宽字体通道。`draw_text` 与 `draw_mono_text` 都参与 `begin_measure()`：等宽文字的外接矩形按实测字宽 × 行高登记，因此用等宽字自绘的组件不需要额外补偿。

`system_icon` 只绘制符号，不提供点击交互，尊重裁剪、颜色和变换。
Windows 使用 Segoe Fluent Icons / MDL2，macOS 使用 SF Symbols；X11 从桌面系统图标主题读取 SVG/PNG，支持主题继承及 Adwaita/hicolor 回退，按传入颜色绘制 alpha mask。
Linux 使用运行时加载的 GdkPixbuf 和系统 SVG loader，不初始化 GTK；Ubuntu/Debian 安装 `libgdk-pixbuf-2.0-0 librsvg2-common adwaita-icon-theme`。缺少库或对应系统图标时返回 false。路径及按尺寸栅格化的结果会缓存，XSETTINGS 图标主题变化时失效。
媒体符号使用 `Play`、`Pause`、`PreviousTrack`、`NextTrack`、`Shuffle`、`RepeatOne`、`RepeatAll`、`Timer`。
可配合空文字按钮构造系统图标按钮，见 [02 §5](02-style-and-theme.md#5-系统图标)。

## 6. 度量

| API | 用途 |
|---|---|
| `text_width(text, font_size, weight = -1)` | 单行比例字体宽度 |
| `line_height(font_size)` | 字体行高 |
| `text_height(text, width, st)` | 与绘制相同折行规则的总高度 |
| `alert_height(title, description, width, st)` | 提示块所需高度 |
| `mono_width(text, font_size, weight = 400)` | 等宽文本宽度 |
| `mono_advance(font_size, weight = 400)` | 等宽半角字符步进 |
| `mono_ascent(font_size, weight = 400)` | 顶部到基线的距离 |

测量与绘制必须使用相同的宽度、字体和折行选项。

```cpp
const aino::Style style = aino::Style().wrap_text().line_spacing(3);
const float height = ui.text_height(paragraph, box.w, style);
ui.label(paragraph, {box.x, box.y, box.w, height}, style);
```

大块自定义内容可用 `begin_measure/end_measure` 测量外接矩形，见 [03](03-layout.md)。

## 7. 裁剪与变换

```cpp
ui.push_clip(viewport);
aino::Transform transform;
transform.origin = {box.cx(), box.cy()};
transform.rotate = 18;
ui.push_transform(transform);
ui.fill_rect(box, ui.theme().tint, 12);
ui.pop_transform();
ui.pop_clip();
```

裁剪栈逐层取交集，同时限制几何、文字和组件命中。push/pop 必须配对。
Transform 只改变绘制，不改变布局或命中区域；旋转裁剪以包围盒近似。
交互控件的视觉位置应与热区保持一致。更多示例见 [11](11-motion.md)。

一帧通常依次绘制背景、容器、内容、直接绘制的浮层；帧末浮层由 `end_frame` 处理。
常规应用由 `Window::run` 管理帧生命周期，浮层顺序见 [08](08-overlays.md)。
