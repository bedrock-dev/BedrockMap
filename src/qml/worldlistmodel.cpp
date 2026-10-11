#include "worldlistmodel.h"

#include <QDateTime>
#include <QDir>

namespace {

QString formatSize(std::int64_t bytes) {
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024);
    if (bytes < 1024LL * 1024 * 1024) return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString formatTime(std::int64_t timestamp) {
    if (timestamp <= 0) return {};
    return QDateTime::fromSecsSinceEpoch(timestamp).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

}  // namespace

WorldListModel::WorldListModel(QObject* parent) : QObject(parent) { refresh(); }

void WorldListModel::refresh() {
    manager_.init();
    manager_.scanNormalPaths();
    manager_.scanModernPaths();

    recent_worlds_.clear();
    all_worlds_.clear();

    for (const auto& path : manager_.recentPaths()) {
        if (!QDir(path).exists()) continue;
        const auto info = LevelPathManager::makeLevelInfo(path);
        if (info.isValid) recent_worlds_.append(toMap(info));
    }

    for (const auto& info : manager_.discoveredLevels()) {
        if (info.isValid) all_worlds_.append(toMap(info));
    }
    emit worldsChanged();
}

QVariantMap WorldListModel::toMap(const LevelPathInfo& info) {
    QVariantMap result;
    result.insert(QStringLiteral("path"), QString::fromStdString(info.path));
    result.insert(QStringLiteral("name"), QString::fromStdString(info.levelName.empty() ? info.path : info.levelName));
    result.insert(QStringLiteral("version"), QString::fromStdString(info.version));
    result.insert(QStringLiteral("time"), formatTime(info.lastModified));
    result.insert(QStringLiteral("size"), formatSize(info.sizeBytes));
    result.insert(QStringLiteral("modified"), info.lastModified);
    result.insert(QStringLiteral("source"), info.preview ? QStringLiteral("Preview") : (info.netease ? QStringLiteral("NetEase") : QStringLiteral("Bedrock")));
    result.insert(QStringLiteral("preview"), info.preview);
    result.insert(QStringLiteral("netease"), info.netease);
    result.insert(QStringLiteral("xorEncrypted"), info.xorEncrypted);
    return result;
}
