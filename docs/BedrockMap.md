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

Android 脚本默认生成 arm64-v8a 的常规 Release 发布包，不额外启用 LTO 或 Java 混淆裁剪；完整中文字体使用 zlib 资源压缩。Qt 部署与 Gradle 打包分开，部署阶段移除 Basic 以外的控件样式、QML 调试插件和 Widgets 样式插件，并检查保留库不再链接被移除的库；同步清理 `libs.xml`，仅剥离部署副本的符号，不修改 Qt/NDK 安装。APK 重新压缩原生库和 DEX，`resources.arsc` 保持未压缩，完成 zipalign 后签名并验证。库采用安装时解压，优先减小 APK 文件而非安装占用；脚本最后输出实际包体积。

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
| `res/icons/` | `ui/` 保存界面 SVG 图标，其余目录保存实体、NBT、方块实体和村庄图标 |
| `res/fonts/` | 界面与编辑器字体，以及字体来源和许可文件 |

`BedrockMapCore` 依赖 Qt Core/Gui/Concurrent/Network 和 `bedrock-level`，不依赖 Qt Quick。`BedrockMapQml` 依赖 Core、Qml、Quick 和 Svg。QML 页面与图标编入应用资源；地形和体素 shader 由 `qt_add_shaders` 编译。

`base/apppaths.h` 和 `src/base/apppaths.cpp` 中的 `app_paths` 命名空间负责应用配置、最近记录、缓存、日志和导入导出目录，并按需创建目录；运行时配置继续由 `config.h/.cpp` 管理。

应用资源统一使用 `/bedrockmap` 前缀，CMake 通过 `qt_add_resources` 的 `PREFIX` 与 `BASE` 映射路径，不手工生成 `.qrc`。C++ 文件接口使用 `:/bedrockmap/...`，QML 图片 URL 使用 `qrc:/bedrockmap/...`。

| 资源路径 | 来源 |
| --- | --- |
| `/bedrockmap/icons/ui/` | `res/icons/ui/` 界面图标 |
| `/bedrockmap/icons/` 下的其他图标 | `res/icons/` 下对应的文件 |
| `/bedrockmap/icons/app.svg` | 仓库根目录的 `BedrockMap.svg`，与打包和图标生成脚本共用 |
| `/bedrockmap/fonts/` | `res/fonts/` 中的字体和字体许可 |
| `/bedrockmap/data/colors/` | `bedrock-level/data/colors/` 中的颜色表；基准程序使用相同路径 |
| `/bedrockmap/i18n/` | `translations/` 编译生成的 `.qm` 文件 |
| `/bedrockmap/shaders/` | `src/qml/shaders/` 编译生成的 `.qsb` 文件，路径不包含源码目录 |

桌面端界面使用系统字体；Android 和 iOS 使用内置 Noto Sans SC，避免中文区域字形回退差异。Noto Sans SC 及其许可只打包到移动端，所有平台的编辑器均保留 JetBrains Mono。

`BedrockMap` QML 模块由 `qt_add_qml_module` 自动注册，资源路径为 `qrc:/qt/qml/BedrockMap/`，入口通过 `loadFromModule("BedrockMap", "Main")` 加载。Qt Quick 渲染项和 `Navigation` 在自己的头文件声明 `QML_ELEMENT`；Core 类型的 `QML_FOREIGN` 声明集中在 `src/qml/qmltypes.h`，保持 Core 不依赖 Qt Qml。

`AppController` 位于 Core，是唯一的 C++ 应用单例，通过 `AppController::instance()` 获取。它持有设置、世界列表、通知和日志，QML 分别通过 `App.settings`、`App.worlds`、`App.notifications` 和 `App.logs` 使用。`main()` 在配置和语言初始化完成后创建应用控制器，并转交提前初始化的日志模型；退出时先销毁 QML 引擎，再销毁会话和应用服务，最后关闭日志。`Navigation` 是 `Main.qml` 持有并传给侧栏、底栏和工具页的普通对象；界面样式 `Theme.qml` 保留 QML 单例声明。

