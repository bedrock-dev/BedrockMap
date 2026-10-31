#include "model/worldlistmodel.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QUrl>
#include <utility>

#include "loader/worldsource.h"

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
        {ThumbnailRole, "thumbnail"},
        {XorEncryptedRole, "xorEncrypted"},
        {AppInternalRole, "appInternal"},
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
            return worldSourceLabel(metadata.sourceKind);
        case ThumbnailRole: {
            if (!metadata.thumbnailDataUrl.empty()) return QUrl(QString::fromStdString(metadata.thumbnailDataUrl));
            return QUrl();
        }
        case XorEncryptedRole:
            return metadata.xorEncrypted;
        case AppInternalRole:
            return metadata.sourceKind == WorldSourceKind::AppInternal;
        default:
            return {};
    }
}

WorldListModel::WorldListModel(QObject* parent)
    : WorldMetadataListModel(parent), scan_service_(this), recent_model_(this), filtered_model_(this) {
    filtered_model_.setSourceModel(this);
    filtered_model_.setFilterRole(SourceRole);
    connect(&scan_service_, &WorldScanService::finished, this, &WorldListModel::updateWorldLists);
    connect(&scan_service_, &WorldScanService::loadingChanged, this, &WorldListModel::loadingChanged);
    connect(&scan_service_, &WorldScanService::errorChanged, this, &WorldListModel::errorChanged);
    refresh();
}

void WorldListModel::setSourceFilter(const QString& source) {
    if (source_filter_ == source) return;
    source_filter_ = source;
    // Match the complete stable label, so "Release" does not also accept
    // "Release internal". An empty filter represents all sources.
    filtered_model_.setFilterRegularExpression(source.isEmpty() ? QString{}
                                                                : QRegularExpression::anchoredPattern(QRegularExpression::escape(source)));
    emit sourceFilterChanged();
}

void WorldListModel::refresh() {
    recent_store_.load();
    recent_store_.pruneMissing();
    scan_service_.start(recent_store_.paths());
}

void WorldListModel::recordRecentWorld(const QString& path) {
    recent_store_.add(path);
    recent_store_.save();
    scan_service_.start(recent_store_.paths());
}

void WorldListModel::updateWorldLists() {
    recent_model_.setWorlds(scan_service_.recentWorlds());
    setWorlds(scan_service_.worlds());
    QStringList sources;
    for (const auto& world : worlds()) {
        const QString source = worldSourceLabel(world.sourceKind);
        if (!sources.contains(source)) sources.append(source);
    }
    const bool sourcesChanged = source_options_ != sources;
    source_options_ = std::move(sources);
    if (!source_filter_.isEmpty() && !source_options_.contains(source_filter_)) setSourceFilter({});
    if (sourcesChanged) emit sourceOptionsChanged();
}
