#ifndef BEDROCKMAP_WORLDLISTMODEL_H
#define BEDROCKMAP_WORLDLISTMODEL_H

#include <QAbstractListModel>
#include <QString>
#include <QStringList>

#include "loader/recentworldstore.h"
#include "loader/worldscanservice.h"
#include "model/worldmetadata.h"

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
        PreviewRole,
        ThumbnailRole,
        NeteaseRole,
        XorEncryptedRole,
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
    Q_PROPERTY(QAbstractItemModel* releaseWorlds READ releaseWorlds CONSTANT)
    Q_PROPERTY(QAbstractItemModel* previewWorlds READ previewWorlds CONSTANT)
    Q_PROPERTY(QAbstractItemModel* neteaseWorlds READ neteaseWorlds CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

   public:
    explicit WorldListModel(QObject* parent = nullptr);

    [[nodiscard]] QAbstractItemModel* recentWorlds() { return &recent_model_; }
    [[nodiscard]] QAbstractItemModel* allWorlds() { return this; }
    [[nodiscard]] QAbstractItemModel* releaseWorlds() { return &release_model_; }
    [[nodiscard]] QAbstractItemModel* previewWorlds() { return &preview_model_; }
    [[nodiscard]] QAbstractItemModel* neteaseWorlds() { return &netease_model_; }
    [[nodiscard]] bool loading() const { return scan_service_.isLoading(); }
    [[nodiscard]] int progress() const { return scan_service_.progress(); }
    [[nodiscard]] QString error() const { return scan_service_.error(); }

    Q_INVOKABLE void refresh();
    void reloadRecentWorlds();
    void recordRecentWorld(const QString& path);
    [[nodiscard]] QStringList recentPaths() const;

   signals:
    void loadingChanged();
    void progressChanged();
    void errorChanged();

   private:
    void updateWorldLists();

    RecentWorldStore recent_store_;
    WorldScanService scan_service_;
    WorldMetadataListModel recent_model_;
    WorldMetadataListModel release_model_;
    WorldMetadataListModel preview_model_;
    WorldMetadataListModel netease_model_;
};

#endif  // BEDROCKMAP_WORLDLISTMODEL_H
