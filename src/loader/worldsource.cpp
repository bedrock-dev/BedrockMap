#include "loader/worldsource.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>
#include <utility>

#include "base/platform.h"
#include "io/documentio.h"
#include "level_dat.h"
#include "loader/leveldbxor.h"
#include "loader/leveloperator.h"

namespace {
    QUrl fileUrl(const QString& path) { return QUrl::fromLocalFile(path); }

    QByteArray readAll(const QString& path) {
        const auto read = io::DocumentIo::readAll(fileUrl(path));
        return read.ok() ? read.data : QByteArray{};
    }

    /// Child directories in name order. Launcher folders may be links or
    /// junctions, so a link counts when its target can be listed.
    QStringList childDirectories(const QString& path) {
        QStringList result;
        const auto children = io::DocumentIo::list(fileUrl(path));
        if (!children.ok()) return result;
        for (const auto& name : children.names) {
            const QString entry = QDir(path).filePath(name);
            const auto status = io::DocumentIo::stat(fileUrl(entry));
            if (!status.ok()) continue;
            if (status.info.isDirectory || (status.info.isLink && io::DocumentIo::list(fileUrl(entry)).ok())) result.push_back(entry);
        }
        return result;
    }

    WorldMetadata readMetadata(const QString& path) {
        WorldMetadata info;
        info.path = path.toStdString();
        const QDir directory(path);
        const auto child = [&](const char* name) { return directory.filePath(QString::fromLatin1(name)); };
        bl::level_dat dat;
        if (!LevelOperator::loadLevelDat(path, dat)) return info;
        info.levelName = dat.level_name();
        info.version = dat.min_compat_version().to_string();
        info.xorEncrypted = leveldb_xor::isEncrypted(path);
        const QByteArray nameRaw = readAll(child("levelname.txt"));
        if (!nameRaw.isEmpty()) {
            const auto name = QString::fromUtf8(nameRaw).trimmed();
            if (!name.isEmpty()) info.levelName = name.toStdString();
        }
        const QByteArray thumbnail = readAll(child("world_icon.jpeg"));
        if (!thumbnail.isEmpty()) {
            info.thumbnailDataUrl = "data:image/jpeg;base64," + thumbnail.toBase64().toStdString();
        }
        QStringList directories{path};
        while (!directories.empty()) {
            const QString current = directories.takeLast();
            const auto children = io::DocumentIo::list(fileUrl(current));
            if (!children.ok()) continue;
            for (const auto& name : children.names) {
                const QString entry = QDir(current).filePath(name);
                const auto status = io::DocumentIo::stat(fileUrl(entry));
                if (!status.ok() || status.info.isLink) continue;
                if (status.info.isDirectory)
                    directories.push_back(entry);
                else if (status.info.isFile) {
                    info.sizeBytes += status.info.size;
                    info.lastModified = std::max(info.lastModified, status.info.modified);
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
                for (const auto& user : childDirectories(QDir(base).filePath(product + QStringLiteral("/Users")))) {
                    append({.path = QDir(user).filePath(games),
                            .kind = levi ? WorldSourceKind::Levi : (preview ? WorldSourceKind::Preview : WorldSourceKind::Release)});
                }
            }
        };
        appendUsers(appData, false);

        if (!scanLeviPath) return;
        const auto config = io::DocumentIo::readAll(fileUrl(QDir(appData).filePath(QStringLiteral("LeviLauncher.exe/config.json"))), 1 << 20);
        if (!config.ok()) return;
        const auto doc = QJsonDocument::fromJson(config.data);
        const QString baseRoot = doc.object().value(QStringLiteral("base_root")).toString();
        if (baseRoot.isEmpty()) return;
        for (const auto& version : childDirectories(QDir(baseRoot).filePath(QStringLiteral("versions")))) {
            appendUsers(version, true);
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
    return readMetadata(normalized);
}

std::vector<WorldMetadata> scanWorldSource(const WorldSource& source) {
    std::vector<WorldMetadata> result;
    if (source.path.isEmpty()) return result;
    const auto children = io::DocumentIo::list(fileUrl(source.path));
    if (!children.ok()) {
        qInfo().noquote() << "[WorldScan] Source unavailable: path=" << source.path << "error=" << children.error;
        return result;
    }
    for (const auto& name : children.names) {
        const QString path = QDir(source.path).filePath(name);
        const auto status = io::DocumentIo::stat(fileUrl(path));
        if (!status.ok() || !status.info.isDirectory || status.info.isLink) continue;
        auto world = readMetadata(path);
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
