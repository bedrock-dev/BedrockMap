#include "recentworldstore.h"

#include <QSettings>

#include "apppaths.h"
#include "loguru/loguru.hpp"

RecentWorldStore::RecentWorldStore() : file_path_(app_paths::recentFile()) { load(); }
RecentWorldStore::~RecentWorldStore() { save(); }

void RecentWorldStore::add(const QString& path) {
    paths_.removeAll(path);
    paths_.prepend(path);
    while (paths_.size() > MaxHistory) paths_.removeLast();
}

void RecentWorldStore::load() {
    paths_.clear();
    QSettings settings(file_path_, QSettings::IniFormat);
    const int size = settings.beginReadArray(QStringLiteral("RecentWorlds"));
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        const auto path = settings.value(QStringLiteral("path")).toString();
        if (!path.isEmpty()) paths_.append(path);
    }
    settings.endArray();
}

void RecentWorldStore::save() const {
    QSettings settings(file_path_, QSettings::IniFormat);
    settings.beginWriteArray(QStringLiteral("RecentWorlds"));
    for (int i = 0; i < paths_.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("path"), paths_.at(i));
    }
    settings.endArray();
    settings.sync();
    if (settings.status() != QSettings::NoError)
        LOG_F(WARNING, "RecentWorldStore: cannot write %s", file_path_.toStdString().c_str());
}