## 世界扫描

世界发现采用“收集路径 → 扫描合法世界 → 读取元数据 → 发布列表”的流程。`WorldSource` 是平铺的路径记录，只包含目录和来源标签；扫描器统一在扫描时检查目录是否存在、可读，不预存权限状态。

| 平台 | 路径标签 | 默认路径来源 |
| --- | --- | --- |
| PC | 正式版、测试版 | UWP 存储目录，以及对应现代安装的 `Users/<用户>/games/com.mojang/minecraftWorlds` |
| PC | 网易版 | `%APPDATA%/MinecraftPC_Netease_PB/minecraftWorlds` |
| PC | Levi | Levi 配置中各版本安装目录下的用户存档目录；由扫描 Levi 设置控制 |
| 安卓 | 旧版 | `/data/user/0/com.mojang.minecraftpe/games/com.mojang/minecraftWorlds`；扫描受实际访问权限约束 |
| 安卓 | 正式版内部 | `/data/data/com.mojang.minecraftpe/files/games/com.mojang/minecraftWorlds` |
| 安卓 | 正式版外部 | `/storage/emulated/0/Android/data/com.mojang.minecraftpe/files/games/com.mojang/minecraftWorlds` |
| 所有平台 | 软件内部 | 应用自己的 `AppLocalDataLocation/worlds` |
| 所有平台 | 外部 | 设置中添加的自定义目录，可有多个 |

路径生成和通用扫描函数位于 `src/loader/worldsource.cpp`。扫描路径统一表示存档的父目录，只检查其直接子目录，不将扫描路径本身作为世界，也不递归搜索任意深度。合法世界需要 `level.dat` 文件、`db` 目录以及可解析的 `level.dat`。扫描与 `LevelOperator::isCompleteWorld()` 共用 `LevelOperator::loadLevelDat()` 校验，扫描复用解析结果读取名称和版本；单独校验不会遍历世界文件或打开 LevelDB。元数据包括名称、兼容版本、文件总大小、最新文件修改时间及 XOR 标记；世界继承发现它的路径标签。

`WorldScanService` 在后台执行扫描，并按规范化的世界路径去重；重叠来源保留先发现的标签。最近世界的元数据也在同一任务中读取，优先复用已扫描结果，保留历史顺序；仅在历史中出现的世界不会额外进入全部世界列表。扫描期间的新刷新请求合并为一轮后续扫描。

`WorldListModel` 发布全部世界、最近世界、加载状态及扫描结果中实际存在的来源选项。全部世界通过代理模型按来源完整标签筛选，默认空筛选表示 `All`；“All worlds” 右侧的按钮组切换来源，来源较多时横向滚动。筛选不重新扫描，最近世界保持独立；刷新后已选来源消失时回到 `All`。扫描不统计或汇报百分比进度，界面只显示正在扫描的状态。`RecentWorldStore` 保存最多 10 条历史路径。设置页、世界卡片和来源筛选共用标签文本，QML 负责翻译。扫描不打开 LevelDB 读取区块；世界打开后的区块索引属于另一条链路。

SAF 打开或导入世界不修改自定义扫描路径；复制到应用内部目录的世界由“软件内部”来源扫描，自定义扫描路径仅由用户在设置中维护。内部副本的目录名取源路径或完整编码 content URI 的 SHA-256 前 16 位十六进制字符；已有使用完整哈希目录名的副本继续复用原路径。

仅“软件内部”世界的操作菜单提供“删除世界”。确认窗显示世界名称、目录及不可恢复提示；必须先关闭该世界及其目录内文件的会话。`LevelOperator` 验证目标是应用 `worlds` 目录下的真实直接子目录，拒绝符号链接和目录联接，并在后台递归删除。删除期间禁止导入和打开文件；完成后刷新世界列表并清理失效的最近记录，失败时显示错误。

