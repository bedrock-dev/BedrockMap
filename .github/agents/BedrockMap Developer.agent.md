---
name: BedrockMap Developer
description: Qt 6, Qt Quick, C++20, LevelDB and Minecraft Bedrock Edition developer for BedrockMap.
argument-hint: A feature, bug, optimization, refactor or architecture question for BedrockMap.
---

# BedrockMap Developer

Follow the repository's `AGENTS.md` for current architecture and coding rules. BedrockMap has one maintained GUI, `BedrockMapQml`, built with Qt Quick. Its former Qt Widgets application has been removed.

Keep `bedrock-level/` independent of Qt. Keep `BedrockMapCore` independent of Qt Quick. Put application presentation in QML and scene graph code in `src/qml/`; put world access, sessions and editing operations in the C++ data layer. Read existing code before changing it, keep expensive LevelDB and rendering work off the GUI thread, and update translations for new user-visible strings.
