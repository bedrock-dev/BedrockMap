#ifndef BEDROCKMAP_MAPCONTROLLER_H
#define BEDROCKMAP_MAPCONTROLLER_H

#include <QByteArray>
#include <QFutureWatcher>
#include <QObject>
#include <QPointF>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <functional>

#include "controller/regionimportcontroller.h"
#include "model/mapviewstate.h"

class AsyncLevelLoader;

/// Commands and observable state for one map view.
///
/// This class contains no dialog or rendering code. QML controls and map items
/// use it as the map's command and state interface.
class MapController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int dimension READ dimension NOTIFY dimensionChanged)
    Q_PROPERTY(int layer READ layer NOTIFY stateChanged)
    Q_PROPERTY(bool transparentVoid READ transparentVoid WRITE setTransparentVoid NOTIFY transparentVoidChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(int selectionMode READ selectionMode WRITE setSelectionMode NOTIFY selectionModeChanged)
    Q_PROPERTY(int selectionChunkCount READ selectionChunkCount NOTIFY selectionChanged)
    Q_PROPERTY(int selectionRectCount READ selectionRectCount NOTIFY selectionChanged)
    Q_PROPERTY(int modifiedChunkCount READ modifiedChunkCount NOTIFY modificationStateChanged)
    Q_PROPERTY(int deletedChunkCount READ deletedChunkCount NOTIFY modificationStateChanged)
    Q_PROPERTY(bool operationRunning READ operationRunning NOTIFY operationChanged)
    Q_PROPERTY(bool worldLoaded READ worldLoaded NOTIFY worldStateChanged)
    Q_PROPERTY(RegionImportController* regionImport READ regionImport CONSTANT)

   public:
    explicit MapController(MapViewState* map_view_state, QObject* parent = nullptr);
    ~MapController() override;

    [[nodiscard]] int dimension() const;
    /// MapRenderOptions::LayerType the map is showing (0 = terrain, 1 = biomes).
    [[nodiscard]] int layer() const;
    [[nodiscard]] bool transparentVoid() const;
    [[nodiscard]] MapCameraState cameraState() const;
    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] int selectionMode() const;
    [[nodiscard]] int selectionChunkCount() const;
    [[nodiscard]] int selectionRectCount() const;
    [[nodiscard]] int modifiedChunkCount() const;
    [[nodiscard]] int deletedChunkCount() const;
    [[nodiscard]] bool selectionDragging() const;
    [[nodiscard]] QVector<QRect> selectionRects() const;
    [[nodiscard]] QRect selectionDragRect() const;
    [[nodiscard]] bool operationRunning() const { return operation_running_; }
    [[nodiscard]] Q_INVOKABLE bool selectionContainsChunkAt(const QPointF& view_position) const;
    [[nodiscard]] Q_INVOKABLE QVariantMap blockInfoAt(const QPointF& view_position) const;
    [[nodiscard]] Q_INVOKABLE QVariantMap voxelViewArea() const;
    [[nodiscard]] bool worldLoaded() const;
    [[nodiscard]] RegionImportController* regionImport() { return &region_import_; }
    Q_INVOKABLE bool beginImportAsync(const QUrl& source, int chunkX, int chunkZ);
    Q_INVOKABLE bool beginPasteAsync(int chunkX, int chunkZ);
    Q_INVOKABLE bool beginImportDragAt(const QPointF& position);
    Q_INVOKABLE void updateImportDragAt(const QPointF& position);
    Q_INVOKABLE void endImportDrag();
    Q_INVOKABLE bool confirmImport();
    Q_INVOKABLE void cancelImport();
    void setEditingBlocked(bool blocked) { editing_blocked_ = blocked; }

    Q_INVOKABLE void setViewportSize(const QSize& size);
    Q_INVOKABLE void setDimension(int dimension);
    Q_INVOKABLE void setScale(qreal scale, const QPointF& anchor = {});
    Q_INVOKABLE void zoomBy(qreal factor, const QPointF& anchor = {});
    Q_INVOKABLE void transformGesture(qreal factor, const QPointF& previous, const QPointF& position);
    Q_INVOKABLE void zoomAdjacent(int direction, const QPointF& anchor = {});
    Q_INVOKABLE void pan(qreal dx, qreal dy);
    Q_INVOKABLE void gotoBlock(int x, int z);
    Q_INVOKABLE void setLayer(int layer);
    Q_INVOKABLE void setTransparentVoid(bool enabled);
    Q_INVOKABLE void setOther(int other, bool enabled);
    Q_INVOKABLE bool toggleOther(int other);
    Q_INVOKABLE void setSelectionMode(int mode);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void beginSelectionDragAt(const QPointF& view_position);
    Q_INVOKABLE void updateSelectionDragAt(const QPointF& view_position);
    Q_INVOKABLE void finishSelectionDrag();
    Q_INVOKABLE void cancelSelectionDrag();
    Q_INVOKABLE bool deleteSelectionAsync();
    Q_INVOKABLE bool createVoidSelectionAsync();
    Q_INVOKABLE bool setSelectionBiomeAsync(int biome);
    Q_INVOKABLE bool copySelectionAsync();
    Q_INVOKABLE bool exportSelectionAsync(const QUrl& destination);
    [[nodiscard]] Q_INVOKABLE QVariantList biomeOptions() const;
    [[nodiscard]] Q_INVOKABLE QVariantMap mapFilterOptions() const;
    [[nodiscard]] Q_INVOKABLE QVariantMap defaultMapFilterOptions() const;
    Q_INVOKABLE bool applyMapFilterOptions(const QVariantMap& options);
    void waitForOperation();
    Q_INVOKABLE void copyText(const QString& text) const;
    void setLevelLoader(AsyncLevelLoader* loader);
    /// Publish externally edited chunks to the map and terrain providers.
    void refreshRegions(const QRegion& region, int dimension);

   signals:
    void dimensionChanged();
    void transparentVoidChanged();
    /// High-frequency camera-only notification used by renderers during drag.
    /// It avoids reevaluating every QML property binding on each mouse move.
    void cameraMoved();
    void stateChanged();
    void selectionChanged();
    void selectionModeChanged();
    void modificationStateChanged();
    void operationChanged();
    void operationFinished();
    void operationFailed(const QString& message);
    void mapDataChanged(const QRegion& region, int dimension);
    void mapFilterChanged();
    void worldStateChanged();

   private:
    [[nodiscard]] MapViewState* mapViewState() const;
    /// Re-apply the configured zoom limits for the current loader state.
    void applyZoomLimits();

    struct OperationResult {
        bool success{false};
        bool refresh_terrain{false};
        QString error;
        QByteArray clipboard;
        bool imported{false};
    };
    using SelectionOperation = std::function<OperationResult(AsyncLevelLoader&, const QRegion&, int)>;
    bool startSelectionOperation(SelectionOperation operation);
    bool startRegionOperation(SelectionOperation operation, const QRegion& region, int dimension);
    void finishSelectionOperation();

    MapViewState* map_view_state_{nullptr};
    RegionImportController region_import_;
    AsyncLevelLoader* loader_{nullptr};
    QFutureWatcher<OperationResult> operation_watcher_;
    bool operation_running_{false};
    QRegion operation_region_;
    int operation_dimension_{0};
    bool editing_blocked_{false};
};

#endif  // BEDROCKMAP_MAPCONTROLLER_H