Android 声明 `.nbt`、`.nbts`、`.mcstructure` 和 `.mcworld` 的外部打开关联，`FileOpenHandler` 通过 Qt 的 URL handler 接收首次启动及后续的 `ACTION_VIEW`，界面就绪后交给应用控制器的 `openFile()`。`.mcworld` 导入世界，`.mcstructure` 进入结构预览，其余文件进入 NBT 编辑器；保留完整 content URI 交给 `QFile`。`platform::fileDisplayName()` 通过 Android `ContentResolver` 查询 `OpenableColumns.DISPLAY_NAME`，供标题和后缀分流共用；元数据不可用时回退到 Qt 文件名、URI 名称或原始输入，避免空标题。NBT 文档在加载或另存为时保存标题，属性读取不反复查询提供者。文档 ID 不含文件名时依靠二进制和 ZIP MIME 关联，因此部分文件管理器也可能为其他二进制或 ZIP 文件显示 BedrockMap。暂不支持 `level.dat` 的专用文件格式。

界面的文件打开入口使用 `FileOpenDialog.selectedUrl`。Qt 6.11 的 `FileDialog` 会通过 `urlToLocalFileOrQrc()` 将 Android `content://` 选择结果转换为空路径，再按文件不存在拒绝更新 `selectedFile`。GUI 层的 `FileOpenDialog` 继承该对话框，保留原生选择器、URI 授权和过滤器，仅在 Android 原生选择完成时直接保存平台 helper 返回的 URL；桌面端继续使用 Qt 原有流程。Qt Quick Dialogs 私有接口依赖只位于 GUI 目标。

新建 NBT 首次保存时根据空 `filePath` 直接打开另存为对话框，不先调用 `save()` 触发缺少路径的错误通知。已有目标的文档直接保存，写入失败时报告原始错误。

NBT 文件读写通过 `platform::openDocumentFile()` 打开。Android `content://` 文档先由 `DocumentMetadata.grantedUri()` 从系统持久授权记录中恢复原始 URI 编码，再调用 `ContentResolver.openFileDescriptor()`，将描述符交给 `QFile` 自动关闭，绕过 Qt 6.11 重编码文档 ID 后误判 URI 授权失效的问题（例如重名文件 `1.nbt (1)`）。平台异常保留实际错误信息；写入并 flush 成功后才清除未保存状态。本地文件继续使用普通 `QFile`。

所有平台均可在文件选择器打开或拖入 `.mcworld`。`LevelOperator::extractToInternal()` 在后台通过 Qt Core 的私有 `QZipReader` 解压到内部哈希目录，拒绝越界路径和符号链接，已有有效副本直接复用，不覆盖修改。解压后直接尝试打开世界，打开失败或解压中途失败均保留已落盘内容；不会自动删除内部目录。文件须使用普通 ZIP 格式，当前读取器不支持 ZIP64，压缩包和单文件大小均须小于 2 GiB。SAF 提供者返回不可定位的管道时，先流式缓存压缩包到临时文件再读取 ZIP。

世界卡片和列表的“导出为 mcworld”通过保存对话框选择目标，默认使用世界名称。`LevelOperator::exportMcworld()` 在后台通过世界的 Env 读取文件，以 `QZipWriter` 打包到临时文件，再写入本地路径或 Android SAF 文档；本地目标使用 `QSaveFile` 原子提交，SAF 直接写入失败时可能留下不完整文档。压缩包根目录直接包含 `level.dat` 和 `db`，跳过 `db/LOCK`、`db/LOG`、`db/LOG.old`，保留编号事务日志。导出前需关闭该世界和其中打开的文件；打包期间持有数据库锁，避免其他程序修改数据库，并禁止导入、删除和重新打开文件。拒绝源世界目录内的本地输出路径、符号链接和目录联接。当前不支持 ZIP64，压缩包小于 2 GiB、最多 65534 个条目；内存使用按单个文件压缩，不将整个世界或 ZIP 读入内存。

## 存档文件访问

