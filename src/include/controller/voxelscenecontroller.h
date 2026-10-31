#ifndef BEDROCKMAP_VOXELSCENECONTROLLER_H
#define BEDROCKMAP_VOXELSCENECONTROLLER_H

#include <QByteArray>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector3D>
#include <functional>
#include <memory>

#include "geometry.h"
#include "render/voxel_scene.h"

/// Renderer-facing contract shared by chunk previews and standalone structures.
class VoxelSceneController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasVoxelModel READ hasVoxelModel NOTIFY voxelChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY statusChanged)
    Q_PROPERTY(int voxelProgress READ voxelProgress NOTIFY voxelProgressChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)
    Q_PROPERTY(bool blockVolumeEditingEnabled READ blockVolumeEditingEnabled CONSTANT)
    Q_PROPERTY(bool deleting READ deleting NOTIFY deletingChanged)

   public:
    explicit VoxelSceneController(QObject* parent = nullptr);
    [[nodiscard]] bool exporting() const { return exporting_; }
    Q_INVOKABLE bool exportMcstructure(const QUrl& url, const QVector3D& minimum, const QVector3D& maximum, bool exportEntities,
                                       bool useNewFormat);
    Q_INVOKABLE bool exportGlb(const QUrl& url, const QVector3D& minimum, const QVector3D& maximum);
    Q_INVOKABLE virtual bool deleteBlockVolumeAsync(const QVector3D& minimum, const QVector3D& maximum, bool deleteEntities);
    [[nodiscard]] virtual bool loading() const = 0;
    [[nodiscard]] virtual QString errorMessage() const = 0;
    [[nodiscard]] virtual int voxelProgress() const = 0;
    [[nodiscard]] virtual bool blockVolumeEditingEnabled() const { return false; }
    [[nodiscard]] virtual bool deleting() const { return false; }
    [[nodiscard]] virtual std::shared_ptr<const VoxelSceneData> voxelScene() const = 0;
    [[nodiscard]] virtual std::shared_ptr<const VoxelSceneData> voxelOverlayScene() const { return {}; }
    [[nodiscard]] bool hasVoxelModel() const {
        const auto scene = voxelScene();
        return scene && scene->modelSize.x() > 0 && scene->modelSize.y() > 0 && scene->modelSize.z() > 0;
    }

   signals:
    void loadingChanged();
    void statusChanged();
    void voxelProgressChanged();
    void voxelChanged();
    void overlayChanged();
    void exportingChanged();
    void deletingChanged();
    void deleteFailed(const QString& message);
    void exportFinished(const QUrl& url, const QString& error);

   protected:
    using StructureExportTask = std::function<QByteArray()>;
    [[nodiscard]] virtual StructureExportTask structureExportTask(const bl::block_box& localBounds, bool exportEntities,
                                                                  bool useNewFormat) const = 0;
    // Loader-backed providers drain exports before replacing or destroying their loader.
    void waitForExport();

   private:
    QFutureWatcher<QString> export_watcher_;
    QUrl export_url_;
    bool exporting_{false};
};

#endif  // BEDROCKMAP_VOXELSCENECONTROLLER_H
