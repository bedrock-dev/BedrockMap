<p align="center">
  <img src="./imgs/sample_pc.png" alt="BedrockMap desktop map and NBT editor" width="70%">&emsp;<img src="./imgs/sample_mobile.png" alt="BedrockMap mobile 3D voxel viewer" width="24%">
</p>

# BedrockMap

<p align="center">
  <img src="https://img.shields.io/github/license/bedrock-dev/BedrockMap" alt="License">
  <img src="https://img.shields.io/github/v/release/bedrock-dev/BedrockMap" alt="Release">
</p>

**English** | [简体中文](./README.zh-CN.md)

**BedrockMap** is an open-source, **high-performance, cross-platform map editor for Minecraft Bedrock Edition**, spanning desktop, mobile, and the web. Built with **C++20**, Qt 6, and Qt Quick, it provides tools for browsing, inspecting, and editing Bedrock world saves (LevelDB format) across dimensions.

Supports **all Minecraft Bedrock Edition versions from 1.2 onward**.

> **[Try the web demo](https://bedrock-dev.github.io/bm/web/)** — No installation required. The initial download is large, so loading may take a while. Please be patient.

## Features

- **World Browsing** — Open and explore Bedrock worlds across the Overworld, Nether, and The End, plus custom dimensions
- **Terrain Visualization** — Biome maps and terrain overlays with configurable rendering filters
- **Chunk Editor** — Select, inspect, and delete chunks; visualize chunk entities, block entities and pending ticks
- **Chunk Copy & Paste** — Copy selected regions and paste them into other worlds, even across different saves, with interactive placement
- **NBT Editor** — View and modify NBT data including `level.dat`, player inventories, villages, map items, and more
- **mcstructure Support** — Browse and edit `.mcstructure` files with a 3D voxel preview, and export selected regions or chunks as `.mcstructure` files
- **Voxel Rendering** — 3D voxel visualization of selected regions, exportable as GLB models
- **Multiple Sessions** — Open and manage multiple worlds simultaneously

## Download

Grab the latest release from the [Releases page](https://github.com/bedrock-dev/BedrockMap/releases).

## Platforms

| Platform | Availability |
| --- | --- |
| Desktop | Windows 10 or newer |
| Mobile | Android 9 or newer (arm64-v8a) |
| Web | Experimental WebAssembly demo in Chrome, Edge, or another Chromium browser with File System Access API support |

## Quick Start

Open a world from the world list, or drag and drop a world folder or `.mcstructure` file onto the application. The map supports panning, zooming, selection, and a context menu.

## Build

Building requires Qt 6 and a compiler with C++20 support. The commands below build and run the Windows desktop app.

```powershell
# Clone with submodules
git clone --recursive https://github.com/bedrock-dev/BedrockMap.git

# Build the Qt Quick application
.\scripts\build.ps1

# Run it
.\scripts\run.ps1
```

## Contributing

Bug reports and feature requests are welcome via [Issues](https://github.com/bedrock-dev/BedrockMap/issues).

## License

BedrockMap is licensed under the [GNU Affero General Public License v3.0](./LICENSE).

## Credits

- [bedrock level](https://github.com/bedrock-dev/bedrock-level) for the software backend
- [Minecraft Wiki](https://minecraft.wiki/) for mob, NBT, and block entity icons
