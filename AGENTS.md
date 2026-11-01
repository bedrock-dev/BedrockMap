---
name: BedrockMap Developer
description: Qt 6, Qt Quick, C++20, LevelDB and Minecraft Bedrock Edition development for BedrockMap.
argument-hint: A feature, bug, performance task, refactor or architecture question for BedrockMap.
---

# BedrockMap Developer

BedrockMap is a Qt Quick application. The former Qt Widgets frontend has been removed and is no longer a maintenance target. Keep the data layer free of Qt Quick and QWidget dependencies.

## Architecture

- `bedrock-level/` is the standalone C++ data library for LevelDB, NBT, chunks, actors and palettes. It has no Qt dependency.
- `src/base/`, `src/loader/`, `src/model/`, `src/controller/` and `src/render/` build into `BedrockMapCore`. They contain configuration, world access, sessions, editing operations and CPU data preparation.
- `src/qml/` contains the Qt Quick entry point, QML pages, controls, scene graph items and shaders. `BedrockMapQml` is the only GUI target.
- `AppController` manages application navigation and open sessions through `SessionController`. Each `WorldSession` owns its loader, `MapViewState`, `MapController`, terrain provider and world editing controllers.
- `TerrainQuickItem` and `MapOverlayQuickItem` render data supplied by `TerrainDataProvider` and the world controllers. Keep camera, selection and map operations in `MapViewState`/`MapController`, not in a rendering item.
- `WorkspaceSession` provides the common lifecycle and dirty state for world, NBT and mcstructure sessions.

## Coding standards

- Use C++20 and RAII. Prefer `std::unique_ptr` for ownership, non-owning raw pointers only for references, and `const` and `[[nodiscard]]` where useful.
- Keep all code comments in English. When editing a file, translate any Chinese comments in the touched code to English.
- Validate LevelDB reads and handle absent keys. Batch related writes with `leveldb::WriteBatch`.
- Keep chunk loading, region baking and expensive exports off the GUI thread. Do not block Qt Quick input or scene graph threads on LevelDB.
- Only load visible terrain regions and respect the configured thread and cache limits.
- Use `tr()`, `qsTr()` or `QCoreApplication::translate()` for user-visible text, and update `translations/zh_CN.ts` and `translations/en.ts` with `lupdate`.
- Prefer editing an existing file over introducing a wrapper with no clear responsibility.
- Do not add or modify tests unless the user explicitly requests tests.
- For changes exceeding 20 lines, compile with `./scripts/build.ps1` before handing off. Do not launch the application for manual testing unless the user explicitly requests it.

## Working in this repository

- Read existing code patterns and `TODO.md` before implementing a feature.
- Refer to `docs/BedrockMap.md` for the current architecture and `docs/qmlWorldRendering.md` for terrain data flow. Treat proposal documents as proposals, not as implemented behavior.
- Build with `./scripts/build.ps1`; run with `./scripts/run.ps1`. `./scripts/build_run.ps1` does both.
- For chunk format changes, test representative MCBE worlds from 1.18 through 1.21 when they are available.
- Preserve the `bedrock-level` submodule as an independent library and do not add Qt dependencies to it.
