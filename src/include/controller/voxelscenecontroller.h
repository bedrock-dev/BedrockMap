#ifndef BEDROCKMAP_VOXELSCENECONTROLLER_H
#define BEDROCKMAP_VOXELSCENECONTROLLER_H

#include <QObject>
#include <QString>
#include <memory>

#include "render/voxel_scene.h"

/// Renderer-facing contract shared by chunk previews and block volumes.
class VoxelSceneController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasVoxelModel READ hasVoxelModel NOTIFY voxelChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY statusChanged)
    Q_PROPERTY(int loadProgress READ loadProgress NOTIFY loadProgressChanged)

   public:
    explicit VoxelSceneController(QObject* parent = nullptr) : QObject(parent) {}
    [[nodiscard]] virtual bool loading() const = 0;
    [[nodiscard]] virtual QString errorMessage() const = 0;
    [[nodiscard]] virtual int loadProgress() const = 0;
    [[nodiscard]] virtual std::shared_ptr<const VoxelSceneData> voxelScene() const = 0;
    [[nodiscard]] virtual std::shared_ptr<const VoxelSceneData> voxelOverlayScene() const { return {}; }
    [[nodiscard]] bool hasVoxelModel() const {
        const auto scene = voxelScene();
        return scene && scene->modelSize.x() > 0 && scene->modelSize.y() > 0 && scene->modelSize.z() > 0;
    }

   signals:
    void loadingChanged();
    void statusChanged();
    void loadProgressChanged();
    void voxelChanged();
    void overlayChanged();
};

#endif  // BEDROCKMAP_VOXELSCENECONTROLLER_H
