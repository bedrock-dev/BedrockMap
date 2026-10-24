# BedrockMap 架构

BedrockMap 是 Qt 6 / Qt Quick 应用，使用 C++20。`BedrockMapQml` 是唯一 GUI 目标；`BedrockMapCore` 提供世界访问、会话、编辑和渲染数据。命令行基准测试在 `benchmark/`，Minecraft 数据格式实现位于独立子模块 `bedrock-level/`。

## 构建与运行

Windows 开发环境需要 CMake、Ninja、MinGW 和 Qt 6，并通过 `QT_ROOT` 指向 Qt 安装目录。克隆时初始化子模块：

```powershell
git clone --recursive https://github.com/bedrock-dev/BedrockMap.git
cd BedrockMap
.\scripts\build.ps1
.\scripts\run.ps1
```

`build.ps1 -Rel` 构建 Release；`build_run.ps1` 先构建再运行。Android 构建使用 `scripts/build_android.ps1`。顶层 CMake 同时提供 `BedrockMapBenchmark`，但它不属于 GUI。

## 目录与依赖

| 目录 | 职责 |
| --- | --- |
| `bedrock-level/` | 不依赖 Qt 的 LevelDB、NBT、区块和实体格式库 |
| `src/base/` | 配置、平台路径、更新检查 |
| `src/loader/` | 世界扫描、区块坐标索引、异步 LevelDB 访问、区块修改 |
| `src/model/` | 地图状态、世界列表、NBT 树、日志和通知模型 |
| `src/controller/` | 应用与工作区会话、地图、NBT、区块和体素操作 |
| `src/render/` | 区域烘焙调度、tile 采样、叠加层数据和体素网格 |
| `src/qml/` | 应用入口、QML 页面与控件、Qt Quick Scene Graph/RHI 绘制 |
| `res/qml/` | QML 图标；`res/icons/` 保存实体和 NBT 等数据图标 |

`BedrockMapCore` 依赖 Qt Core/Gui/Concurrent/Network 和 `bedrock-level`，不依赖 Qt Quick。`BedrockMapQml` 依赖 Core、Qml、Quick 和 Svg。QML 页面与图标编入应用资源；地形和体素 shader 由 `qt_add_shaders` 编译。

## 会话与页面

`AppController` 处理应用页面导航及打开命令，并通过 `SessionController` 管理多个 `WorkspaceSession`。世界、独立 NBT 文件和 `.mcstructure` 文件各自有会话类型。会话负责数据生命周期、dirty 状态和保存能力；QML 负责窗口、对话框和当前展示状态。

每个 `WorldSession` 拥有自己的 `AsyncLevelLoader`、`MapSession`、`MapController`、`TerrainDataProvider`、全局 NBT 与区块编辑控制器。`Main.qml` 是应用壳，`SessionHost.qml` 保留每种会话的页面实例，`OpenWorldPage.qml` 展示世界加载态，`WorldWorkspace.qml` 组织地图、全局 NBT 与工具面板。

地图相机、维度、图层和选择由 `MapView`/`MapController` 管理。QML 组件只将输入交给控制器，不在渲染节点内维护另一份地图状态。

## 世界与渲染数据流

1. `WorldSession` 打开世界，`AsyncLevelLoader` 初始化 LevelDB，并按需建立区块坐标索引。
2. `TerrainQuickItem` 根据相机和窗口尺寸请求可见区域及 LOD。`TerrainDataProvider` 推动区域烘焙、tile 采样和交付。
3. `TerrainQuickItem` 在 Qt Quick Scene Graph/RHI 中维护地形纹理与 page table。`MapOverlayQuickItem` 绘制实体、网格、选择等地图叠加内容。
4. 区块或 NBT 修改通过会话控制器进入 loader；相关区域缓存失效后按需重新烘焙。

区域加载、tile 采样和体素网格构建不得阻塞 GUI 线程。Scene Graph 渲染线程只处理绘制资源，不直接访问 LevelDB。渲染细节见 `docs/qmlWorldRendering.md`；如果该文档与代码有差异，以当前代码为准。

## 编辑与配置

`GlobalDataController` 和 `NbtDocumentController` 提供全局 NBT 与独立文件编辑；`ChunkEditorController` 提供区块数据与体素预览，`McstructureController` 处理结构文件。`RegionImportController` 负责地图区域导入。关联的世界修改由 `WorldSession` 统一提交。

运行配置由 `src/base/config.cpp` 通过 `QSettings` 读写，QML 使用 `SettingsViewModel`。用户可见字符串使用 Qt 翻译机制，源文件是 `translations/zh_CN.ts` 与 `translations/en.ts`；`scripts/build.ps1` 会运行 `lupdate`。
