# 11 · 动画与变换

两个都不大，但语义必须先说清：**动画是"目标驱动"的，变换是"只影响绘制"的**。
这两条决定了它们在立即模式里为什么能这么小。

---

## 1. 为什么没有常驻动画系统

aino 的头号承诺是**空闲 0.00% CPU**（事件驱动、`WaitMessage` 阻塞）。
一个"每帧都在推进"的动画系统会直接毁掉它：只要有一个 300ms 的过渡在跑，
主循环就从"阻塞"变成"满速重绘"。

所以这里只有两种东西：

| API | 用途 | 什么时候要帧 |
|---|---|---|
| `tween()` | 有终点的过渡（展开、滑入、变色） | **只在过渡进行中**要帧，到点立刻停 |
| `smooth()` | 跟随一个一直在变的目标（鼠标、滚动惯性） | 不要帧，纯函数，由调用方那帧的重绘带动 |

没有关键帧、没有时间轴、没有隐式动画。想要"组件出场自动淡入"就写
`ui.fade(id, visible)`，把它乘进颜色或位移里 —— 就一句。

---

## 2. 缓动函数

```cpp
using Easing = float (*)(float);   // 输入 0..1，输出 0..1（可以过冲）
```

| 函数 | 形状 | 适合 |
|---|---|---|
| `easing::linear` | 直线 | 进度条、滚动惯性 |
| `easing::out_cubic` | 快起慢收（默认） | 绝大多数 UI 过渡 |
| `easing::out_quint` | 更急的收尾 | 强调"到位"的数值变化 |
| `easing::in_out_cubic` | 两头慢中间快 | 位置移动（滑入/滑出） |
| `easing::out_expo` | 几乎瞬间到位 | 弹出菜单、轻提示 |
| `easing::out_back` | 轻微过冲再回落 | "弹一下"的强调动效 |

端点都精确：`e(0)==0`、`e(1)==1`（`out_back` 的过冲只发生在中间）。

---

## 3. `tween` —— 补间

```cpp
float t = ui.tween(id, open ? 1.0f : 0.0f, 0.24f, aino::easing::out_cubic);
ui.fill_rect(aino::Rect(x, y, 240.0f * t, 3.0f), ui.theme().primary, 1.5f);
```

| 参数 | 说明 |
|---|---|
| `id` | 用 `detail::make_id("kind", box)` 生成；同一个 id = 同一条动画 |
| `target` | 目标值。**和上帧不同就从当前显示值重新起跑**（不会跳） |
| `duration` | 秒。0.15~0.3 是 UI 的舒适区，>0.5 会显得迟钝 |
| `e` | 缓动函数 |

行为细节：

- **第一次调用直接返回 target**（不做"从 0 开始"的进场动画，避免首帧闪一下）。
- 目标中途改变（比如手抖点了两次），会以**当前显示值**为新起点重新计时。
- 到达目标后**不再请求重绘** —— 这是"空闲 0%"的关键，`bigfeat_probe` 里有断言。

### 颜色：`tween_color`

```cpp
aino::Color bg = ui.tween_color(id, hover ? ui.theme().accent : ui.theme().background, 0.18f);
```

一个进度驱动四个通道（不是 4 个独立 tween），所以插值路径是直线，中途不发脏。

### 显隐：`fade`

```cpp
const float a = ui.fade(id, visible, 0.18f);
if (a > 0.01f) {
    aino::Transform tint;
    tint.translate = aino::Vec2(0.0f, 8.0f * (1.0f - a));     // 从下方 8px 升上来
    ui.push_transform(tint);
    ui.panel(box, aino::Style().background(ui.theme().card.alpha255(int(255 * a))));
    ui.pop_transform();
}
```

> 注意：`fade` 只给你 0..1 的数，**不会**自动改透明度 —— 因为文字走 GDI、
> 没有 alpha 通道，得自己把颜色往背景方向混（`Color::with_alpha` / `alpha255`）。
> 所以典型的"淡入"是**位移 + 底色 alpha** 两条一起做，文字本身保持不透明。

### `smooth` —— 没有终点的跟随

```cpp
// 鼠标跟随：60fps 和 30fps 落到同一个位置（指数逼近，和帧率无关）
x = ui.smooth(x, ui.input().mouse.x, 14.0f);
```

