#include "levelpathmanager.h"

#include <qdir.h>
#include <qdiriterator.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qjsondocument.h>
#include <qjsonobject.h>

#include <QStandardPaths>
#include <fstream>

#include "config.h"
#include "json/json.hpp"
#include "level_dat.h"
#include "loguru/loguru.hpp"

using json = nlohmann::json;

namespace {

    void scanWorldsInDir(const QString &worldsDir, bool modern, bool preview, std::vector<LevelPathInfo> &out) {
        QDir dir(worldsDir);
        LOG_F(INFO, "scanWorldsInDir: %s exists=%d", worldsDir.toStdString().c_str(), dir.exists());
        if (!dir.exists()) return;
        const auto entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const auto &sub : entries) {
            auto info = LevelPathManager::makeLevelInfo(sub.absoluteFilePath());
            info.modern = modern;
            info.preview = preview;
            out.push_back(info);
        }
    }

}  // namespace

LevelPathInfo LevelPathManager::makeLevelInfo(const QString &dirPath) {
    LevelPathInfo info;
    info.path = dirPath.toStdString();

    QDir dir(dirPath);
    bool hasLevelDat = dir.exists("level.dat");
    bool hasDb = dir.exists("db");
    info.isValid = hasLevelDat && hasDb;

    // levelname.txt
    QFile nameFile(dirPath + "/levelname.txt");
    if (nameFile.open(QIODevice::ReadOnly)) {
        info.levelName = QString::fromUtf8(nameFile.readAll()).trimmed().toStdString();
        nameFile.close();
    }

    // version from level.dat clientversion
    if (hasLevelDat) {
        bl::level_dat dat;
        if (dat.load_from_file((dirPath + "/level.dat").toStdString())) {
            if (info.levelName.empty()) {
                info.levelName = dat.level_name();
            }
            info.version = dat.min_compat_version().to_string();
        } else {
            info.isValid = false;
        }
    }

    // total size + newest file mtime
    int64_t totalSize = 0;
    int64_t newestTime = 0;
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFileInfo fi = it.fileInfo();
        totalSize += fi.size();
        int64_t mtime = fi.lastModified().toSecsSinceEpoch();
        if (mtime > newestTime) newestTime = mtime;
    }
    info.sizeBytes = totalSize;
    info.lastModified = newestTime;

    return info;
}

const std::string LevelPathManager::GAMES_REL_PATH = "games/com.mojang/minecraftWorlds";
const std::string LevelPathManager::PACKAGE_UWP = "Packages/Microsoft.MinecraftUWP_8wekyb3d8bbwe/LocalState";
const std::string LevelPathManager::PACKAGE_WINDOWS_BETA = "Packages/Microsoft.MinecraftWindowsBeta_8wekyb3d8bbwe/LocalState";
const std::string LevelPathManager::DIR_MINECRAFT_BEDROCK = "Minecraft Bedrock";
const std::string LevelPathManager::DIR_MINECRAFT_BEDROCK_PREVIEW = "Minecraft Bedrock Preview";

LevelPathManager::LevelPathManager() {
    QFileInfo configInfo(cfg::CONFIG_FILE_PATH.c_str());
    filePath_ = configInfo.absoluteDir().absoluteFilePath("cache").toStdString();
    loadHistory();
}

LevelPathManager::~LevelPathManager() { saveHistory(); }

void LevelPathManager::addRecentPath(const QString &path) {
    history_.removeAll(path);
    history_.prepend(path);
    while (history_.size() > MAX_HISTORY) {
        history_.removeLast();
    }
}

QStringList LevelPathManager::recentPaths() const { return history_; }

void LevelPathManager::saveHistory() {
    json root;
    auto &arr = root["history"];
    for (const auto &p : history_) {
        arr.push_back(p.toStdString());
    }

    std::ofstream out(filePath_);
    if (!out.is_open()) {
        LOG_F(WARNING, "LevelPathManager: cannot write cache to %s", filePath_.c_str());
        return;
    }
    out << root.dump(2);
}

void LevelPathManager::loadHistory() {
    std::ifstream in(filePath_);
    if (!in.is_open()) return;
    try {
        json root;
        in >> root;
        if (root.contains("history") && root["history"].is_array()) {
            for (const auto &item : root["history"]) {
                history_.append(QString::fromStdString(item.get<std::string>()));
            }
        }
    } catch (std::exception &e) {
        LOG_F(WARNING, "LevelPathManager: failed to parse cache: %s", e.what());
        history_.clear();
    }
}

