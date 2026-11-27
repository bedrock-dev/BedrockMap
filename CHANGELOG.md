# BedrockMap 1.6.0 更新日志

发布日期：2026-11-27

## 新增功能

- 优化整体界面与导航效果，改善控件间距、圆角以及小窗口和移动端布局，并支持调节界面边框宽度。
- 大幅重做 3D 视图的结构导入和导出流程，支持实体、空气、结构空位、新版格式、虚空区域结构和区块网格参考平面。
- 世界导出新增“导出到文件夹”，可直接生成 `level.dat` 和 `db/` 等内容，无需再经过压缩包步骤。
- 导入世界时自动检测文件实际格式，避免文件被错误解析成NBT。
- 新增地图初始位置设置，可在打开世界时定位到原点、世界出生点或玩家位置。
- NBT 记录表新增正则搜索和多选批量操作
- 关闭未保存的会话时新增保存、放弃或取消确认流程。
- 新增自定义背景图片和透明度调节功能。
- 新增在线教程（工具->教程，更新中可能不完善）
- 兼容 LeviLauncher 新版实例目录结构
- 补充和修复部分缺失的文本和翻译

## Bug 修复

- 修复把mcstructure导入到世界内可能发生的存档格式不兼容进而造成闪退的问题。
- 修复mcstructure导入到全局数据的名称异常问题。
- 修复空文件夹被误判为非空、空存档导出和文件夹导出状态判断错误的问题。
- 修复若干界面布局、图标和交互状态异常。

---

# BedrockMap 1.6.0 Changelog

Release date: 2026-11-27

## New Features

- Improved the overall interface and navigation, including control spacing, corner radii, small-window and mobile layouts, and a configurable interface border width.
- Overhauled 3D-view structure import and export, with support for entities, air, structure voids, the new format, structures in empty areas, and a chunk-grid reference plane.
- Added "Export to Folder" for worlds, which writes `level.dat`, `db/`, and other contents directly without requiring an intermediate archive.
- Added automatic detection of the actual file format when importing worlds, preventing files from being incorrectly parsed as NBT.
- Added an initial map position setting that can open a world at the origin, world spawn, or player position.
- Added regular-expression search and multi-select batch operations to NBT record tables.
- Added a save, discard, or cancel confirmation flow when closing sessions with unsaved changes.
- Added custom background images and opacity adjustment.
- Added online tutorials under Tools → Tutorial; they are still being updated and may be incomplete.
- Added compatibility with the new LeviLauncher instance directory layout.
- Added and fixed some missing text and translations.

## Bug Fixes

- Fixed crashes caused by save-format incompatibilities when importing an mcstructure into a world.
- Fixed incorrect names when importing mcstructures into global data.
- Fixed empty folders being incorrectly detected as non-empty, as well as incorrect status handling for empty-world and folder exports.
- Fixed several interface layout, icon, and interaction-state issues.

---

# BedrockMap 1.5.0-beta5 更新日志

发布日期：2026-11-15

## 新增功能

- 区块编辑器新增 HSA（硬编码出生区域）编辑，并提供 HSA 显示开关。
- 大地图新增可配置准心，开启后使用准心位置显示实时坐标；Android 默认开启，桌面端默认关闭。
- Android 大地图坐标输入新增跳转按钮，避免不同软键盘确认键行为不一致。
- NBT 编辑器支持节点复制和粘贴。
- 3D 视图不再受 256 格范围限制，并重新整理结构、选区和体素预览区域。
- 新增地图图层面板和紧凑窗口工作区导航，改善移动端和小窗口操作。
- 地图状态栏、维度边界和地形轮廓改为可配置，并加入对比度感知的地形轮廓渲染。

## Bug 修复

- 修复 NBT 编辑器复制、粘贴及部分节点操作异常。
- 修复 3D 预览、区块编辑器和选区切换中的若干体素显示与状态同步问题。
- 修复阴影、环境光遮蔽和地形轮廓的部分渲染问题。
- 修复坐标输入框回车无法按预期跳转的问题。
- 修复区块编辑器保存后地形、HSA 和实体 overlay 缓存未及时刷新的问题。
- 世界保存失败时现在会显示具体原因，包括 LevelDB 状态、`level.dat` 路径和文件 I/O 错误。
- 修复 Web/WASM 版本包体过大的问题。

