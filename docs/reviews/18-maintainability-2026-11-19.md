# 示例与公共接口可维护性审查 · 2026-11-19

范围：全部六个示例 `.cpp`、三个 SSH 支持头文件，以及 `aino.hpp` 的公开声明和对应状态、滚动、浮层实现。此次关注开发者阅读与接入成本，不是完整安全审计或发布验收。

结论：小型示例可直接作为起步模板；Gallery 适合查组件，函数显示器与 SSH 适合参考完整应用。保持单头文件、应用持有状态和文件内页面函数的组织方式，不需要增加公共样式层。主要债务是大型示例访问内部实现、位置相关的分派规则，以及少数生命周期接口的限制。

## 逐模块结论

| 模块与主要入口 | 判断 | 维护重点 |
| --- | --- | --- |
| `examples/sidebar_demo.cpp`：`SidebarApp`、`draw_sidebar_app` | 易读，状态与页面分开，适合作为起步模板 | 已纠正过时构建命令；继续保留文件内行布局函数 |
| `examples/primitives_demo.cpp`：`sample`、`main` | 绘图调用直接，依赖少，适合查图元 | 循环组织相似项目合理，不必把每种图元提取成类 |
| `examples/pomodoro.cpp`：`Pomodoro`、`SecondTicker`、`draw_pomodoro` | 计时模型、唤醒线程、页面职责清楚 | 保留线程先退出、窗口后销毁的顺序，不改成分离线程 |
| `examples/gallery.cpp`：`App`、`kCards/kPages`、`draw_body`、`layout_gallery` | 分类绘制函数较好；目录与测量集中，扩组件仍需理解多处关联 | 已取消图标编号副本与整数运算；部分输入、抽屉和截图仍依赖内部实现 |
| `examples/function_viewer.cpp`：`fx::Parser`、`PlotCache`、`UiState`、`draw_plot` | 编译、求值、采样和界面有明确区段；数学代码有必要的算法注释 | 已限制外部索引；缓存键和聚合参数较长，扩绘图维度时需同步检查 |
| `examples/ssh_client.cpp`：`Profile`、`Session`、`App`、`frame` | 状态快照与后台工作分开；配置、会话和视图仍集中在一个大文件 | 已清理空入口和无效参数；后续优先拆配置解析，样式仍留在 `.cpp` |
| `examples/ssh_core.hpp`：`Engine`、`Terminal` | 网络 I/O owner 与无 UI 的终端解析边界清楚 | 引用型查询与线程安全快照并存，不能把所有查询都当成并发安全 |
| `examples/ssh_files.hpp`：`atomic_write_file`、`KnownHosts` | 本地持久化独立于 libssh2，可单独维护 | 配置替换与传输替换有不同语义，不应只为消除重复而合并 |
| `examples/ssh_credentials.hpp`：`SecretStore`、凭据读写函数 | 平台凭据集中，未污染 UI 核心 | 底层返回失败时由应用形成提示；保留新凭据回滚和旧格式迁移 |
| `aino.hpp`：`Style`、`Stack`、`Ui`、`Window` | 调用形态统一，默认参数与值语义便于使用；兼容接口增加发现成本 | 已补齐公开区标准库头、精简注释；不删除旧名称或改变 API 签名 |

## 已处理

| 程度 | 定位 | 原因与处理 |
| --- | --- | --- |
| High | `function_viewer.cpp::main`、`App::switch_mode` | 外部 `AINO_FN_CAT` 非法时预设为空，主回调可解引用空 `pre`；非法 `AINO_FN_MODE` 可进入 `custom_hist` 下标。入口统一限制范围，合法值行为不变 |
| Medium | `gallery.cpp::Glyph`、`icon`、导航与外观按钮 | 本地枚举复制公共枚举顺序，`Upload+i`、`Moon+i` 隐式绑定编号。改用 `SystemIcon`、显式数组，页面图标与页面元数据放在一起 |
| Medium | `gallery.cpp::time_wheel`、`draw_cards` | 自绘输入直接查询内部闸门。改用公开 `pointer_available()`，同时遵守测量与裁剪边界 |
| Low | `aino.hpp` 公开区 | 内联代码使用 `std::max`、`std::move`，却依赖其它标准库头的间接包含。显式包含 `<algorithm>`、`<utility>` |
| Low | `aino.hpp`：`WindowConfig`、`Window::wake`、动画与滚动说明 | 注释夹杂体积数字、实现历史与不安全的分离线程示例。缩短为当前契约，纠正 X11 字体说明，补明帧末捕获和线程寿命要求 |
| Low | `ssh_client.cpp`：`Clipboard::probe`、按钮辅助函数、`frame`；Gallery 常量 | 空初始化、未使用参数与仅定义未引用的 `kTitleH/kStylePage` 增加跟读成本。删除这些局部冗余，交互逻辑保留 |
| Low | `sidebar_demo.cpp`、`function_viewer.cpp` 文件头 | 手写链接命令缺少当前系统库。改为仓库已有的 CMake 目标命令 |