存档扫描、`level.dat` 读取与保存、网易 XOR 检测、缩略图和 LevelDB 使用同一个文件访问入口。`src/base/worldenv.cpp` 中的 `platform::worldEnv(path)` 在后台选择通道：普通目录使用本地 Env；Android 受限目录依次尝试已经授权的 Shizuku 和 root，实际不可访问就跳过。应用内部目录始终使用本地通道，缺失时不尝试提权。配置、日志、资源、SAF 文档接口及应用沙盒导入暂存流程继续使用各自现有接口。

`WorldEnv` 基于 `leveldb::EnvWrapper`，额外提供扫描需要的文件类型、大小与修改时间查询，并记录当次打开中的写权限失败以触发复制提示。路径使用 UTF-8，本地实现负责 Windows 文件名转换。Java 不再单独扫描和编码世界元数据：C++ 使用 Env 枚举来源的直接子目录、解析 `level.dat`、读取名称与图标，并遍历世界内的文件统计大小和修改时间；统计跳过符号链接，避免循环。

每次打开世界在后台选择并保留自己的 Env，生命周期覆盖数据库及其文件。`bedrock-level` 只接收通用 `std::shared_ptr<leveldb::Env>`，不依赖 Qt 或 JNI。数据库调用链是 `LevelDB → McneWrapper → 本地/安卓 Env`，普通存档使用空 XOR 密钥；`level.dat` 等其他文件直接使用底层 Env。修改 XOR 密钥保留已经选择的文件通道。`level.dat` 保存通过同一个 Env 写临时文件、同步并原子重命名，替换失败保留临时文件。

Android 的 `WorldFileService` 提供目录查询、打开文件、创建、删除和重命名操作，Shizuku UserService 与 libsu `RootFileService` 共用同一份实现。文件描述符通过 Binder 交给应用，C++ 在本地执行 `read/pread/write/fsync`，避免将每次数据库读写变成 RPC。写入提供缓冲；数据库使用 Linux OFD 文件锁，与 LevelDB 的 POSIX 锁互斥，也拒绝同进程中的重复打开。root 新建文件和目录继承父目录的 UID/GID，避免变成仅 root 可访问的存档文件。

root 服务启动任务使用权限按钮已建立的 shell，不调用会自动获取新 shell 的绑定接口；扫描和打开世界均不申请权限。权限失效或服务断开后的新文件操作返回错误，不在写入过程中切换后端。Shizuku 的 shell 身份通常不能访问其他应用的私有目录，这些目录需要 root 或以 root 身份运行的 Shizuku。已经传递给应用的描述符遵循系统的文件描述符及 SELinux 访问规则。

原地打开因写权限不足失败时，会询问是否复制到软件内部。仅用户确认后才后台复制完整世界并打开副本；后续修改仅保存在副本，不回写原目录。复制使用原世界的 Env 读取，与 SAF 共用导入暂存和发布流程，跳过 `db/LOCK`、`db/LOG` 和 `db/LOG.old`，保留编号事务日志。失败清理暂存目录，已有完整内部副本直接复用，不覆盖之前的修改。复制前需先关闭 Minecraft，避免复制到正在变化的数据库。数据库占用、损坏等其他错误仍使用原有报错。

`base/platform.h/.cpp` 提供通用平台能力，包括文件 URL 判定、本地路径与 SAF 目录解析、权限、进程内存和崩溃处理，不负责世界结构校验。`src/loader/leveloperator.cpp` 中的 `LevelOperator` 复用这些能力，通过静态函数负责世界结构校验、可打开世界与已有副本的选择、副本目标路径、世界复制、暂存校验与落盘；`AppController` 负责打开世界或请求复制确认，以及后台任务调度、导入状态和通知。

Shizuku 绑定失败、超时或断开后等待 5 秒，允许下一次操作重新绑定，不自动循环重试。连接回调超时只说明服务尚未接通；启动崩溃可通过 `adb logcat -d -v threadtime -s ShizukuServiceStarter UserServiceManager UserServiceRecord AndroidRuntime` 定位。授权完成、权限页显示和应用回到前台都只更新权限显示，用户在世界列表手动刷新。

