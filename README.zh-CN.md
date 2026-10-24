<p align="center">
  <img src="./imgs/sample.png" alt="BedrockMap 截图" width="720">
</p>

# BedrockMap

<p align="center">
  <img src="https://img.shields.io/github/license/bedrock-dev/BedrockMap" alt="License">
  <img src="https://img.shields.io/github/v/release/bedrock-dev/BedrockMap" alt="Release">
</p>

[English](./README.md) | **简体中文**

**BedrockMap** 是一款基于 Qt 6、Qt Quick 和 C++20 构建的开源《我的世界》基岩版地图编辑器，可浏览、检查和编辑基岩版世界存档（LevelDB 格式）。

## 功能特性

- **世界浏览** — 打开并探索主世界、下界和末地，以及自定义维度的基岩版世界
- **地形可视化** — 生物群系地图和地形叠加层，支持可配置的渲染过滤
- **区块编辑器** — 选择、检查和删除区块；可视化区块实体、方块实体和计划刻（pending ticks）
- **区块复制粘贴** — 复制选中区域并粘贴到其他世界，甚至跨存档，支持交互式放置
- **NBT 编辑器** — 查看和修改 NBT 数据，包括 `level.dat`、玩家物品栏、村庄、地图物品等
- **mcstructure 支持** — 浏览和编辑 `.mcstructure` 文件（带 3D 体素预览），并可将选中区域或区块导出为 `.mcstructure` 文件
- **体素渲染** — 选中区域的 3D 体素可视化，可导出为 GLB 模型
- **多会话工作区** — 同时打开和管理多个世界

## 下载

从 [Release 页面](https://github.com/bedrock-dev/BedrockMap/releases) 下载最新版本。

## 系统要求

- **操作系统** — Windows 10 及更新版本；也支持构建 Android 版本

## 快速上手

在世界列表中打开存档，也可以将世界文件夹或 `.mcstructure` 文件拖入应用。地图支持平移、缩放、选择和右键菜单。

## 构建

```powershell
# 使用子模块克隆
git clone --recursive https://github.com/bedrock-dev/BedrockMap.git

# 构建 Qt Quick 应用
.\scripts\build.ps1

# 运行
.\scripts\run.ps1
```

## 支持的 MCBE 版本

支持 Minecraft 基岩版 1.2（可能更早）及以后的世界，直到 1.21 系列（不同版本的区块格式可能不同）。

## 参与贡献

欢迎通过 [Issues](https://github.com/bedrock-dev/BedrockMap/issues) 提交 Bug 报告和功能建议。规划中的功能和已知问题见 [TODO.md](./TODO.md)。

## 许可证

BedrockMap 基于 [GNU Affero General Public License v3.0](./LICENSE) 许可证发布。

## Credits

- [bedrock level](https://github.com/bedrock-dev/bedrock-level) 用于软件后端
- [Minecraft Wiki](https://minecraft.wiki/) 提供生物、NBT以及方块实体等图标