void LevelPathManager::init() {
    QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    QString appData = qEnvironmentVariable("APPDATA");
    if (localAppData.isEmpty() || appData.isEmpty()) return;

    scan_paths_.push_back({appData.toStdString(), {}, "Official", true, false});

    if (cfg::SCAN_LEVI_PATH) {
        initLeviPath();
    }
}

void LevelPathManager::initLeviPath() {
    QFile file(QString(qEnvironmentVariable("APPDATA")) + "/LeviLauncher.exe/config.json");
    if (!file.open(QIODevice::ReadOnly)) return;
    auto doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) return;

    auto root = doc.object();
    auto baseRoot = root.value("base_root").toString();
    if (baseRoot.isEmpty()) return;

    QDir versionsDir(baseRoot + "/versions");
    if (!versionsDir.exists()) return;

    auto versions = versionsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto &ver : versions) {
        scan_paths_.push_back({QDir(baseRoot).absoluteFilePath("versions/" + ver).toStdString(), ver.toStdString(), "Levi", true, false});
    }
}

void LevelPathManager::scanNormalPaths() {
    discovered_levels_.clear();
    QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (localAppData.isEmpty()) return;

    scanWorldsInDir(QDir(localAppData + "/" + QString::fromStdString(PACKAGE_UWP)).absoluteFilePath(QString::fromStdString(GAMES_REL_PATH)),
                    false, false, discovered_levels_);
    scanWorldsInDir(
        QDir(localAppData + "/" + QString::fromStdString(PACKAGE_WINDOWS_BETA)).absoluteFilePath(QString::fromStdString(GAMES_REL_PATH)),
        false, false, discovered_levels_);
}

void LevelPathManager::scanModernPaths() {
    struct DirInfo {
        QString name;
        bool preview;
    };
    static const DirInfo kDirs[] = {
        {QString::fromStdString(DIR_MINECRAFT_BEDROCK), false},
        {QString::fromStdString(DIR_MINECRAFT_BEDROCK_PREVIEW), true},
    };

    for (const auto &entry : scan_paths_) {
        if (!entry.modern) continue;
        QString basePath = QString::fromStdString(entry.path);

        for (const auto &di : kDirs) {
            QString gameDir = basePath + "/" + di.name;
            QDir dir(gameDir);
            if (!dir.exists()) {
                LOG_F(INFO, "scanModern: game dir not found %s", gameDir.toStdString().c_str());
                continue;
            }

            QString usersDir = gameDir + "/Users";
            QDir users(usersDir);
            if (!users.exists()) {
                LOG_F(INFO, "scanModern: User dir not found %s", usersDir.toStdString().c_str());
                continue;
            }

            const auto userDirs = users.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const auto &user : userDirs) {
                QString worldsDir = QDir(user.absoluteFilePath()).absoluteFilePath(QString::fromStdString(GAMES_REL_PATH));
                QDir worlds(worldsDir);
                if (!worlds.exists()) {
                    LOG_F(INFO, "scanModern: worlds dir not found for user %s", user.fileName().toStdString().c_str());
                    continue;
                }

                const auto worldEntries = worlds.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
                for (const auto &world : worldEntries) {
                    auto info = makeLevelInfo(world.absoluteFilePath());
                    info.modern = true;
                    info.preview = di.preview;
                    discovered_levels_.push_back(info);
                }
            }
        }
    }
}

void LevelPathManager::dumpPaths() {
    LOG_F(INFO, "=== Scan paths ===");
    for (const auto &e : scan_paths_) {
        LOG_F(INFO, "  [%s] tag=%s modern=%d version=%s", e.path.c_str(), e.tag.c_str(), e.modern, e.version.c_str());
    }
    LOG_F(INFO, "=== Discovered levels ===");
    for (const auto &l : discovered_levels_) {
        LOG_F(INFO, "  [%s] valid=%d modern=%d preview=%d ver=%s name=%s", l.path.c_str(), l.isValid, l.modern, l.preview,
              l.version.c_str(), l.levelName.c_str());
    }
}
