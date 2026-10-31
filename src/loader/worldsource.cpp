#include "loader/worldsource.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>
#include <utility>

#include "base/platform.h"
#include "base/worldfilesource.h"
#include "level_dat.h"
#include "loader/leveldbxor.h"
#include "loader/leveloperator.h"

namespace {
    QByteArray readAll(platform::WorldFileSource& source, const QString& path) {
        const auto input = source.openRead(path);
        return input ? input->readAll() : QByteArray{};
    }

    WorldMetadata readMetadata(const QString& path, platform::WorldFileSource& source) {
        WorldMetadata info;
        info.path = path.toStdString();
        const QDir directory(path);
        const auto child = [&](const char* name) { return directory.filePath(QString::fromLatin1(name)); };
        bl::level_dat dat;
        if (!LevelOperator::loadLevelDat(source, path, dat)) return info;
        info.levelName = dat.level_name();
        info.version = dat.min_compat_version().to_string();
        info.xorEncrypted = leveldb_xor::isEncrypted(source, path);
        const QByteArray nameRaw = readAll(source, child("levelname.txt"));
        if (!nameRaw.isEmpty()) {
            const auto name = QString::fromUtf8(nameRaw).trimmed();
            if (!name.isEmpty()) info.levelName = name.toStdString();
        }
        const QByteArray thumbnail = readAll(source, child("world_icon.jpeg"));
        if (!thumbnail.isEmpty()) {
            info.thumbnailDataUrl = "data:image/jpeg;base64," + thumbnail.toBase64().toStdString();
        }
        platform::WorldFileInfo file;
        QStringList directories{path};
        while (!directories.empty()) {
            const QString current = directories.takeLast();
            QStringList children;
            if (!source.children(current, &children).isEmpty()) continue;
            for (const auto& name : children) {
                const QString entry = QDir(current).filePath(name);
                if (!source.fileInfo(entry, file).isEmpty() || file.isLink) continue;
                if (file.isDirectory)
                    directories.push_back(entry);
                else if (file.isFile) {
                    info.sizeBytes += file.size;
                    info.lastModified = std::max(info.lastModified, file.modified);
                }
            }
        }
        info.isValid = true;
        return info;
    }

#ifdef Q_OS_WIN
    void appendMinecraftPaths(std::vector<WorldSource>& sources, bool scanLeviPath) {
        const QString games = QStringLiteral("games/com.mojang/minecraftWorlds");
        QSet<QString> seen;
        const auto append = [&](WorldSource source) {
            source.path = platform::normalizeLocalPath(source.path);
            if (source.path.isEmpty() || seen.contains(source.path)) return;
            seen.insert(source.path);
            sources.push_back(std::move(source));
        };

        const QString local = qEnvironmentVariable("LOCALAPPDATA");
        if (!local.isEmpty()) {
            for (bool preview : {false, true}) {
                const QString package = preview ? QStringLiteral("Packages/Microsoft.MinecraftWindowsBeta_8wekyb3d8bbwe/LocalState")
                                                : QStringLiteral("Packages/Microsoft.MinecraftUWP_8wekyb3d8bbwe/LocalState");
                append({.path = QDir(local + QLatin1Char('/') + package).filePath(games),
                        .kind = preview ? WorldSourceKind::Preview : WorldSourceKind::Release});
            }
        }

        const QString appData = qEnvironmentVariable("APPDATA");
        if (appData.isEmpty()) return;
        append(
            {.path = QDir(appData).filePath(QStringLiteral("MinecraftPC_Netease_PB/minecraftWorlds")), .kind = WorldSourceKind::NetEase});

        const auto appendUsers = [&](const QString& base, bool levi) {
            for (bool preview : {false, true}) {
                const QString product = preview ? QStringLiteral("Minecraft Bedrock Preview") : QStringLiteral("Minecraft Bedrock");
                const QDir users(QDir(base).filePath(product + QStringLiteral("/Users")));
                for (const auto& user : users.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                    append({.path = QDir(user.absoluteFilePath()).filePath(games),
                            .kind = levi ? WorldSourceKind::Levi : (preview ? WorldSourceKind::Preview : WorldSourceKind::Release)});
                }
            }
        };
        appendUsers(appData, false);

        if (!scanLeviPath) return;
        QFile config(QDir(appData).filePath(QStringLiteral("LeviLauncher.exe/config.json")));
        if (!config.open(QIODevice::ReadOnly)) return;
        const auto doc = QJsonDocument::fromJson(config.readAll());
        const QString baseRoot = doc.object().value(QStringLiteral("base_root")).toString();
        if (baseRoot.isEmpty()) return;
        const QDir versions(QDir(baseRoot).filePath(QStringLiteral("versions")));
        for (const auto& version : versions.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            appendUsers(version.absoluteFilePath(), true);
        }
    }
#endif
}  // namespace

