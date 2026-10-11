#pragma once

#include "levelpathmanager.h"

class WorldRepository final {
   public:
    [[nodiscard]] static LevelPathInfo readWorld(const QString& path);
    [[nodiscard]] static std::vector<LevelPathInfo> scanNormal();
    [[nodiscard]] static std::vector<LevelPathInfo> scanModern(const std::vector<ScanPathEntry>& paths);
};
