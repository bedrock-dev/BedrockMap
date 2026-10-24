#ifndef BEDROCKMAP_LEVELPATHMANAGER_H
#define BEDROCKMAP_LEVELPATHMANAGER_H

#include <QString>
#include <QStringList>
#include <cstdint>
#include <string>
#include <vector>

#include "model/worldmetadata.h"

struct ScanPathEntry {
    std::string path;
    std::string version;
    std::string tag;
    bool modern{false};
    bool normal{false};
};

class LevelPathManager {
   public:
    LevelPathManager();

    void init();
    void initLeviPath();
    void scanNormalPaths();
    void scanModernPaths();
    void dumpPaths();

    // scan results
    const std::vector<LevelPathInfo>& discoveredLevels() const { return discovered_levels_; }

    // build LevelPathInfo from a world directory path
    static LevelPathInfo makeLevelInfo(const QString& dirPath);

    static const std::string GAMES_REL_PATH;
    static const std::string PACKAGE_UWP;
    static const std::string PACKAGE_WINDOWS_BETA;
    static const std::string DIR_MINECRAFT_BEDROCK;
    static const std::string DIR_MINECRAFT_BEDROCK_PREVIEW;

   private:
    std::vector<ScanPathEntry> scan_paths_;
    std::vector<LevelPathInfo> discovered_levels_;
};

#endif  // BEDROCKMAP_LEVELPATHMANAGER_H