std::vector<WorldSource> makeWorldSources(const QStringList& customPaths, bool scanLeviPath) {
    std::vector<WorldSource> sources;
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!appData.isEmpty()) {
        sources.push_back({.path = QDir(appData).filePath(QStringLiteral("worlds")), .kind = WorldSourceKind::AppInternal});
    }
#ifdef Q_OS_WIN
    appendMinecraftPaths(sources, scanLeviPath);
#else
    Q_UNUSED(scanLeviPath);
#endif

#ifdef Q_OS_ANDROID
    sources.push_back({.path = QStringLiteral("/data/user/0/com.mojang.minecraftpe/games/com.mojang/minecraftWorlds"),
                       .kind = WorldSourceKind::AndroidLegacy});
    sources.push_back({.path = QStringLiteral("/data/data/com.mojang.minecraftpe/files/games/com.mojang/minecraftWorlds"),
                       .kind = WorldSourceKind::AndroidReleaseInternal});
    sources.push_back(
        {.path = QStringLiteral("/storage/emulated/0/Android/data/com.mojang.minecraftpe/files/games/com.mojang/minecraftWorlds"),
         .kind = WorldSourceKind::AndroidReleaseExternal});
#endif

    QSet<QString> seen;
    for (const auto& path : customPaths) {
        const QString normalized = platform::normalizeLocalPath(path);
        if (normalized.isEmpty() || seen.contains(normalized)) continue;
        seen.insert(normalized);
        sources.push_back({.path = normalized, .kind = WorldSourceKind::External});
    }
    return sources;
}

WorldMetadata readWorldMetadata(const QString& path) {
    const auto normalized = platform::normalizeLocalPath(path);
    if (normalized.isEmpty()) return {};
    const auto source = platform::worldFileSource(normalized);
    return source ? readMetadata(normalized, *source) : WorldMetadata{};
}

std::vector<WorldMetadata> scanWorldSource(const WorldSource& source) {
    std::vector<WorldMetadata> result;
    if (source.path.isEmpty()) return result;
    const auto files = platform::worldFileSource(source.path);
    if (!files) return result;
    QStringList children;
    if (!files->children(source.path, &children).isEmpty()) return result;
    for (const auto& name : children) {
        const QString path = QDir(source.path).filePath(name);
        platform::WorldFileInfo file;
        if (!files->fileInfo(path, file).isEmpty() || !file.isDirectory || file.isLink) continue;
        auto world = readMetadata(path, *files);
        if (!world.isValid) continue;
        world.sourceKind = source.kind;
        result.push_back(std::move(world));
    }
    qInfo().noquote() << "[WorldScan] Source complete: path=" << source.path << "accepted=" << result.size();
    return result;
}

QString worldSourceLabel(WorldSourceKind kind) {
    switch (kind) {
        case WorldSourceKind::Release:
            return QString::fromUtf8(QT_TRANSLATE_NOOP("WorldSource", "Release"));
        case WorldSourceKind::Preview:
            return QString::fromUtf8(QT_TRANSLATE_NOOP("WorldSource", "Preview"));
        case WorldSourceKind::NetEase:
            return QString::fromUtf8(QT_TRANSLATE_NOOP("WorldSource", "NetEase"));
        case WorldSourceKind::Levi:
            return QString::fromUtf8(QT_TRANSLATE_NOOP("WorldSource", "Levi"));
        case WorldSourceKind::AndroidLegacy:
            return QString::fromUtf8(QT_TRANSLATE_NOOP("WorldSource", "Legacy"));
        case WorldSourceKind::AndroidReleaseInternal:
            return QString::fromUtf8(QT_TRANSLATE_NOOP("WorldSource", "Release internal"));
        case WorldSourceKind::AndroidReleaseExternal:
            return QString::fromUtf8(QT_TRANSLATE_NOOP("WorldSource", "Release external"));
        case WorldSourceKind::AppInternal:
            return QString::fromUtf8(QT_TRANSLATE_NOOP("WorldSource", "App internal"));
        case WorldSourceKind::External:
            return QString::fromUtf8(QT_TRANSLATE_NOOP("WorldSource", "External"));
    }
    return {};
}
