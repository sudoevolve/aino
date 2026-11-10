# 08 · 浮层类组件

## 0. 绘制顺序与输入隔离

浮层有两种绘制方式，调用方需要区分：

| API | 绘制时机 | 调用建议 |
|---|---|---|
| tooltip、menu、select、context_menu、menubar、popover、hover_card | 弹出内容推迟至帧末 | 可以在对应触发器处调用 |
| dialog、sheet、alert_dialog、drawer、toast | 调用时直接绘制 | 放在普通内容之后；自定义内容紧接着绘制 |

`popover` 的内容回调在帧末执行；`drawer` 的回调在调用期间执行。
不要用“所有浮层都在帧末绘制”来安排层级。

模态面板与下拉菜单登记矩形和交互层级，阻挡下面组件的点击、悬停、滚轮与键盘焦点。
关闭帧仍保留输入闸门，防止关闭点击穿透。纯展示的 tooltip/toast/hover_card 不登记模态闸门。
自定义交互应使用库组件或 `ui.hovered(box)`；直接读 `box.contains(mouse)` 不自动遵守输入隔离。
检测覆盖范围与层级的断言见 `tools/probe/overlay_probe.cpp`。

## 1. `tooltip`

```cpp
ui.button("删除", box, aino::Style().destructive());
ui.tooltip(box, "永久删除，不可恢复");
```

悬停约 0.4 秒出现；计时期间请求帧，显示稳定后停止。离开锚点重新计时。
默认字号 11、字重 500、圆角 5、水平内边距 6、垂直内边距 3。
底色 `tooltip_bg`，文字色 `popover_foreground`；深色主题增加细边。
优先放在锚点上方 7，放不下时翻到下方；水平夹取至窗口内。
支持 `bg/fg/font_size/disabled`。宽度按单行文字测量，不自动折行，长说明请使用 hover_card。

## 2. `menu` 与 `select`

```cpp
int picked = -1;
if (ui.menu("操作", {"重命名", "复制", "删除"}, &picked, {24, 24, 120, 24}))
    apply(picked);
```

点击触发器展开或收起，选择项写入索引并返回 true，点击外部关闭。
触发器在正常内容层绘制，列表推迟到帧末。列表项用系统箭头和状态色，
不使用原生平台菜单窗口；默认项高至少 24，列表宽度同时考虑触发器与最长文本。
`select()` 的返回值表示持久选中索引变化，区别见 [06](06-interactive.md)。

## 3. `dialog` 与 `sheet`

```cpp
if (ui.dialog("确认操作", "确认后将保存设置。", &open, panel)) {
    if (ui.button("保存", {panel.x + 20, panel.bottom() - 44, panel.w - 40, 24})) {
        save();
        open = false;
    }
}
```

`dialog` 返回本帧是否绘制打开状态，关闭帧仍返回 true，后续帧不再绘制。
面板默认 `popover_bg`、圆角 10、内边距 20、标题 13/700、正文 11/400、正文附加行距 3。
面板依靠阴影分层，不铺全窗口黑色遮罩，无默认描边或隐藏的关闭热区。
Esc、点击面板外或调用方按钮可关闭。自定义按钮要在同一个打开分支内处理。
内容裁剪在面板中，调用方必须预留操作按钮高度；正文与按钮不能占用同一区域。

`sheet(title, body, &open, box, st)` 绘制贴上边的工作表，上方裁平、下方圆角；
调用方把 `box.y` 放在工具栏下边。它也直接绘制，并使用同样的外部 open 状态和输入闸门。

## 4. `toast`

```cpp
if (toast_left > 0) {
    ui.toast("已保存", toast_box);
    toast_left -= ui.frame_dt();
    ui.repaint();
}
```

纯展示，位置与生命周期由调用方管理。默认 `popover_bg`、圆角 10、字号 13/500、
左右内缩 14、阴影；无描边或左侧危险色条。Destructive 使用危险色文字。
支持 `bg/fg/radius/font_size/variant/disabled`；文字超出盒子时裁剪。
倒计时需要请求后续帧；持续显示的静态 toast 不需要无条件重绘。

## 5. `popover`

```cpp
ui.popover("排序", &open, trigger, aino::Vec2(200, 96), [&](const aino::Rect& c) {
    if (ui.button("按名称", {c.x, c.y, c.w, 24})) open = false;
});
```

传入内容区尺寸，库添加外部内边距，并把内容区矩形交给回调。
返回本帧打开状态。回调在帧末运行，允许放按钮、输入框等交互控件。
**回调捕获的数据必须存活到当前帧结束**，不要引用已经离开作用域的局部临时值。

## 6. `hover_card`

```cpp
ui.hover_card(anchor, aino::Vec2(210, 78), [&](const aino::Rect& c) {
    ui.label("资料说明", c);
});
```

锚点悬停立即显示任意只读内容，无模态输入闸门。优先放右侧，空间不足时翻到左侧。
面板默认圆角 12，内容周围内边距 12。交互内容请使用 popover。
同样遵守帧末回调捕获的生命周期要求。

## 7. `alert_dialog`

```cpp
if (ui.alert_dialog("删除素材？", "删除后不可恢复。", "删除", "取消", &open, panel,
                    aino::Style().destructive())) remove_material();
```

返回本帧是否确认，确认或取消都会将 `*open` 置 false。
面板圆角 12，标题 13/700，正文 11/400，横向内边距 20、纵向 16。
两个通宽 28 高的操作按钮纵向排列，均为胶囊圆角；主操作使用强调色或危险色填充，取消使用中性底。
Esc 与面板外点击视为取消。它直接绘制，不铺黑色遮罩。

## 8. `drawer`

```cpp
ui.drawer("详情", &open, 0, panel, [&](const aino::Rect& c) {
    if (ui.button("完成", {c.x, c.y, c.w, 24})) open = false;
});
```

面板位置和大小由 `box` 决定；保留 `side` 参数供已有代码使用，但不会自动摆放面板。
内容回调直接执行，并裁剪到内容区。标题行高 `max(fs + 32, 54)`、默认内边距 20、面板圆角 10。
没有隐式位移动画、遮罩或不可见关闭热区。关闭由 Esc、外部点击或可见内容按钮触发。

## 9. `context_menu`

```cpp
if (ui.context_menu(area, {"标记已读", "归档", "删除"}, &picked)) apply(picked);
```

在 area 内右键打开，弹出位置取按下时鼠标坐标，并夹取到窗口内。
点外部或 Esc 关闭。选中时返回 true 并写入索引；弹出内容在帧末绘制。

## 10. `menubar`

```cpp
aino::MenuPick pick = ui.menubar({"文件", "编辑"}, {{"新建", "保存"}, {"撤销", "重做"}}, box);
if (pick.valid()) apply_menu(pick.menu, pick.item);
```

返回顶级菜单与菜单项索引；没有选择时两者为 -1。
点击顶级项打开，已有菜单打开时悬停相邻项切换菜单。
输入闸门同时包含菜单栏与弹层，让顶级项间切换仍能响应。
