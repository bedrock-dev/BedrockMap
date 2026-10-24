#ifndef BEDROCKMAP_MCSTRUCTURECONTROLLER_H
#define BEDROCKMAP_MCSTRUCTURECONTROLLER_H

#include <QFutureWatcher>
#include "controller/nbtdocumentcontroller.h"
#include "controller/voxelscenecontroller.h"

struct McstructureLoadResult;
namespace bl { class mcstructure; }

/// Loads a standalone structure without tying the renderer to a world session.
class McstructureController final : public VoxelSceneController {
    Q_OBJECT
    Q_PROPERTY(NbtDocumentController* document READ document CONSTANT)
public:
    explicit McstructureController(QObject* parent = nullptr);
    void load(const QString& path);
    [[nodiscard]] NbtDocumentController* document() const { return document_; }
    [[nodiscard]] bool loading() const override { return loading_; }
    [[nodiscard]] QString errorMessage() const override { return error_; }
    [[nodiscard]] int voxelProgress() const override { return scene_ ? 100 : 0; }
    [[nodiscard]] std::shared_ptr<const VoxelSceneData> voxelScene() const override { return scene_; }
signals:
    void loadFailed(const QString& message);
protected:
    [[nodiscard]] StructureExportTask structureExportTask(const bl::block_box& localBounds, bool exportEntities,
                                                         bool useNewFormat) const override;
private:
    std::shared_ptr<const bl::mcstructure> structure_;
    NbtDocumentController* document_;
    QFutureWatcher<std::shared_ptr<McstructureLoadResult>> watcher_;
    std::shared_ptr<const VoxelSceneData> scene_;
    QString error_;
    QString title_;
    bool loading_{false};
};
#endif  // BEDROCKMAP_MCSTRUCTURECONTROLLER_H
