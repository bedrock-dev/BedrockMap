# 架构审查与调整清单

审查日期：2026-10-24。

本文记录移除 Qt Widgets 后的全项目静态架构审查，覆盖 `bedrock-level`、Core、Qt Quick 及其依赖边界。内容是待实施建议，不代表已完成的行为；本轮不讨论具体 bug，也未做运行验证。当前实现说明见 [BedrockMap.md](BedrockMap.md)，总工作入口见 [TODO.md](../TODO.md)。

## 总体判断

保留 `bedrock-level → BedrockMapCore → Qt Quick` 三层结构。主要调整方向是收拢一次操作的完整职责、删除重复封装、明确会话与页面的状态归属。当前没有充分理由继续细分 CMake targets，也不应按文件行数机械拆分类或组件。

优先级表示实施收益和依赖关系，不表示 bug 严重程度：

- **优先**：跨层协议或明显重复的职责，建议在继续扩展功能前处理。
- **后续**：边界整理，可随相关功能渐进实施。
- **清理**：低成本删除或合并，实施前复核当前调用关系。

## 修改项

### A01：统一修改完成后的更新流程〔优先〕

- [ ] 收拢区块修改后的坐标索引、概览、dirty、区域缓存失效和 terrain provider 通知。

现状：这些工作分布在 `ChunkEditService`、loader、操作函数、`MapController` 和 `WorldSession`。`ChunkOperator` 的 `invalidate_tiles` 参数还要求调用方决定谁负责收尾，新增操作需要了解多层内部约定。

建议：操作返回明确的修改范围和结果，由统一入口完成派生数据和通知更新；批量操作结束后统一处理。保留 `ChunkStorage` 的存储职责以及 scheduler 的渲染职责，不把所有功能合成一个大类。

完成标准：新增修改操作时，无需在多个调用层分别补缓存失效和刷新通知；失败或部分完成时也能描述实际修改范围。

依据：[ChunkEditService](../src/loader/chunkeditservice.cpp)、[ChunkOperator](../src/include/loader/chunkoperator.h)、[MapController](../src/controller/mapcontroller.cpp)。

### A02：统一会话保存协议与关闭协调〔优先〕

- [ ] 统一会话保存的受理、选路径、完成和失败语义。
- [ ] 在协议明确后，将关闭协调与相关对话框从 `Main.qml` 抽出。

现状：`WorkspaceSession::commit()` 返回 `bool`，NBT 会话表示同步保存结果，世界会话表示是否受理异步保存；`saved/saveFailed` 仅定义在世界会话上。`Main.qml` 因而需要识别类型、访问具体 document 并分别处理结果。

建议：公共会话接口应足以驱动通用保存与关闭流程，明确未命名文档需要路径的情况。关闭组件负责提示、等待保存和继续关闭；`Main` 保留窗口与布局。避免为此引入通用路由或工作流框架。

完成标准：关闭流程不再根据 `kind` 分别解释 `commit()` 的返回值，也不直接操作具体文档的保存实现。

依据：[WorkspaceSession](../src/include/controller/workspacesession.h)、[WorldSession](../src/controller/worldsession.cpp)、[NbtSession](../src/controller/nbtsession.cpp)、[Main.qml](../src/qml/Main.qml)。

### A03：合并世界发现链路〔优先〕

- [x] 统一来源目录生成、扫描和元数据读取，移除 `LevelPathManager` 和 `WorldRepository` 的重复编排。
- [x] 最近世界的元数据读取复用同一异步链路。

原问题：`makeWorldSources()` 已解析默认安装和 Levi 路径，`scanWorldSource()` 遇到默认来源后又创建 `LevelPathManager` 重新发现路径，再交给 `WorldRepository` 扫描。`WorldListModel` 还直接读取最近世界元数据。

