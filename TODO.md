# 新功能

## 移动端与 QML 前端改造

### P0：基础架构与跨平台路径

- [x] 使用 `QStandardPaths::AppConfigLocation` 统一配置文件路径。
- [x] 将最近世界记录从 JSON cache 改为 `recent.ini`（`QSettings::IniFormat`）。
- [x] 将日志目录移到应用配置目录下。
- [x] 将内置方块颜色表、生物群系颜色表改为 Qt Resource System 加载。
- [x] 将翻译文件改为从 Qt Resource System 加载。
- [x] 增加统一的 `AppPaths` 单元测试，覆盖 Windows、Android、iOS 路径策略。
- [x] 区分持久化配置目录、可删除缓存目录和用户导入/导出目录。
- [x] 清理仍暴露相对路径语义的旧 `constant::*_FILE_PATH` 接口。

### P0：世界数据访问层

- [ ] 从 `LevelPathManager` 中拆出 `WorldRepository`。
- [ ] 从 `LevelPathManager` 中拆出 `RecentWorldStore`。
- [ ] 让世界扫描、世界元数据读取和最近记录保存分别拥有独立接口。
- [ ] 将世界扫描改为异步服务，向前端暴露 `loading`、`progress` 和 `error` 状态。
- [ ] 统一世界元数据模型，避免 Widgets/QML 各自格式化名称、版本、时间和大小。
- [ ] 增加世界扫描去重和路径失效处理。

### P1：应用状态与前端解耦

- [ ] 新增 `AppController`，统一管理当前页面、当前世界和打开/关闭世界命令。
- [ ] 新增 `WorldListViewModel`，让 Widgets 和 QML 共享同一个世界列表模型。
- [ ] 新增 `SettingsViewModel`，将设置读写从具体窗口中移出。
- [ ] 将页面导航从 `LevelTabWidget` 中抽离，使桌面端可以使用 Tab，移动端可以使用 StackView。
- [ ] 所有 ViewModel 只依赖核心服务，不依赖 `QWidget`、`QDialog` 或具体 QML 页面。
- [ ] 统一错误、忙碌、进度和确认操作的信号模型。

### P1：地图会话与渲染器解耦

- [ ] 从 `MapHost` 中抽出不依赖 QWidget 的 `MapSession`。
- [ ] 将地图视图状态、图层、选择区和地图操作集中到 `MapSession`。
- [ ] 从 `MapHost` 中抽出 `MapController`，负责跳转、选择、导入、导出和编辑命令。
- [ ] 保持 `CpuMapWidget` 与 `GpuMapWidget` 作为桌面端渲染器，不让它们拥有核心地图状态。
- [ ] 为 QML 地图预留独立渲染适配层，不直接复用 QWidget 渲染控件。
- [ ] 将地图操作中的文件对话框、消息框和页面跳转移到前端适配器。

### P1：移动端文件访问

- [ ] 新增 `FileAccessService` 抽象文件选择、打开、导入和导出。
- [ ] 支持普通本地路径、Android `content://` URI 和导入到应用沙盒的文件。
- [ ] 不让 `bedrock-level` 直接依赖平台文件选择器或 Qt UI 类型。
- [ ] 明确世界打开、临时解包、导出和权限失败时的错误状态。
- [ ] 设计 Android/iOS 的世界导入流程和权限请求流程。

### P2：QML 页面框架

- [ ] 建立 QML 主题 token：颜色、字号、间距、圆角和触控尺寸。
- [ ] 完成桌面侧栏与移动端底部导航的响应式切换。
- [ ] 完成世界列表页的真实加载、刷新、打开和长按菜单。
- [ ] 完成设置页与 `SettingsViewModel` 的绑定。
- [ ] 完成地图页的占位交互，再接入 `MapSession`。
- [ ] 为小屏、横屏、平板和高 DPI 场景建立视觉回归检查。
- [ ] 统一 QML 组件的最小触控区域为 44–48 px。

### P2：性能与测试

- [ ] 世界扫描和缩略图读取不阻塞 UI 线程。
- [ ] 增加 ViewModel 的无窗口单元测试。
- [ ] 增加路径服务和最近世界存储的跨平台测试。
- [ ] 测试世界数量较多、路径失效、权限拒绝和损坏 `level.dat` 的情况。
- [ ] 在 Windows、Android 和 iOS 上分别检查启动、导入、导出和退出时的数据保存。

# BUGS
