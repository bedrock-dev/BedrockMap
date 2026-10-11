#pragma once

#include <QStringList>

class RecentWorldStore final {
   public:
    static constexpr int MaxHistory = 10;
    RecentWorldStore();
    ~RecentWorldStore();
    void add(const QString& path);
    [[nodiscard]] QStringList paths() const { return paths_; }
    void load();
    void save() const;

   private:
    QString file_path_;
    QStringList paths_;
};
