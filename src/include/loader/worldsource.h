#ifndef BEDROCKMAP_WORLDSOURCE_H
#define BEDROCKMAP_WORLDSOURCE_H

#include <QString>
#include <QStringList>
#include <cstdint>
#include <string>
#include <vector>

enum class WorldSourceKind {
    Release,
    Preview,
    NetEase,
    Levi,
    AndroidLegacy,
    AndroidReleaseInternal,
    AndroidReleaseExternal,
    AppInternal,
    External,
};

// UI-independent metadata describing a Bedrock world on disk.
struct WorldMetadata {
    std::string path;
    std::string levelName;
    std::string version;
    // Loaded on the scan worker so QML never opens world files directly.
    std::string thumbnailDataUrl;
    std::int64_t lastModified{0};
    std::int64_t sizeBytes{0};
    bool isValid{false};
    bool xorEncrypted{false};
    WorldSourceKind sourceKind{WorldSourceKind::External};
};

/// A parent directory containing world folders, also displayed in source settings.
struct WorldSource {
    QString path;
    WorldSourceKind kind{WorldSourceKind::External};
};

/// Builds the ordered source list for the current platform. Custom folders are
/// passed in from the UI thread so the worker never reads mutable settings.
[[nodiscard]] std::vector<WorldSource> makeWorldSources(const QStringList& customPaths, bool scanLeviPath = true);
/// Reads valid worlds from the source directory's direct child folders.
[[nodiscard]] std::vector<WorldMetadata> scanWorldSource(const WorldSource& source);
/// Validates the world files and reads metadata; isValid is false on failure.
[[nodiscard]] WorldMetadata readWorldMetadata(const QString& path);
/// Stable translation source text; QML translates it when displaying a label.
[[nodiscard]] QString worldSourceLabel(WorldSourceKind kind);

#endif  // BEDROCKMAP_WORLDSOURCE_H
