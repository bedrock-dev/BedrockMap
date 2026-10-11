#include "worldrepository.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

#include "config.h"
#include "level_dat.h"
#include "leveldbxor.h"

namespace {
void scanDir(const QString& root, bool preview, bool netease, std::vector<LevelPathInfo>& out) {
    QDir dir(root);
    if (!dir.exists()) return;
    for (const auto& entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        auto info = WorldRepository::readWorld(entry.absoluteFilePath());
        info.preview = preview;
        info.netease = netease;
        out.push_back(std::move(info));
    }
}
}  // namespace

LevelPathInfo WorldRepository::readWorld(const QString& dirPath) {
    LevelPathInfo info;
    info.path = dirPath.toStdString();
    QDir dir(dirPath);
    const bool hasLevelDat = dir.exists(QStringLiteral("level.dat"));
    const bool hasDb = dir.exists(QStringLiteral("db"));
    info.isValid = hasLevelDat && hasDb;
    info.xorEncrypted = leveldb_xor::isEncrypted(dirPath.toStdString());
    QFile nameFile(dirPath + QStringLiteral("/levelname.txt"));
    if (nameFile.open(QIODevice::ReadOnly)) info.levelName = QString::fromUtf8(nameFile.readAll()).trimmed().toStdString();
    if (hasLevelDat) {
        bl::level_dat dat;
        if (dat.load_from_file((dirPath + QStringLiteral("/level.dat")).toStdString())) {
            if (info.levelName.empty()) info.levelName = dat.level_name();
            info.version = dat.min_compat_version().to_string();
        } else info.isValid = false;
    }
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const auto file = it.fileInfo();
        info.sizeBytes += file.size();
        info.lastModified = std::max(info.lastModified, file.lastModified().toSecsSinceEpoch());
    }
    return info;
}

std::vector<LevelPathInfo> WorldRepository::scanNormal() {
    std::vector<LevelPathInfo> result;
    const auto local = qEnvironmentVariable("LOCALAPPDATA");
    if (!local.isEmpty()) {
        scanDir(QDir(local + "/" + QString::fromStdString(LevelPathManager::PACKAGE_UWP)).absoluteFilePath(QString::fromStdString(LevelPathManager::GAMES_REL_PATH)), false, false, result);
        scanDir(QDir(local + "/" + QString::fromStdString(LevelPathManager::PACKAGE_WINDOWS_BETA)).absoluteFilePath(QString::fromStdString(LevelPathManager::GAMES_REL_PATH)), false, false, result);
    }
    const auto appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty()) scanDir(QDir(appData).absoluteFilePath("MinecraftPC_Netease_PB/minecraftWorlds"), false, true, result);
    return result;
}

std::vector<LevelPathInfo> WorldRepository::scanModern(const std::vector<ScanPathEntry>& paths) {
    std::vector<LevelPathInfo> result;
    for (const auto& entry : paths) {
        if (!entry.modern) continue;
        for (const auto& pair : {std::pair<QString, bool>{"Minecraft Bedrock", false}, {"Minecraft Bedrock Preview", true}}) {
            const QDir users(QDir(QString::fromStdString(entry.path) + "/" + pair.first).absoluteFilePath("Users"));
            if (!users.exists()) continue;
            for (const auto& user : users.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                const auto worlds = QDir(user.absoluteFilePath()).absoluteFilePath(QString::fromStdString(LevelPathManager::GAMES_REL_PATH));
                scanDir(worlds, pair.second, false, result);
                for (auto& info : result) info.modern = true;
            }
        }
    }
    return result;
}