## 待处理与边界

以下为源码确认的限制或维护风险，本轮没有据此宣称已发生生产故障。

| 程度 / 性质 | 具体定位 | 影响与建议 |
| --- | --- | --- |
| Medium / 生命周期缺陷 | `aino.hpp::ScrollScope::operator=(ScrollScope&&)` | 覆盖活跃目标只转移字段，没有收尾原滚动区，可能遗留裁剪与布局栈。普通 `auto sc = ui.scroll(...)` 不走这条覆盖路径；暂避免覆盖活跃句柄。修复需明确嵌套和关闭顺序，发布时补对应探针，不宜直接加一次 `close()` |
| Medium / 维护风险 | `gallery.cpp::draw_toolbar`、`build_ui`、`dump_canvas_bmp` | 搜索焦点重算内部 ID，窄屏导航使用 `SeqFrameEnd/gate_claim`，截图读取 canvas。内部变化可能要求同步修改示例。先区分交互能力缺口与诊断代码，再设计最小公开入口，避免暴露整个 Impl |
| Medium / 扩展风险 | `gallery.cpp::draw_body`、`kPages` 的 `first/count` | 组件范围按枚举顺序分派，新增或移动组件需同步改分类区间。后续可将明确的绘制回调放进卡片清单，并用具名 CardId 表示分类起点；布局和样式继续留在该文件 |
| Medium / 维护风险 | `ssh_client.cpp::JsonScan`、`g_block_input`、`Profile` 持久化 | 配置解析、迁移和页面共处一文件，输入禁用依赖文件级状态。优先把配置读写形成独立模块；再让页面显式传递禁用状态，分两步保持当前弹窗行为 |
| Medium / 使用风险 | `ssh_core.hpp::Engine::cwd`、`transfer`、`set_wake` | 裸引用查询及回调设置需要调用时序保证；现有客户端用快照和后台传输线程，并非所有访问都会竞争。后续先固定线程契约，再按需求补只读快照，不直接删兼容接口 |
| Low / 可读性建议 | `function_viewer.cpp::PlotCache::update`、`Ctx` | 16 项缓存键和长聚合初始化容易漏字段。下次扩展采样维度时，用具名键字段与统一模式映射替换，不为缩短行数再包一层通用框架 |
| Low / 接口限制 | `aino.hpp::Style`、`Color::none` | 透明色同时作为“未设置”哨兵；链式方法返回引用。保存样式用 `Style style = Style().primary()`，不要绑定到临时对象的引用。若要支持显式全透明覆盖，应另行设计兼容的设置标记 |

重复的示例主题函数、`panel/card`、样式别名、显式与 RAII 滚动接口不是无效代码：前者遵守示例独立性，后者服务兼容性。旧密码解码用于迁移，也应保留。算法与线程关闭顺序的注释有维护价值；不做逐行注释或按注释数量清理。

## 建议顺序

1. 本轮：局部清理、非法配置约束、必要契约注释与阅读顺序；保持公开签名、页面排版和业务数据。
2. 下一批：先解决滚动句柄覆盖契约，再减少 Gallery 对内部交互层的依赖；这些变化需要单独评估行为与发布验证。
3. 随功能演进：将 Gallery 分派归入清单、拆 SSH 配置模块、具名化采样缓存键。每次只调整一个职责，不把所有示例统一成应用框架。

## 本轮验证

六个 Windows 示例通过 Release 编译。实窗查看了 150% DPI 下 Gallery 浅色默认窗口、深色 640×480 导航，以及函数显示器 `AINO_FN_CAT=999 / AINO_FN_MODE=999` 的深色最小窗口，非法值已收敛为可用模式。

未运行 CTest、回归探针或批量验证脚本。代码复查确认公开签名和图标映射保留，未增加依赖或公共样式层。远程 SSH 连接、Linux/macOS 原生交互和滚动句柄覆盖场景不属于此次手工验收范围。