扫描与文件访问日志统一进入应用日志页和日志文件，可搜索 `WorldScan`、`BedrockMapFiles`、`Shizuku` 或 `Root`。重复文件访问细节使用调试级别；扫描汇总、权限和服务连接状态及异常保留信息或告警级别。不记录文件内容。Java 的通用日志入口 `AppLog` 通过 JNI 接入 `LogModel`。

日志页默认显示信息及以上级别，隐藏时间、线程、文件位置等日志前缀，保留业务标签和告警颜色；“显示前缀”开关可恢复完整格式。复制使用当前筛选和显示格式，完整日志文件导出不受这些显示选项影响。重复的通道请求、返回和逐世界读取细节使用调试级别，扫描汇总、权限与服务连接状态、告警仍使用信息及以上级别。移动端通过顶部按钮复制，不提供长按菜单；桌面端保留文本选择和右键菜单。

## 会话与页面

`AppController` 处理应用页面导航及打开命令，并通过 `SessionController` 管理多个 `WorkspaceSession`。世界、独立 NBT 文件和 `.mcstructure` 文件各自有会话类型。会话负责数据生命周期、dirty 状态和保存能力；QML 负责窗口、对话框和当前展示状态。

每个 `WorldSession` 拥有自己的 `AsyncLevelLoader`、`MapViewState`、`MapController`、`TerrainDataProvider`、全局 NBT 与区块编辑控制器。`Main.qml` 是应用壳，`SessionHost.qml` 保留每种会话的页面实例，`OpenWorldPage.qml` 展示世界加载态，`WorldWorkspace.qml` 组织地图、全局 NBT 与工具面板。

地图相机、维度、图层和选择由 `MapViewState`/`MapController` 管理。QML 组件只将输入交给控制器，不在渲染节点内维护另一份地图状态。

## 世界与渲染数据流

1. `WorldSession` 打开世界，`AsyncLevelLoader` 初始化 LevelDB，并按需建立区块坐标索引。
2. `TerrainQuickItem` 根据相机和窗口尺寸请求可见区域及 LOD。`TerrainDataProvider` 推动区域烘焙、tile 采样和交付。
3. `TerrainQuickItem` 在 Qt Quick Scene Graph/RHI 中维护地形纹理与 page table。`MapOverlayQuickItem` 绘制实体、网格、选择等地图叠加内容。
4. 区块或 NBT 修改通过会话控制器进入 loader；相关区域缓存失效后按需重新烘焙。

区域加载、tile 采样和体素网格构建不得阻塞 GUI 线程。Scene Graph 渲染线程只处理绘制资源，不直接访问 LevelDB。渲染细节见 `docs/qmlWorldRendering.md`；如果该文档与代码有差异，以当前代码为准。

## 编辑与配置

`GlobalDataController` 和 `NbtDocumentController` 提供全局 NBT 与独立文件编辑；`ChunkEditorController` 提供区块数据与体素预览，`McstructureController` 处理结构文件。`RegionImportController` 负责地图区域导入。关联的世界修改由 `WorldSession` 统一提交。

运行配置由 `src/base/config.cpp` 通过 `QSettings` 读写，QML 使用 `SettingsViewModel`。用户可见字符串使用 Qt 翻译机制，源文件是 `translations/zh_CN.ts` 与 `translations/en.ts`；`scripts/build.ps1` 会运行 `lupdate`。

Android 的“工具 → 权限管理”页面显示所有文件访问、Shizuku 和 root 的状态，只有点击申请按钮才启动相应授权流程，启动和扫描时均不申请权限。Shizuku 使用官方 API/provider 13.1.5。root 使用 libsu core/service 6.0.0，在后台通过 `su` 建立并确认 UID 0 的 shell，建立连接最多等待 60 秒；连接保留供扫描复用，失效后需要再次点击按钮。权限变化或从系统页面返回时只更新权限显示，世界列表由用户手动刷新。
