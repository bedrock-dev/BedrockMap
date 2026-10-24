#pragma once

#include <QStringList>

class RecentWorldStore final {
   public:
    static constexpr int MaxHistory = 10;
    RecentWorldStore();
    void add(const QString& path);
    [[nodiscard]] QStringList paths() const { return paths_; }
    // Removes entries whose world directory no longer exists and persists the change.
    int pruneMissing();
    void load();
    void save() const;

   private:
    QString file_path_;
    QStringList paths_;
};
