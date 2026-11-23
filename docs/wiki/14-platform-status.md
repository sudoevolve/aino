# 平台能力与验收边界

本文说明当前源码能力与平台使用边界。历史验证见[平台记录](../reviews/platform-validation.md)，构建命令见[快速开始](01-quickstart.md)。

## 当前能力矩阵

“待原生验证”表示代码路径存在，但本轮没有在对应桌面环境运行；不能据此判断交互已经完整可用。
Windows 离屏夹具直接构造输入，不能替代系统输入法、剪贴板和辅助技术的端到端检查。

| 能力 | Windows / Win32 | Linux / X11 | macOS / Cocoa |
| --- | --- | --- | --- |
| CPU 绘制、文字及上屏 | Release 构建、实窗像素与离屏探针通过 | 已有 X11、FreeType/fontconfig 和 XFontSet 路径；待原生验证 | 已有 CoreText/CoreGraphics 路径；缓存图像复用待原生验证 |
| 后台唤醒和程序关闭 | 内核事件；实窗检查已有历史记录 | 非阻塞管道与 X 连接联合等待；待原生验证 | CFFileDescriptor 接主 RunLoop；待原生验证 |
| 光标闪烁 | 按下一截止时间等待；实窗检查已有历史记录 | 同一截止时间接入等待；待原生验证 | 按截止时间与 RunLoop deadline 等待；待原生验证 |
| 多窗口与原生关闭 | 同线程事件泵；Gallery 创建、取消、回填已实窗检查 | 同线程回调与关闭已实现；待原生验证 | 主线程回调、原生关闭与资源释放已实现；待原生验证 |
| 剪贴板 | Unicode 系统读写与成功返回已实现；跨应用另验 | TARGETS/UTF8/TEXT、SelectionRequest 与 INCR 已实现；待原生互通验证 | NSPasteboard UTF-8 读写已实现；待原生互通验证 |
| 键盘和编辑 | 选区、快捷键和历史隔离已有探针；系统输入另验 | 键值、修饰键和 XIM 路径存在；待原生输入法验证 | 控制键、修饰键、Unicode、IME client 已实现；surrounding text 能力仍有限，待原生验证 |
| 调整窗口、DPI 和失焦 | 已有事件与缓存更新路径；真实多显示器另验 | 尺寸、XSettings 和 XIM 焦点已实现；待桌面验证 | backing scale、尺寸、失焦和 Retina 缓冲已实现；待原生验证 |
| 通用键盘焦点及无障碍 | 尚无通用 Tab/Shift-Tab 遍历、按钮键盘激活和系统无障碍桥接 | 同左，且需配合原生输入验证 | 同左，且需补齐键盘后端 |

源码核对入口为 `aino.hpp` 的 `win_proc`、`Window::poll/run`、`x11_handle`、
各平台 `Window::try_set_clipboard/request_clipboard` 及 `cocoa_*` 回调。
画廊素材子窗口入口为 `examples/gallery.cpp` 的 `open_material_window()`。
上述缺口不会因示例能编译或绘图像素通过而自动消失。

## Linux / 原生 Wayland

使用 `-DAINO_WAYLAND=ON`，不链接 X11、不依赖 XWayland；接入方法见
[原生 Wayland](15-wayland.md)。默认 Linux 构建仍为 X11。

| 能力 | 实现与边界 |
| --- | --- |
| 绘制与呈现 | FreeType/fontconfig 客户端文字、CPU 光栅器、wl_shm；最多三个缓冲，release 后复用 |
| 调度与多窗口 | 独立窗口连接、联合等待 socket/唤醒管道/剪贴板发送、frame callback 与按键重复截止时间 |
| 窗口管理 | xdg-shell 标题、尺寸约束与关闭；优先服务端装饰，缺少时使用库内标题栏；位置由 compositor 决定 |
| DPI | output 整数缩放；fractional-scale-v1 + viewporter 存在时采用首选分数缩放；混合 DPI 实屏另验 |
| 键盘与指针 | xkb keymap、Compose、修饰键快照、按键重复、按钮、双击、拖动、垂直滚动与主题光标 |
| 输入法 | 可选 text-input-v3，光标矩形、用途、预编辑与提交；未实现 surrounding-text、上下文删除和预编辑选区 |
| 剪贴板 | wl_data_device UTF-8，2 秒截止时间、32 MiB 上限、非阻塞发送；写入须有焦点和有效 serial |
| 桌面集成 | `WindowConfig::app_id` 匹配 .desktop。SSH 示例接收本地文件拖放，尚无公开拖放 API、触屏与无障碍桥接 |

源码入口是 `WaylandState`、`wayland_registry/create/present/dispatch` 与 Wayland 的 `Window` 方法。
协议表内嵌于头文件，系统库动态加载；普通构建不需要协议生成工具或第三方开发头文件。
日常 CI 只编译本后端，不能据此宣称所有 Wayland 桌面和输入法均已完成验收。

## SSH/SFTP 功能边界

网络示例的完整性与窗口后端分开验收。

| 项目 | 当前状态 | 后续验收 |
| --- | --- | --- |
| 私钥与主机校验 | 私钥路径/口令参数已修正；主机记录使用 RAII，读取错误、不匹配及首次保存失败均拒绝信任 | 真实服务器验证无口令/加密私钥、错误口令、主机指纹变更和重新连接 |
| 配置与错误反馈 | 连接配置同目录临时文件和原子替换；失败保留旧文件、表单和模型；SSH 配置改由系统编辑器打开实际来源文件，保存后重新导入 | 在真实用户目录和受限权限下复核；不承诺多进程配置合并或完整断电恢复 |
| 凭据存储 | Windows DPAPI；macOS Keychain / Linux libsecret；后台事务仅重存变化项，不支持明文/XOR 密码记录 | 三平台安全服务拒绝访问与撤销记住密码需原生验收；不可用时保留记录并报错 |
| 大文件 | 使用 64 位文件长度，3 GiB 稀疏文件检查通过 | 真实超过 2 GiB 上传/下载，校验内容和进度；本地长度检查不等于完成网络传输 |
| 传输与取消 | 单 I/O owner、非阻塞 EAGAIN 等待、取消唤醒与约 2 秒收尾截止时间已实现；暂存提交、长度与关闭检查、基本权限/属主保留 | 慢网下终端与传输交错、各阶段取消、断线及真实清理仍需实传验收；POSIX ACL/xattr/硬链接语义仍需设计 |
| 文件选择、目录及队列 | 远程文件可筛选、右键操作。Windows 支持多选上传，Windows 与原生 Wayland 支持拖入本地文件，上传按会话排队 | 递归目录传输和断点续传尚未提供。Wayland 需按文件管理器验证 URI 拖放互通 |

本地服务端测试入口和独立测试目录的设置方法见 [01 的 SSH 部分](01-quickstart.md)。
普通 CTest 中的 `ssh_ui` 是离线界面测试。发布流程另用 `tools/ssh_release_check.py` 启动隔离服务器，
运行 SSH/SFTP 实传检查，POSIX 覆盖高描述符连接，Windows 额外覆盖客户端会话与传输工作线程。
