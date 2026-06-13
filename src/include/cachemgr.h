#ifndef CACHEMGR_H
#define CACHEMGR_H

#include <QString>
#include <QStringList>
#include <string>

/// Manages persistent cache data (recent history, etc.) stored as JSON.
/// File is stored alongside the config file, without a .json suffix.
class CacheManager {
   public:
    static constexpr int MAX_HISTORY = 10;

    CacheManager();
    ~CacheManager();

    /// Add a path to the recent history (dedup, trim to MAX_HISTORY).
    void addRecentPath(const QString &path);

    /// Get the list of recent paths, most recent first.
    QStringList recentPaths() const;

    /// Persist to disk immediately.
    void save();

   private:
    void load();

    std::string filePath_;
    QStringList history_;
};

#endif  // CACHEMGR_H