`smooth` 是**纯函数**，不注册状态、不请求重绘。它适合"目标每帧都在变"的场景；
有明确终点的过渡请用 `tween`（`smooth` 永远逼近不到 100%，`tween` 会精确落地）。

---

## 4. `Transform` —— 仿射变换

```cpp
struct Transform {
    Vec2  translate{0.0f, 0.0f};   // 逻辑单位的平移
    float scale = 1.0f;            // 缩放（绕 origin）
    float rotate = 0.0f;           // 度，顺时针为正（屏幕坐标）
    Vec2  origin{0.0f, 0.0f};      // 缩放/旋转的中心（逻辑坐标）
};
```

合成顺序与 CSS 一致：`p' = translate + rotate * scale * (p - origin)`。

```cpp
aino::Transform t;
t.rotate = -6.0f;
t.scale  = 1.08f;
t.origin = aino::Vec2(card.cx(), card.cy());
ui.push_transform(t);
    ui.panel(card, aino::Style().background(ui.theme().card));
    ui.label("悬停放大并轻微倾斜", card, aino::Style().center());
ui.pop_transform();
```

### 它只影响绘制，不影响布局与命中

`push_transform` 期间画的几何和文字都被映射过，但：

| 层 | 是否受变换影响 |
|---|---|
| 组件绘制的像素 | ✅ 平移 / 缩放 / 旋转都生效 |
| 布局坐标（`Stack` 切出来的 `Rect`） | ❌ 不变，还是逻辑坐标 |
| 命中判定（`contains` / `hovered`） | ❌ 不变 |

这就是 CSS `transform` 的语义：**"看起来变了"和"点得到"是两件事**。
好处是动画期间布局不抖、命中不飘；代价是旋转后的控件点选区域不会跟着转
（要转就把命中判定自己算一遍，或者用旋转后的多边形做 `contains`）。

### 支持范围（诚实版）

| 图元 | 无旋转（平移/缩放） | 有旋转 |
|---|---|---|
| `fill_rect` / `stroke_rect` | 精确（走原来的专用光栅器） | 多边形近似（每角 5 段圆弧，`raster_poly` 抗锯齿） |
| `fill_circle` | 精确 | 精确（圆旋转不变，半径按缩放走） |
| `draw_line` | 精确 | 精确（端点映射，宽度按缩放） |
| `draw_text` | 精确（**字号按 scale 放大**，不是拉位图） | 精确（GDI `lfEscapement` 原生旋转，边缘仍是灰度 AA） |
| `drop_shadow` | 精确 | 用**包围盒**（阴影不跟着转，但不会消失） |
| `push_clip` | 精确 | 用**包围盒**（轴对齐的裁剪区表达不了旋转矩形） |

几条硬事实：

- **文字不会因为缩放而糊**：`scale` 会真的换成更大的字号去调 GDI，所以
  `scale=2` 的字和直接写 `font_size*2` 一样清晰。位图放大那种糊法这里没有。
- **旋转文字是原生的**：不是"画到位图再采样"，GDI 用 `lfEscapement` 直接排
  旋转的字形，所以边缘还是干净的灰度抗锯齿。
- **嵌套**：内层在外层的局部坐标里再变换一次，顺序是父 → 子（与 CSS 一致）。
- **裁剪区**在旋转下按包围盒处理 —— 精确裁旋转内容需要多边形裁剪，本库没做。

### 一层 push 就够的场景

```cpp
// 抽屉从左滑入：只用 translate，命中判定不用管（控件本来就在屏幕外）
aino::Transform slide;
slide.translate = aino::Vec2(-280.0f * (1.0f - t), 0.0f);
ui.push_transform(slide);
drawer(ui, box);
ui.pop_transform();
```

---

## 5. 完整例子：一个"按下弹一下"的卡片

```cpp
bool hovered = box.contains(ui.input().mouse.x, ui.input().mouse.y);
const float h = ui.tween(id_dyn, hovered ? 1.0f : 0.0f, 0.16f);   // 悬停插值

aino::Transform t;
t.scale  = 1.0f + 0.03f * h;
t.rotate = -1.5f * h;
t.origin = aino::Vec2(box.cx(), box.cy());
ui.push_transform(t);
    ui.panel(box, aino::Style().background(ui.theme().card));
    ui.label("卡片", box, aino::Style().center());
ui.pop_transform();
```

