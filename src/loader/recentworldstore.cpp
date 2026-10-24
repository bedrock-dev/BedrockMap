#include "loader/recentworldstore.h"

#include <QDir>
#include <QSettings>
#include <algorithm>

#include "base/config.h"
#include "loguru/loguru.hpp"
#include "model/worldpath.h"

RecentWorldStore::RecentWorldStore() : file_path_(app_paths::recentFile()) { load(); }

void RecentWorldStore::add(const QString& path) {
    const QString target = world_path::normalize(path);
    if (target.isEmpty()) return;
    const QString key = world_path::identityKey(target);
    paths_.removeIf([&key](const QString& entry) { return world_path::identityKey(entry) == key; });
    paths_.prepend(target);
    while (paths_.size() > MaxHistory) paths_.removeLast();
}

int RecentWorldStore::pruneMissing() {
    const auto before = paths_.size();
    paths_.removeIf([](const QString& path) { return !QDir(path).exists(); });
    if (paths_.size() != before) save();
    return before - paths_.size();
}

void RecentWorldStore::load() {
    paths_.clear();
    QSettings settings(file_path_, QSettings::IniFormat);
    const int size = settings.beginReadArray(QStringLiteral("RecentWorlds"));
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        const QString path = world_path::normalize(settings.value(QStringLiteral("path")).toString());
        if (path.isEmpty()) continue;
        const QString key = world_path::identityKey(path);
        const bool duplicate =
            std::any_of(paths_.cbegin(), paths_.cend(), [&key](const QString& entry) { return world_path::identityKey(entry) == key; });
        if (!duplicate && paths_.size() < MaxHistory) paths_.append(path);
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
