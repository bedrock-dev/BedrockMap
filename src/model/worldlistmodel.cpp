#include "model/worldlistmodel.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <utility>

#include "loader/worldrepository.h"

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

WorldMetadataListModel::WorldMetadataListModel(QObject* parent) : QAbstractListModel(parent) {}

int WorldMetadataListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(worlds_.size());
}

QVariant WorldMetadataListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    return valueForRole(worlds_[static_cast<std::size_t>(index.row())], role);
}

QHash<int, QByteArray> WorldMetadataListModel::roleNames() const {
    return {
        {PathRole, "path"},
        {NameRole, "name"},
        {VersionRole, "version"},
        {TimeRole, "time"},
        {SizeRole, "size"},
        {ModifiedRole, "modified"},
        {SourceRole, "source"},
        {PreviewRole, "preview"},
        {ThumbnailRole, "thumbnail"},
        {NeteaseRole, "netease"},
        {XorEncryptedRole, "xorEncrypted"},
    };
}

void WorldMetadataListModel::setWorlds(std::vector<WorldMetadata> worlds) {
    beginResetModel();
    worlds_ = std::move(worlds);
    endResetModel();
}

QVariant WorldMetadataListModel::valueForRole(const WorldMetadata& metadata, int role) {
    switch (role) {
        case Qt::DisplayRole:
        case NameRole:
            return QString::fromStdString(metadata.levelName.empty() ? metadata.path : metadata.levelName);
        case PathRole:
            return QString::fromStdString(metadata.path);
        case VersionRole:
            return QString::fromStdString(metadata.version);
        case TimeRole:
            return formatTime(metadata.lastModified);
        case SizeRole:
            return formatSize(metadata.sizeBytes);
        case ModifiedRole:
            return static_cast<qlonglong>(metadata.lastModified);
        case SourceRole:
            switch (metadata.sourceKind) {
                case WorldSourceKind::AppInternal:
                    return QStringLiteral("Imported");
                case WorldSourceKind::UserFolder:
                    return metadata.sourceLabel.empty() ? QStringLiteral("Custom") : QString::fromStdString(metadata.sourceLabel);
                case WorldSourceKind::MojangExternal:
                    return QStringLiteral("Mojang");
                case WorldSourceKind::MinecraftDefault:
                    break;
            }
            return metadata.preview ? QStringLiteral("Preview")
                                    : (metadata.netease ? QStringLiteral("NetEase") : QStringLiteral("Bedrock"));
        case PreviewRole:
            return metadata.preview;
        case ThumbnailRole: {
            const QFileInfo thumbnail(QString::fromStdString(metadata.path) + QStringLiteral("/world_icon.jpeg"));
            return thumbnail.isFile() ? QUrl::fromLocalFile(thumbnail.absoluteFilePath()) : QUrl();
        }
        case NeteaseRole:
            return metadata.netease;
        case XorEncryptedRole:
            return metadata.xorEncrypted;
        default:
            return {};
    }
}

WorldListModel::WorldListModel(QObject* parent)
    : WorldMetadataListModel(parent),
      scan_service_(this),
      recent_model_(this),
      release_model_(this),
      preview_model_(this),
      netease_model_(this) {
    connect(&scan_service_, &WorldScanService::finished, this, &WorldListModel::updateWorldLists);
    connect(&scan_service_, &WorldScanService::loadingChanged, this, &WorldListModel::loadingChanged);
    connect(&scan_service_, &WorldScanService::progressChanged, this, &WorldListModel::progressChanged);
    connect(&scan_service_, &WorldScanService::errorChanged, this, &WorldListModel::errorChanged);
    refresh();
}

void WorldListModel::refresh() {
    reloadRecentWorlds();
    scan_service_.start();
}

void WorldListModel::reloadRecentWorlds() {
    recent_store_.load();
    recent_store_.pruneMissing();
    std::vector<WorldMetadata> recent_worlds;
    for (const auto& path : recent_store_.paths()) {
        if (!QDir(path).exists()) continue;
        const auto info = WorldRepository::readWorld(path);
        if (info.isValid) recent_worlds.push_back(info);
    }
    recent_model_.setWorlds(std::move(recent_worlds));
}

void WorldListModel::recordRecentWorld(const QString& path) {
    recent_store_.add(path);
    recent_store_.save();
    reloadRecentWorlds();
}

QStringList WorldListModel::recentPaths() const {
    QStringList result;
    result.reserve(static_cast<qsizetype>(recent_model_.worlds().size()));
    for (const auto& world : recent_model_.worlds()) result.append(QString::fromStdString(world.path));
    return result;
}

void WorldListModel::updateWorldLists() {
    reloadRecentWorlds();
    std::vector<WorldMetadata> release;
    std::vector<WorldMetadata> preview;
    std::vector<WorldMetadata> netease;
    for (const auto& world : scan_service_.worlds()) {
        if (world.netease)
            netease.push_back(world);
        else if (world.preview)
            preview.push_back(world);
        else
            release.push_back(world);
    }
    release_model_.setWorlds(std::move(release));
    preview_model_.setWorlds(std::move(preview));
    netease_model_.setWorlds(std::move(netease));
    setWorlds(scan_service_.worlds());
}
