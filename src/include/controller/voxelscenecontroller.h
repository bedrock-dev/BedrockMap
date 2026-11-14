#ifndef BEDROCKMAP_VOXELSCENECONTROLLER_H
#define BEDROCKMAP_VOXELSCENECONTROLLER_H

#include <QMetaObject>
#include <QObject>
#include <QString>
#include <algorithm>
#include <functional>
#include <memory>

#include "render/voxelmesh.h"

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
    [[nodiscard]] int loadProgress() const { return load_progress_; }
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

   protected:
    void setLoadProgress(int progress) {
        progress = std::clamp(progress, 0, 100);
        if (load_progress_ == progress) return;
        load_progress_ = progress;
        emit loadProgressChanged();
    }

    /// A progress callback safe to call from a worker; updates are queued to this object.
    [[nodiscard]] std::function<void(int)> progressReporter() {
        return [this](int progress) {
            QMetaObject::invokeMethod(this, [this, progress] { setLoadProgress(progress); }, Qt::QueuedConnection);
        };
    }

   private:
    int load_progress_{0};
};

#endif  // BEDROCKMAP_VOXELSCENECONTROLLER_H
