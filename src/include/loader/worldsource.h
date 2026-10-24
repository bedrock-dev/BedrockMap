#ifndef BEDROCKMAP_WORLDSOURCE_H
#define BEDROCKMAP_WORLDSOURCE_H

#include <QString>
#include <QStringList>
#include <vector>

#include "model/worldmetadata.h"

enum class WorldSourceAccess {
    Direct,
    CopyIn,
    Privileged,
};

enum class WorldSourceAvailability {
    Available,
    NeedsAllFiles,
    NeedsPrivilege,
    Unavailable,
};

/// One ordered world location presented to the scanner and source settings.
struct WorldSource {
    QString id;
    QString label;
    WorldSourceKind kind{WorldSourceKind::MinecraftDefault};
    WorldSourceAccess access{WorldSourceAccess::Direct};
    WorldSourceAvailability availability{WorldSourceAvailability::Available};
    QStringList roots;
};

/// Builds the ordered source list for the current platform. Custom folders are
/// passed in from the UI thread so the worker never reads mutable settings.
[[nodiscard]] std::vector<WorldSource> makeWorldSources(const QStringList& customPaths, bool scanLeviPath = true,
                                                        bool allFilesAccess = true);
[[nodiscard]] std::vector<WorldMetadata> scanWorldSource(const WorldSource& source);

#endif  // BEDROCKMAP_WORLDSOURCE_H
