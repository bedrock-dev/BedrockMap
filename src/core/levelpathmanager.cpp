#include "levelpathmanager.h"
#include "apppaths.h"
#include "recentworldstore.h"
#include "worldrepository.h"

#include <qdir.h>
#include <qdiriterator.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>

#include <QStandardPaths>

#include "config.h"
#include "level_dat.h"
#include "leveldbxor.h"
#include "loguru/loguru.hpp"

namespace {

    void scanWorldsInDir(const QString& worldsDir, bool modern, bool preview, bool netease, std::vector<LevelPathInfo>& out) {
        QDir dir(worldsDir);
        LOG_F(INFO, "scanWorldsInDir: %s exists=%d", worldsDir.toStdString().c_str(), dir.exists());
        if (!dir.exists()) return;
        const auto entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const auto& sub : entries) {
            auto info = LevelPathManager::makeLevelInfo(sub.absoluteFilePath());
            info.modern = modern;
            info.preview = preview;
            info.netease = netease;
            out.push_back(info);
        }
    }

}  // namespace

LevelPathInfo LevelPathManager::makeLevelInfo(const QString& dirPath) {
    return WorldRepository::readWorld(dirPath);
}

const std::string LevelPathManager::GAMES_REL_PATH = "games/com.mojang/minecraftWorlds";
const std::string LevelPathManager::PACKAGE_UWP = "Packages/Microsoft.MinecraftUWP_8wekyb3d8bbwe/LocalState";
const std::string LevelPathManager::PACKAGE_WINDOWS_BETA = "Packages/Microsoft.MinecraftWindowsBeta_8wekyb3d8bbwe/LocalState";
const std::string LevelPathManager::DIR_MINECRAFT_BEDROCK = "Minecraft Bedrock";
const std::string LevelPathManager::DIR_MINECRAFT_BEDROCK_PREVIEW = "Minecraft Bedrock Preview";

LevelPathManager::LevelPathManager() : recent_store_(std::make_unique<RecentWorldStore>()) {}

LevelPathManager::~LevelPathManager() = default;

void LevelPathManager::addRecentPath(const QString& path) {
    recent_store_->add(path);
}

QStringList LevelPathManager::recentPaths() const { return recent_store_->paths(); }

void LevelPathManager::saveHistory() {
    recent_store_->save();
}

void LevelPathManager::loadHistory() {
    recent_store_->load();
}

void LevelPathManager::init() {
    QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    QString appData = qEnvironmentVariable("APPDATA");
    if (localAppData.isEmpty() || appData.isEmpty()) return;

    scan_paths_.push_back({appData.toStdString(), {}, "Official", true, false});

    if (setting::current().SCAN_LEVI_PATH) {
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
    for (const auto& ver : versions) {
        scan_paths_.push_back({QDir(baseRoot).absoluteFilePath("versions/" + ver).toStdString(), ver.toStdString(), "Levi", true, false});
    }
}

void LevelPathManager::scanNormalPaths() {
    discovered_levels_ = WorldRepository::scanNormal();
}

void LevelPathManager::scanModernPaths() {
    auto modern = WorldRepository::scanModern(scan_paths_);
    discovered_levels_.insert(discovered_levels_.end(), modern.begin(), modern.end());
}

void LevelPathManager::dumpPaths() {
    LOG_F(INFO, "=== Scan paths ===");
    for (const auto& e : scan_paths_) {
        LOG_F(INFO, "  [%s] tag=%s modern=%d version=%s", e.path.c_str(), e.tag.c_str(), e.modern, e.version.c_str());
    }
    LOG_F(INFO, "=== Discovered levels ===");
    for (const auto& l : discovered_levels_) {
        LOG_F(INFO, "  [%s] valid=%d modern=%d preview=%d netease=%d xor=%d ver=%s name=%s", l.path.c_str(), l.isValid, l.modern, l.preview,
              l.netease, l.xorEncrypted, l.version.c_str(), l.levelName.c_str());
    }
}
