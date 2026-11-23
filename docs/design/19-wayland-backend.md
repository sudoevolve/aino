# 原生 Wayland 后端边界

状态：已实现编译期可选后端；使用与平台限制以 [Wiki](../wiki/15-wayland.md) 为准。

## 决策

保留 `aino.hpp` 和现有 `Window` / `Ui` 接口。Linux 用 `AINO_WAYLAND` 选择原生后端，
CMake 将选择传播到所有调用单元；默认 X11 不变。暂不在同一二进制中运行时切换两个后端，
避免引入 X11 链接依赖、双套窗口状态分派和连接失败后的隐式降级。

Wayland 只拥有窗口协议、输入、缩放、剪贴板和呈现。控件继续向 CPU Canvas 绘制；
`UnixTextRenderer` 共用 FreeType/fontconfig 缓存，XLFD 回退只编译进 X11。
Wayland 的运行库不可用时明确建窗失败，不创建无文字窗口。

## 协议与依赖

采用系统 libwayland-client 的代理 API，不自行解析 wire protocol。
协议接口表从上游 XML 生成并内嵌，保留协议许可；编译无需开发头文件或 scanner。
libwayland-client、xkbcommon、wayland-cursor 在运行时加载；不增加 GPU 或 UI 框架。
绑定版本上限与监听器匹配，可选协议缺失时保留明确的功能边界。

## 所有权与调度

每个窗口在 UI 线程拥有独立连接及代理，后台只调用 `wake()` / `close()`。
独立连接沿用既有 X11 生命周期，代价是同进程多窗口的 registry/seat 状态重复。
同线程读取剪贴板时同时派发各窗口协议，避免互等；绘制中的 resize 只记为待处理，
下一帧才替换 CPU 画布。

CPU 画布与 compositor 使用的共享缓冲分开，最多三个共享缓冲，busy 缓冲不覆盖、不解除映射。
frame callback 和 buffer release 决定能否上屏；调度等待时考虑显示连接、唤醒管道、
按键重复及剪贴板发送截止时间，没有固定频率的空闲轮询。
显式 `render()` 保留同步绘制回调语义，上屏暂不可用时保留最新完成画面。

## 有意保留的边界

窗口位置由 compositor 决定；app-id / .desktop 匹配替代 X11 图标属性。
服务端装饰不可用时在表面顶部绘制标题栏，内容坐标与输入坐标相应转换。
text-input-v3 目前提供预编辑和提交，不声称具备完整上下文编辑；扩展时须将 UTF-8 字节偏移、
撤销栈、焦点切换和密码隐私一起设计。水平滚动、触屏和拖放需要对应的公开输入契约，
不能只在后端丢弃事件后标记为已支持。
