#ifndef BEDROCKMAP_WORLDLISTMODEL_H
#define BEDROCKMAP_WORLDLISTMODEL_H

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QString>
#include <QStringList>

#include "loader/recentworldstore.h"
#include "loader/worldscanservice.h"
#include "loader/worldsource.h"

class WorldMetadataListModel : public QAbstractListModel {
   public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        NameRole,
        VersionRole,
        TimeRole,
        SizeRole,
        ModifiedRole,
        SourceRole,
        ThumbnailRole,
        XorEncryptedRole,
        AppInternalRole,
    };

    explicit WorldMetadataListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setWorlds(std::vector<WorldMetadata> worlds);
    [[nodiscard]] const std::vector<WorldMetadata>& worlds() const { return worlds_; }

   protected:
    static QVariant valueForRole(const WorldMetadata& metadata, int role);

   private:
    std::vector<WorldMetadata> worlds_;
};

class WorldListModel final : public WorldMetadataListModel {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* recentWorlds READ recentWorlds CONSTANT)
    Q_PROPERTY(QAbstractItemModel* allWorlds READ allWorlds CONSTANT)
    Q_PROPERTY(QStringList sourceOptions READ sourceOptions NOTIFY sourceOptionsChanged)
    Q_PROPERTY(QString sourceFilter READ sourceFilter WRITE setSourceFilter NOTIFY sourceFilterChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

   public:
    explicit WorldListModel(QObject* parent);

    [[nodiscard]] QAbstractItemModel* recentWorlds() { return &recent_model_; }
    [[nodiscard]] QAbstractItemModel* allWorlds() { return &filtered_model_; }
    [[nodiscard]] QStringList sourceOptions() const { return source_options_; }
    [[nodiscard]] QString sourceFilter() const { return source_filter_; }
    void setSourceFilter(const QString& source);
    [[nodiscard]] bool loading() const { return scan_service_.isLoading(); }
    [[nodiscard]] QString error() const { return scan_service_.error(); }

    Q_INVOKABLE void refresh();
    void recordRecentWorld(const QString& path);

   signals:
    void loadingChanged();
    void errorChanged();
    void sourceOptionsChanged();
    void sourceFilterChanged();

   private:
    void updateWorldLists();

    RecentWorldStore recent_store_;
    WorldScanService scan_service_;
    WorldMetadataListModel recent_model_;
    QSortFilterProxyModel filtered_model_;
    QStringList source_options_;
    QString source_filter_;
};

#endif  // BEDROCKMAP_WORLDLISTMODEL_H
