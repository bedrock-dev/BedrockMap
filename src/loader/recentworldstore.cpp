#include "loader/recentworldstore.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include "base/apppaths.h"
#include "base/platform.h"
#include "loguru/loguru.hpp"

RecentWorldStore::RecentWorldStore() : file_path_(app_paths::recentFile()) { load(); }

void RecentWorldStore::add(const QString& path) {
    const QString target = platform::normalizeLocalPath(path);
    if (target.isEmpty()) return;
    paths_.removeAll(target);
    paths_.prepend(target);
    while (paths_.size() > MaxHistory) paths_.removeLast();
}

int RecentWorldStore::pruneMissing() {
    const auto before = paths_.size();
    paths_.removeIf([](const QString& path) {
        // An inaccessible parent is not evidence that the world was deleted.
        return !QDir(path).exists() && QDir(QFileInfo(path).absolutePath()).isReadable();
    });
    if (paths_.size() != before) save();
    return before - paths_.size();
}

void RecentWorldStore::load() {
    paths_.clear();
    QSettings settings(file_path_, QSettings::IniFormat);
    const int size = settings.beginReadArray(QStringLiteral("RecentWorlds"));
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        const QString path = platform::normalizeLocalPath(settings.value(QStringLiteral("path")).toString());
        if (path.isEmpty()) continue;
        if (!paths_.contains(path) && paths_.size() < MaxHistory) paths_.append(path);
    }
    settings.endArray();
}

void RecentWorldStore::save() const {
    QSettings settings(file_path_, QSettings::IniFormat);
    settings.remove(QStringLiteral("RecentWorlds"));
    settings.beginWriteArray(QStringLiteral("RecentWorlds"));
    for (int i = 0; i < paths_.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("path"), paths_.at(i));
    }
    settings.endArray();
    settings.sync();
    if (settings.status() != QSettings::NoError) LOG_F(WARNING, "RecentWorldStore: cannot write %s", file_path_.toStdString().c_str());
}
