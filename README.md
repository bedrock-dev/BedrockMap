<p align="center">
  <img src="./imgs/sample2.png" alt="BedrockMap Screenshot" width="720">
</p>

# BedrockMap

**English** | [简体中文](./README.zh-CN.md)

**BedrockMap** is a fully open-source map editor for Minecraft Bedrock Edition, built with Qt6 and C++17. It provides a comprehensive toolset for browsing, inspecting, and editing Bedrock world saves (LevelDB format) across all dimensions.

## Features

- **World Browsing** — Open and explore Bedrock worlds across the Overworld, Nether, and The End, plus custom dimensions
- **Terrain Visualization** — Biome maps and terrain overlays with configurable rendering filters
- **Chunk Editor** — Select, inspect, and delete chunks; visualize chunk entities, block entities and pending ticks
- **Chunk Copy & Paste** — Copy selected regions and paste them into other worlds, even across different saves, with interactive placement
- **NBT Editor** — View and modify NBT data including `level.dat`, player inventories, villages, map items, and more
- **mcstructure Support** — Browse and edit `.mcstructure` files with a 3D voxel preview, and export selected regions or chunks as `.mcstructure` files
- **Voxel Rendering** — 3D voxel visualization of selected regions, exportable as GLB models
- **Multi-Tab Workspace** — Open and manage multiple worlds simultaneously

### Build

```powershell
# Clone with submodules
git clone --recursive https://github.com/bedrock-dev/BedrockMap.git

# Build BedrockMap
.\scripts\build.ps1 -buildBL
```

## License

BedrockMap is licensed under the [GNU Affero General Public License v3.0](./LICENSE).
