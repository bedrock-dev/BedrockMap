#include "loader/worldsource.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>

#include "loader/levelpathmanager.h"
#include "loader/worldrepository.h"
#include "model/worldpath.h"

namespace {

    QString appWorldsRoot() {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        return base.isEmpty() ? QString{} : QDir(base).filePath(QStringLiteral("worlds"));
    }

#ifdef Q_OS_WIN
    QStringList desktopMinecraftRoots(bool scanLeviPath) {
        QStringList roots;
        const QString games = QString::fromStdString(LevelPathManager::GAMES_REL_PATH);
        const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
        if (!localAppData.isEmpty()) {
            for (const auto& package : {LevelPathManager::PACKAGE_UWP, LevelPathManager::PACKAGE_WINDOWS_BETA}) {
                roots << QDir(localAppData + QLatin1Char('/') + QString::fromStdString(package)).absoluteFilePath(games);
            }
        }

        const QString appData = qEnvironmentVariable("APPDATA");
        if (!appData.isEmpty()) {
            roots << QDir(appData).absoluteFilePath(QStringLiteral("MinecraftPC_Netease_PB/minecraftWorlds"));
            const auto appendUsers = [&roots, &games](const QString& base) {
                for (const auto& product : {QStringLiteral("Minecraft Bedrock"), QStringLiteral("Minecraft Bedrock Preview")}) {
                    const QDir users(QDir(base).absoluteFilePath(product + QStringLiteral("/Users")));
                    for (const auto& user : users.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                        roots << QDir(user.absoluteFilePath()).absoluteFilePath(games);
                    }
                }
            };
            appendUsers(appData);

            if (scanLeviPath) {
                QFile config(appData + QStringLiteral("/LeviLauncher.exe/config.json"));
                if (config.open(QIODevice::ReadOnly)) {
                    const auto doc = QJsonDocument::fromJson(config.readAll());
                    const QString baseRoot = doc.isObject() ? doc.object().value(QStringLiteral("base_root")).toString() : QString{};
                    if (!baseRoot.isEmpty()) {
                        const QDir versions(QDir(baseRoot).absoluteFilePath(QStringLiteral("versions")));
                        for (const auto& version : versions.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                            appendUsers(version.absoluteFilePath());
                        }
                    }
                }
            }
        }
        QStringList existing;
        for (const auto& root : roots) {
            if (QFileInfo(root).isDir()) existing << root;
        }
        existing.removeDuplicates();
        return existing;
    }
#endif

    void appendUserFolders(std::vector<WorldSource>& sources, const QStringList& customPaths, WorldSourceAvailability availability) {
        QSet<QString> seen;
        for (const auto& path : customPaths) {
            const QString normalized = world_path::normalize(path);
            if (normalized.isEmpty()) continue;
            const QString key = world_path::identityKey(normalized);
            if (seen.contains(key)) continue;
            seen.insert(key);
            const QString name = QFileInfo(normalized).fileName();
            sources.push_back({QStringLiteral("user:") + normalized,
                               name.isEmpty() ? QStringLiteral("Custom folder") : name,
                               WorldSourceKind::UserFolder,
                               WorldSourceAccess::Direct,
                               availability,
                               {normalized}});
        }
    }

}  // namespace

std::vector<WorldSource> makeWorldSources(const QStringList& customPaths, bool scanLeviPath, bool allFilesAccess) {
    std::vector<WorldSource> sources;

    const QString app_worlds = appWorldsRoot();
    if (!app_worlds.isEmpty()) {
        sources.push_back({QStringLiteral("app-internal"), QStringLiteral("App storage"),
                           WorldSourceKind::AppInternal, WorldSourceAccess::Direct,
                           WorldSourceAvailability::Available, {app_worlds}});
    }

#ifdef Q_OS_WIN
    const QStringList minecraft_roots = desktopMinecraftRoots(scanLeviPath);
    if (!minecraft_roots.isEmpty()) {
        sources.push_back({QStringLiteral("minecraft-default"), QStringLiteral("Minecraft"),
                           WorldSourceKind::MinecraftDefault, WorldSourceAccess::Direct,
                           WorldSourceAvailability::Available, minecraft_roots});
    }
    appendUserFolders(sources, customPaths, WorldSourceAvailability::Available);
#elif defined(Q_OS_ANDROID)
    // Shared folders need the user to grant "all files access" before the
    // native filesystem APIs can see them. The activity requests it on launch.
    const auto availability =
        allFilesAccess ? WorldSourceAvailability::Available : WorldSourceAvailability::NeedsAllFiles;
    appendUserFolders(sources, customPaths, availability);
    // MojangExternal is supplied by the privileged access layer in a later phase.
#else
    appendUserFolders(sources, customPaths, WorldSourceAvailability::Available);
#endif

    return sources;
}

std::vector<WorldMetadata> scanWorldSource(const WorldSource& source) {
    std::vector<WorldMetadata> result;

    if (source.kind == WorldSourceKind::MinecraftDefault) {
        LevelPathManager manager;
        manager.init();
        manager.scanNormalPaths();
        manager.scanModernPaths();
        result = manager.discoveredLevels();
    } else {
        for (const auto& root : source.roots) {
            if (!QFileInfo(root).isDir()) continue;

            // A selected folder may be the world itself or a directory that
            // contains several world folders.
            const auto self = WorldRepository::readWorld(root);
            if (self.isValid) {
                result.push_back(self);
                continue;
            }
            const QDir dir(root);
            for (const auto& entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                const auto world = WorldRepository::readWorld(entry.absoluteFilePath());
                if (world.isValid) result.push_back(world);
            }
        }
    }

    result.erase(std::remove_if(result.begin(), result.end(), [](const WorldMetadata& world) { return !world.isValid; }),
                 result.end());
    for (auto& world : result) {
        world.sourceKind = source.kind;
        world.sourceLabel = source.label.toStdString();
    }
    return result;
}