已调整为“生成带标签的平铺路径清单 → 统一扫描目录 → 读取世界元数据”。每个路径只有路径和来源标签，实际可读性在扫描时检查；移除旧扫描封装、预留访问方式、重复分类字段及没有 QML 使用方的分类列表。保留 `WorldScanService` 的异步职责，model 接收全部世界和最近世界的结果。来源标签和平台路径约定见 [世界扫描](BedrockMap.md#世界扫描)。

验证：Windows Debug 和 Android arm64 Release 的 `BedrockMapQml` 目标构建通过；Android 未打包或部署 APK。翻译 XML、本文链接和 `git diff --check` 检查通过。本项未新增或修改测试。

完成标准：修改一种安装路径规则只需改一处；默认来源、自定义来源和最近世界共用元数据读取逻辑。

依据：[worldsource.cpp](../src/loader/worldsource.cpp)、[worldscanservice.cpp](../src/loader/worldscanservice.cpp)、[worldlistmodel.cpp](../src/model/worldlistmodel.cpp)。旧 `levelpathmanager` 和 `worldrepository` 文件已删除。

### A04：分离区块编辑和区域体素预览〔优先〕

- [ ] 为区域预览提供轻量控制器，复用体素构建函数。
- [ ] 区块编辑器按实际打开的页签准备昂贵数据。

现状：`WorldSession` 拥有两个完整的 `ChunkEditorController`，分别用于编辑和区域预览；后者也携带三份 NBT document、统计与地形层等状态。单区块加载一次性准备 NBT、统计、全部地形明细和体素网格，各页面的加载成本绑定在一起。

建议：区域预览只维护区域加载、体素场景、进度和导出所需状态；共享数据构建函数。编辑器继续聚合相关页签，不必为每个页签创建一套 controller/service。

完成标准：区域预览不再依赖完整区块编辑器类型；只查看 NBT 或统计时不强制生成体素网格和全部地形明细。

依据：[WorldSession](../src/include/controller/worldsession.h)、[ChunkEditorController](../src/controller/chunkeditorcontroller.cpp)、[VoxelSceneController](../src/include/controller/voxelscenecontroller.h)。

### A05：统一 terrain 需求请求〔优先〕

- [ ] 将视口、维度、图层、LOD 和 overview 模式收拢到一次请求。

现状：Quick item 分别调用 `setOverviewMode()`、`setViewport()`、`setBlocksPerTexel()`；provider 的多个入口各自重置交付状态和扫描队列，调用顺序构成隐含协议。

建议：扩展现有 `TerrainViewportRequest`，由 provider 比较前后需求并决定复用、失效或重建，明确请求与交付的代际关系。保留 region bake、tile sampling、GPU 上传三段分工。

完成标准：调用方一次提交完整需求，不必安排多个 setter 的先后顺序，也不直接管理 provider 的扫描重置。

依据：[TerrainDataProvider](../src/include/render/terraindataprovider.h)、[TerrainViewportRequest](../src/include/render/terraintiles.h)、[TerrainQuickItem](../src/qml/terrainquickitem.cpp)。

### A06：合并过薄的 MapSession〔清理，已完成〕

- [x] 让 `WorldSession` 直接拥有 `MapViewState`，`MapController` 接收 `MapViewState*`，移除 `MapSession` 转发层。

原问题：`MapSession` 只拥有一个 `MapViewState`，转发少量 setter 和 selection 信号，没有独立生命周期或业务约束。

调整时保留状态与命令接口的区分，并更新 benchmark 中的构造方式。

依据：[MapController](../src/include/controller/mapcontroller.h)、[WorldSession](../src/include/controller/worldsession.h)、[region_import_test.cpp](../benchmark/region_import_test.cpp)。

### A07：明确会话与页面状态归属〔后续〕

- [ ] 明确切换世界时各项页面状态的保留策略。
- [ ] 为 section 使用单一状态来源，消除 `restoringSection` 镜像同步。

现状：`SessionHost` 每种会话类型保留一个页面实例，切换世界时复用。`WorldWorkspace` 将 section 存入会话并双向同步，同时重置部分面板状态、保留其他状态，策略分散在生命周期回调中。

建议：随世界恢复的状态放会话；共享布局偏好放页面或设置；临时输入留组件。明确工具面板可见性、面板宽度、诊断开关及导入状态的归属。现有 `SessionHost/OpenWorldPage/WorldWorkspace` 各有生命周期职责，可以保留。

依据：[SessionHost.qml](../src/qml/SessionHost.qml)、[WorldWorkspace.qml](../src/qml/WorldWorkspace.qml)。

### A08：按交互职责整理 MapCanvas〔后续〕

- [ ] 抽出地图输入与惯性、导入交互、诊断显示等完整职责。

现状：`MapCanvas` 同时组织输入、惯性、菜单、文件对话框、导入摆放和诊断显示，相关修改需要穿过大量无关内容。

建议：优先抽出少数完整组件，菜单随对应命令整理。相机、选区和世界修改继续归现有 controller，避免在 QML 新增一份地图状态。拆分后的组件应通过清晰属性和信号协作，减少跨组件访问内部 id。

依据：[MapCanvas.qml](../src/qml/MapCanvas.qml)。

### A09：分离 NBT 文档与数据库绑定，复用文件操作〔后续〕

- [ ] 将 LevelDB key 映射和差异生成移到世界全局数据一侧。
- [ ] 复用 NBT 页面的导入、导出和替换确认流程。

现状：`NbtDocumentController` 同时维护文件路径、NBT roots、原始数据库 keys 和删除差异，区块内存文档、结构预览也暴露数据库相关能力。独立、全局、区块 NBT 页面重复实现文件对话框与确认流程。

建议：文档继续负责内容、选择和 dirty；世界数据绑定负责数据库身份与差异。QML 抽出小型文件操作组件，保留独立文档 `loadFile` 和宿主文档 `importFile` 的语义差异。继续保留 `NbtDocumentView` 与 `NbtEditorSurface` 的展示/编辑分工，不合成万能页面。

依据：[NbtDocumentController](../src/include/controller/nbtdocumentcontroller.h)、[GlobalNbtPage.qml](../src/qml/GlobalNbtPage.qml)、[NbtEditorPage.qml](../src/qml/NbtEditorPage.qml)、[ChunkEditorPanel.qml](../src/qml/ChunkEditorPanel.qml)。

### A10：解耦实体区域数据与图标资源〔后续〕

- [ ] 用实体类型 ID、位置和计数表示区域数据，在 overlay 准备阶段解析图标及展示形式。

现状：`ChunkRegion` 用 `const QImage*` 作 key 保存实体位置和计数，烘焙阶段通过注入的 `ResourceManager` 解析图标并读取实体显示模式，区域数据仍绑定展示资源。

完成标准：更换图标或实体展示形式时，不需要深入区域数据烘焙；图片资源生命周期由展示侧负责。

依据：[ChunkRegion](../src/include/loader/chunkregion.h)、[readChunkActors](../src/loader/chunkregion.cpp)、[TerrainDataProvider](../src/render/terraindataprovider.cpp)。

### A11：下沉纯 Minecraft 数据变换〔后续〕

- [ ] 将结构裁剪、实体筛选、坐标转换和结构重建等纯规则移入 `bedrock-level`。

现状：`mcstructureio.cpp` 在文件适配之外，还承担不依赖 Qt 的数据变换。

建议：底层库提供普通数据函数；应用层处理文件、异步任务、错误展示和场景构建。依赖应用缓存与修改流程的世界编辑操作继续留在 Core，无需整包下沉。保持子模块独立且不依赖 Qt。

依据：[mcstructureio.cpp](../src/loader/mcstructureio.cpp)、[mcstructure.h](../bedrock-level/src/include/mcstructure.h)。

### A12：渐进明确底层库 API 所有权〔后续〕

- [ ] 优先整理跨库边界中返回拥有对象的 API。

现状：`get_chunk()`、NBT `copy()`、`parse_all()` 等通过裸指针转移所有权，容器中也存在拥有和借用指针，上层需要反复接管并了解释放约定。

建议：拥有结果使用 `unique_ptr`，借用结果使用指针、引用或 span。优先调整边界，内部实现渐进迁移，避免一次性重写整个 NBT 对象体系。

依据：[bedrock_level.h](../bedrock-level/src/include/bedrock_level.h)、[nbt.h](../bedrock-level/src/include/nbt.h)、[palette.h](../bedrock-level/src/include/palette.h)。

## 其他清理与适度拆分

以下调用情况基于本次审查快照，实施前需要复核，尤其要检查动态 QML 加载和资源入口。

- [x] **C01**：删除无调用的 `src/include/loader/datakeyindex.h` 与 `src/loader/datakeyindex.cpp`，包括 `DataKeyIndex` 及其专用数据类型。
- [x] **C02**：删除无调用的旧占位页 `src/qml/MapPage.qml`、`src/qml/MapsPage.qml`。
- [x] **C03**：删除 [chunkregion.h](../src/include/loader/chunkregion.h) 中无使用方的 `TaskBuffer` 及相关头文件依赖。
- [x] **C04**：删除无调用的 [ChunkSelection](../src/include/model/chunkselection.h) 的 `draw()`，移除选区状态对 QPainter 绘制职责的依赖。
- [ ] **C05**：将 [voxel_mesh.h](../src/include/render/voxel_mesh.h) 中屏幕拾取和选区手柄交互移到独立文件，保留网格构建职责；Quick 渲染器的资源管理可提取私有 helper，继续保持一个渲染 item。
- [ ] **C06**：按现有分类拆分 [SettingsPage.qml](../src/qml/SettingsPage.qml) 内容组件，保留公共 `SettingGroup/SettingRow`，无需每个设置项一个 view model。
- [ ] **C07**：将 [AppController](../src/controller/appcontroller.cpp) 中的 Android 世界复制和 JNI 实现提取到平台文件，保留应用导航与会话命令入口，无需引入通用文件访问框架。

### C01–C04 实施记录（2026-10-24）

已删除上述无调用代码及绘制依赖，并通过 `lupdate` 从中英文翻译中移除两个旧页面对应的 7 条条目。源码、构建入口、资源清单和翻译检索未发现残留引用。`MapSession` 合并已在后续 A06 中完成。

验证：`BedrockMapQml` 构建、`BedrockMapRegionImportTest` 和 `BedrockMapChunkCoordsBlocksTest` 均通过，文档链接及 `git diff --check` 检查通过。原 `build/BedrockMapQml.exe` 被占用，链接无法覆盖，因此在独立目录 `build/cleanup-check` 完成构建和测试。

## 应保留的设计

- `bedrock-level`、Core、Qt Quick 的三层依赖边界，以及当前构建目标数量。
- `WorkspaceSession` 及具体会话：承担异构文档的生命周期和 dirty 状态。
- `ChunkStorage`、region scheduler/cache：存储、调度、缓存的职责不同。
- `NbtDocumentView` 与 `NbtEditorSurface`：展示和编辑交互有明确分工。
- 集中的 `SettingsViewModel` 与提供样式、触控尺寸的小型公共控件。
- 异步渲染管线中的不可变快照、generation/revision 校验和必要的线程边界。
- `WorldCardDelegate` 虽短，但承担 GridView/ListView 共用的 role 适配，不应仅因行数少就删除。

## 建议实施顺序与验证

1. **删除与合并**：C01–C04、A06。先减少无效代码和转发层。
2. **世界发现**：A03，统一来源、扫描和最近世界元数据读取。
3. **跨层协议**：A01、A02、A05，分别明确修改、保存和渲染请求的完整边界。
4. **编辑器与页面**：A04、A07–A09，结合保存协议与状态归属调整组件。
5. **渐进整理**：A10–A12、C05–C07，随相关功能推进。

每项完成后更新本文复选框与 TODO。涉及子模块的改动单独记录，避免应用层整理与底层 API 大迁移混成一个提交。

验证应围绕行为边界：世界发现覆盖不同来源及最近列表；修改流程覆盖索引、概览与缓存刷新；保存流程覆盖独立文档、世界、多脏会话和退出取消；渲染请求覆盖平移、缩放、图层/维度切换和后台会话；页面拆分覆盖世界切换、桌面和触屏交互。默认使用构建和必要的人工检查；仅在用户明确要求时新增或修改测试。