---

# BedrockMap 1.5.0-beta5 Changelog

Release date: 2026-11-15

## New Features

- Added HSA (hardcoded spawn area) editing to the chunk editor, with a visibility toggle for HSA overlays.
- Added a configurable map crosshair that also drives the live center coordinate display. It is enabled by default on Android and disabled by default on desktop.
- Added an Android coordinate jump button to avoid inconsistent soft-keyboard confirmation behavior.
- Added copy and paste support to the NBT editor.
- Removed the 256-block limit from the 3D view and reorganized its structure, selection, and voxel preview sections.
- Added a map layer panel and compact-workspace navigation for mobile and small-window layouts.
- Made the map status bar, dimension border, and terrain outline configurable, and added contrast-aware terrain outlines.

## Bug Fixes

- Fixed NBT editor copy, paste, and several node operation issues.
- Fixed several voxel rendering and state synchronization issues in the 3D preview, chunk editor, and selection workflow.
- Fixed rendering issues affecting shadows, ambient occlusion, and terrain outlines.
- Fixed coordinate input fields not jumping as expected when Return is pressed.
- Fixed terrain, HSA, and entity overlay caches not refreshing after the chunk editor saved data.
- World save failures now report specific causes, including LevelDB status, the `level.dat` path, and file I/O errors.
- Fixed the oversized Web/WASM distribution.

---

# BedrockMap 1.5.0-beta4 更新日志

发布日期：2026-11-10

## 新增功能

- 大地图支持自定义维度，并可通过 `Alt+4` 快捷打开。
- Android 支持导出世界后直接调用其他应用打开。
- 世界导入和导出增加进度浮层。
- Windows 支持通过文件关联或命令行直接打开 NBT、mcstructure 和 mcworld。
- 日志页面新增复制功能，并优化导出时的默认文件名。
- 新增纯黑主题，恢复世界卡片/列表切换。
- 当前打开的区块新增橙色轮廓高亮。
- 导入后的世界目录改为“世界名称 + 短哈希”，更容易辨认。

## Bug 修复

- 修复包含空目录的 mcworld 被误判为非法存档的问题。
- 修复 Shizuku 授权成功后世界列表不自动刷新的问题。
- 修复 Android 外部存储和特权文件访问不稳定的问题。
- 修复地图过滤、视图和下拉面板无法正确展开、关闭或切换的问题。
- 修复移动端操作地图后输入框未失焦、软键盘不收起的问题。
- 修复大地图状态区背景透明、超出窗口或完全不可见的问题。
- 修复颜色选择器打开独立原生窗口、样式与应用不一致的问题。
- 修复部分标题栏、侧边栏和世界列表控件的对齐与显示异常。

---

# BedrockMap 1.5.0-beta4 Changelog

Release date: 2026-11-10

## New Features

- Added custom dimension switching to the map, with `Alt+4` as a shortcut.
- Added an Android "Export and Open" workflow for worlds.
- Added progress overlays for world imports and exports.
- Windows can now open NBT, mcstructure, and mcworld files through file associations or command-line paths.
- Added log copying and improved the default log export filename.
- Added a pure black theme and restored the world card/list switcher.
- Added an orange outline around the currently opened chunk.
- Imported world folders now use a readable world name plus a short hash.

## Bug Fixes

- Fixed mcworld files containing empty directories being incorrectly rejected during import.
- Fixed the Android world list not refreshing after Shizuku authorization.
- Improved the reliability of Android external storage and privileged file access.
- Fixed map filter, view, and dropdown panels not opening, closing, or toggling correctly.
- Fixed mobile map interaction leaving text fields focused and the keyboard visible.
- Fixed the bottom-right map status area being transparent, clipped, or invisible.
- Fixed color pickers opening as native standalone windows with inconsistent styling.
- Fixed title bar, sidebar, and world-list alignment and display issues.
