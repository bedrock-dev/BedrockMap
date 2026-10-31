#ifndef BEDROCKMAP_LEVELOPERATOR_H
#define BEDROCKMAP_LEVELOPERATOR_H

#include <QString>
#include <QUrl>

namespace bl {
    class level_dat;
}
namespace platform {
    class WorldFileSource;
}

/// File operations for complete worlds, including import into internal storage.
class LevelOperator {
   public:
    LevelOperator() = delete;

    /// Resolves the stable internal destination for a normalized path or content URI.
    /// Uses 16 SHA-256 hex characters for new copies; reuses existing full-hash paths.
    [[nodiscard]] static QString targetForSource(const QString& source);

    /// True only for a real direct child directory of the app's worlds folder.
    [[nodiscard]] static bool isInternalWorld(const QString& path);

    /// Permanently removes an internal world on a worker; returns an error or
    /// an empty string on success. The caller must first ensure it is closed.
    [[nodiscard]] static QString deleteInternalWorld(const QString& path);

    /// Resolves a content URI to an accessible world or an existing internal copy.
    /// Returns empty when the user must confirm importing a copy first.
    [[nodiscard]] static QString resolveContentWorldFolder(const QUrl& source);

    /// Checks for a readable, parseable level.dat and the db directory using local file access.
    /// Does not open LevelDB or traverse the world's files.
    [[nodiscard]] static bool isCompleteWorld(const QString& path);

    /// Validates a world directory and parses level.dat through the selected source.
    [[nodiscard]] static bool loadLevelDat(platform::WorldFileSource& files, const QString& path, bl::level_dat& dat);

    /// Copies to a temporary directory, validates it, then publishes to target.
    /// Call on a worker with a target from targetForSource(); existing targets
    /// are never replaced. Returns an empty string on success or an error.
    [[nodiscard]] static QString copyToInternal(const QUrl& source, const QString& target);

    /// Extracts a .mcworld ZIP to staging and publishes it on success. Existing
    /// complete targets are never replaced; staging is discarded on failure.
    [[nodiscard]] static QString extractToInternal(const QUrl& source, const QString& target);

    /// Packages a closed world as a .mcworld ZIP on a worker. Destination may
    /// be a local path or Android content URI; returns an error or empty on success.
    [[nodiscard]] static QString exportMcworld(const QString& source, const QUrl& destination);
};

#endif  // BEDROCKMAP_LEVELOPERATOR_H
