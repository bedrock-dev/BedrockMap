#ifndef BEDROCKMAP_LEVELPATHMANAGER_H
#define BEDROCKMAP_LEVELPATHMANAGER_H

#include <QString>
#include <QStringList>
#include <cstdint>
#include <string>
#include <vector>

struct LevelPathInfo {
    std::string path;
    std::string levelName;
    std::string version;
    int64_t lastModified{0};
    int64_t sizeBytes{0};
    bool isValid{false};
    bool modern{false};
    bool preview{false};
};

struct ScanPathEntry {
    std::string path;
    std::string version;
    std::string tag;
    bool modern{false};
    bool normal{false};
};

class LevelPathManager {
   public:
    static constexpr int MAX_HISTORY = 10;

    LevelPathManager();
    ~LevelPathManager();

    void init();
    void initLeviPath();
    void scanNormalPaths();
    void scanModernPaths();
    void dumpPaths();

    // recent history (was CacheManager)
    void addRecentPath(const QString &path);
    QStringList recentPaths() const;
    void saveHistory();
    void loadHistory();

    // scan results
    const std::vector<LevelPathInfo> &discoveredLevels() const { return discovered_levels_; }

    // build LevelPathInfo from a world directory path
    static LevelPathInfo makeLevelInfo(const QString &dirPath);

    static const std::string GAMES_REL_PATH;
    static const std::string PACKAGE_UWP;
    static const std::string PACKAGE_WINDOWS_BETA;
    static const std::string DIR_MINECRAFT_BEDROCK;
    static const std::string DIR_MINECRAFT_BEDROCK_PREVIEW;

   private:
    std::vector<ScanPathEntry> scan_paths_;
    std::vector<LevelPathInfo> discovered_levels_;
    std::string filePath_;
    QStringList history_;
};

#endif  // BEDROCKMAP_LEVELPATHMANAGER_H
