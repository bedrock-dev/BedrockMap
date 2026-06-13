#include "cachemgr.h"

#include <qdir.h>
#include <qfile.h>
#include <qlogging.h>

#include <QDir>
#include <fstream>

#include "config.h"
#include "json/json.hpp"
#include "loguru/loguru.hpp"

using json = nlohmann::json;

CacheManager::CacheManager() {
    // Place cache file alongside the config file
    QFileInfo configInfo(cfg::CONFIG_FILE_PATH.c_str());
    filePath_ = configInfo.absoluteDir().absoluteFilePath("cache").toStdString();
    load();
}

CacheManager::~CacheManager() { save(); }

void CacheManager::addRecentPath(const QString &path) {
    history_.removeAll(path);
    history_.prepend(path);
    while (history_.size() > MAX_HISTORY) {
        history_.removeLast();
    }
}

QStringList CacheManager::recentPaths() const { return history_; }

void CacheManager::save() {
    json root;
    auto &arr = root["history"];
    for (const auto &p : history_) {
        arr.push_back(p.toStdString());
    }

    std::ofstream out(filePath_);
    if (!out.is_open()) {
        LOG_F(WARNING, "CacheManager: cannot write to %s", filePath_.c_str());
        return;
    }
    out << root.dump(2);
    out.close();
}

void CacheManager::load() {
    std::ifstream in(filePath_);
    if (!in.is_open()) {
        // First run, no cache file yet
        return;
    }
    try {
        json root;
        in >> root;

        if (root.contains("history") && root["history"].is_array()) {
            for (const auto &item : root["history"]) {
                history_.append(QString::fromStdString(item.get<std::string>()));
            }
        }
    } catch (std::exception &e) {
        LOG_F(WARNING, "CacheManager: failed to parse cache: %s", e.what());
        history_.clear();
    }
}
