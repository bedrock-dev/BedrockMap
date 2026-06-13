<p align="center">
  <img src="./imgs/sample2.png" alt="BedrockMap Screenshot" width="720">
</p>

# BedrockMap

**BedrockMap** is a fully open-source map editor for Minecraft Bedrock Edition, built with Qt6 and C++17. It provides a comprehensive toolset for browsing, inspecting, and editing Bedrock world saves (LevelDB format) across all dimensions.

## Features

- **World Browsing** — Open and explore Bedrock worlds across the Overworld, Nether, and The End
- **Terrain Visualization** — Biome maps and terrain overlays with configurable rendering filters
- **Chunk Editor** — Select, inspect, and delete chunks; visualize chunk entities, block entities and pending ticks
- **NBT Editor** — View and modify NBT data including `level.dat`, player inventories, villages, map items, and more
- **Voxel Rendering** — 3D voxel visualization of selected regions
- **Multi-Tab Workspace** — Open and manage multiple worlds simultaneously

### Build

```powershell
# Clone with submodules
git clone --recursive https://github.com/bedrock-dev/BedrockMap.git

# Build the bedrock-level static library
cd bedrock-level
.\build.ps1

# Build the main application
cd ..
.\scripts\build.ps1

# Build and run in one step
.\scripts\build_run.ps1
```

For detailed instructions, see the [Compile Guide](./docs/complie_guide.md).

## License

BedrockMap is licensed under the [GNU Affero General Public License v3.0](./LICENSE).
