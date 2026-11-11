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
