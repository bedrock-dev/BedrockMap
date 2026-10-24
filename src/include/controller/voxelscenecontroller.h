#ifndef BEDROCKMAP_VOXELSCENECONTROLLER_H
#define BEDROCKMAP_VOXELSCENECONTROLLER_H

#include <QByteArray>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
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

   public:
    explicit VoxelSceneController(QObject* parent = nullptr);
    [[nodiscard]] bool exporting() const { return exporting_; }
    Q_INVOKABLE bool exportMcstructure(const QString& path, const QVector3D& minimum, const QVector3D& maximum,
                                      bool exportEntities, bool useNewFormat);
    Q_INVOKABLE bool exportGlb(const QString& path, const QVector3D& minimum, const QVector3D& maximum);
    [[nodiscard]] virtual bool loading() const = 0;
    [[nodiscard]] virtual QString errorMessage() const = 0;
    [[nodiscard]] virtual int voxelProgress() const = 0;
    [[nodiscard]] virtual std::shared_ptr<const VoxelSceneData> voxelScene() const = 0;
    [[nodiscard]] bool hasVoxelModel() const {
        const auto scene = voxelScene();
        return scene && scene->modelSize.x() > 0 && scene->modelSize.y() > 0 && scene->modelSize.z() > 0;
    }

   signals:
    void loadingChanged();
    void statusChanged();
    void voxelProgressChanged();
    void voxelChanged();
    void exportingChanged();
    void exportFinished(const QString& path, const QString& error);

   protected:
    using StructureExportTask = std::function<QByteArray()>;
    [[nodiscard]] virtual StructureExportTask structureExportTask(const bl::block_box& localBounds, bool exportEntities,
                                                                 bool useNewFormat) const = 0;
    // Loader-backed providers drain exports before replacing or destroying their loader.
    void waitForExport();

   private:
    QFutureWatcher<QString> export_watcher_;
    QString export_path_;
    bool exporting_{false};
};

#endif  // BEDROCKMAP_VOXELSCENECONTROLLER_H