悬停时只有 0.16 秒在重绘，鼠标移开再落回 0 帧 —— 和整个库的模型一致。

---

## 6. 插值动画 vs 一次跳变：成本到底差多少

### 先分清"空闲"和"动画期间"

判据不是"有没有插值"，而是**帧有没有停下来**。`tween` / `tween_color` / `animate_value`
三者都只在 `t < dur`（或 `|剩余差| > eps`）时 `repaint()`，收敛那一帧之后**一次都不要帧**：

```cpp
if (tw.t < tw.dur) {          // 只有还在跑才要帧
    tw.t += impl->dt;
    if (tw.t >= tw.dur) tw.t = tw.dur;
    repaint();
}
```

所以"用插值"和"空闲 0.00% CPU"完全不冲突 —— 回归里 `bigfeat_probe` 有一项就是
"到点后 `request_repaint` 计数必须为 0"。

### 三种形态的真实成本

| 形态 | 帧数 | 渲染耗时 | 之后 |
|---|---|---|---|
| 一次跳变（直接给终值） | 1 帧（输入帧） | ≈ 6.5 ms | 0 |
| 有终点的插值（150~250ms） | 约 6~12 帧 | 40~80 ms 摊在 0.2 s 内 | **归 0** |
| 永不收敛的循环动画 | 无限 | 每帧 6.5 ms × 帧率 | 一直烧 |

前两行是**有界的**，第三行才是真正的问题。历史上空闲 CPU 跳到 95%~100% 的三次，
全部是第三行：`set_theme` 无条件 `need_frame`、常驻 spinner 的光标闪烁、`skeleton`
无条件 `repaint()`。**没有一次是因为"用了 tween"**。

### 该怎么选

| 场景 | 建议 |
|---|---|
| 页面切换、展开/折叠、弹窗进出 | 插值，150~250 ms，一次就停 |
| 按压反馈（`press_scale`） | 可以用，但限制在 0.94~0.98，且时长 ≤120 ms |
| hover 变色/位移 | 最容易被滥用：鼠标划过一排 10 个控件 = 连续 1.7 s 有帧。**要么短（≤100 ms），要么直接跳变** |
| 装饰性常驻动画（呼吸、跑马灯） | 不要做。`skeleton` 已经因此改成默认静态 |
| 加载指示（spinner） | 只在"确实在加载"时出现，加载完立刻消失 |

### 真的要省，用 `set_anim_fps` 降频

`win.set_anim_fps(30)` 只节流**组件自请求的帧**，输入帧不节流（点击永远立即响应），
静止时仍然是 0 帧。动画页开销大致按比例下降。

注意 `Sleep` 的 15.6 ms 节拍量化：目标 fps 越低越准，30fps 实测约 32fps，
60fps 实际只有 30~36fps —— 所以"降到 30"省不到一半，别按线性外推。

---

## 7. 别这么做

| 反模式 | 为什么 |
|---|---|
| 用 `ui.time()` 算所有动画 | 全局时钟相位无法"从这里开始"；改用 `tween` 或局部相位 |
| 每帧无条件 `ui.repaint()` | 空闲 CPU 从 0% 直接跳到 100%（历史上踩过三次） |
| 用 `smooth` 做有终点的位移 | 永远差最后一点点，永远不会停 |
| 对 `box` 做旋转却按原矩形做命中 | 视觉和点击区域会分离（这是 `Transform` 的设计取舍，不是 bug） |
| 用 `Transform` 替代布局 | 它不改布局，父子间距不会跟着缩放 |

---

## 8. 断言

```bash
tools/probe/bigfeat_probe.exe     # 47 项：折行缓存 / 滚动 / 撤销 / 补间 / 变换
```

变换部分的断言是**像素级**的：平移后落位精确、2 倍缩放尺寸翻倍且中心不动、
旋转 90° 宽高互换（矩形与文字各一组）、旋转 30° 的圆角矩形内部实心且面积不变、
嵌套按父→子复合、`pop` 之后逐像素复原。
