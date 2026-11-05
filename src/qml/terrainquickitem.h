#ifndef BEDROCKMAP_TERRAINQUICKITEM_H
#define BEDROCKMAP_TERRAINQUICKITEM_H

#include <QtQml/qqmlregistration.h>

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QRect>
#include <QSet>
#include <QTimer>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

#include "controller/mapcontroller.h"
#include "maprenderstate.h"
#include "render/gpurenderoptions.h"
#include "render/rendertimings.h"
#include "render/terraindataprovider.h"
#include "render/terraintiles.h"
#include "terrainmaterial.h"

class QSGGeometryNode;
class QSGNode;
class TerrainAtlasTexture;
class TerrainArrayMaterial;
class QQuickWindow;

struct TerrainRenderSettingsSnapshot {
    GpuRenderOptions gpu;
    int cpuShadowLevel{150};
    bool highDetailMap{false};
    /// Requested tile-array slices; the render thread clamps to the device limit.
    int tileLayers{terrain_render_config::tile_array_layers};
};

/// Immutable GUI-to-render view state. The render thread consumes one instance
/// for the complete frame, so camera, viewport and LOD cannot mix across input
/// updates.
struct TerrainFrameState {
    MapCameraState camera;
    TerrainViewportRequest request;
    std::shared_ptr<const TerrainRenderSettingsSnapshot> settings;
    int requestedBlocksPerTexel{1};
    bool overviewOnly{false};
    bool transparentVoid{false};
};

struct TerrainWorldTileKey {
    int lod{1};
    int x{0};
    int z{0};
    int dimension{0};
    int layer{0};
    bool operator==(const TerrainWorldTileKey& other) const {
        return lod == other.lod && x == other.x && z == other.z && dimension == other.dimension && layer == other.layer;
    }
};
struct TerrainWorldTileReady {
    TerrainWorldTileKey key;
    QImage color;
    QImage surface;
    QPoint destination;
    bool empty{false};
};

struct TerrainFrameMetrics {
    int terrainSlots{0};
    int loadingSlots{0};
    int emptySlots{0};
    int blocksPerTexel{1};
    int atlasSlotsPerSide{0};
    int uploadedTiles{0};
};

inline std::size_t qHash(const TerrainWorldTileKey& key, std::size_t seed = 0) noexcept {
    std::size_t value = static_cast<std::size_t>(static_cast<std::uint32_t>(key.lod));
    value = value * 1315423911u + static_cast<std::uint32_t>(key.x);
    value = value * 1315423911u + static_cast<std::uint32_t>(key.z);
    value = value * 1315423911u + static_cast<std::uint32_t>(key.dimension);
    value = value * 1315423911u + static_cast<std::uint32_t>(key.layer);
    return value ^ seed;
}

/// Scene-graph objects reset together. Qt owns the material and textures through
/// the geometry node, so these pointers must remain non-owning.
struct ArrayGpuResources {
    QSGGeometryNode* node{nullptr};
    TerrainArrayMaterial* material{nullptr};
    TerrainAtlasTexture* color{nullptr};
    TerrainAtlasTexture* surface{nullptr};
    TerrainAtlasTexture* pageTable{nullptr};
    TerrainAtlasTexture* overview{nullptr};
    QImage biomePalette;

    void resetPointers() {
        node = nullptr;
        material = nullptr;
        color = nullptr;
        surface = nullptr;
        pageTable = nullptr;
        overview = nullptr;
    }
};

/// Render-thread tile residency and page-table bookkeeping.
struct ArrayTilePool {
    QRect overviewChunks;
    qint64 overviewCacheKey{0};
    QHash<TerrainWorldTileKey, int> tileLayers;
    QHash<int, quint64> layerLastUsed;
    QHash<int, quint64> retiringLayers;
    QSet<TerrainWorldTileKey> uploadedTiles;
    QSet<TerrainWorldTileKey> terrainTiles;
    QVector<int> freeLayers;
    std::array<QImage, terrain_render_config::page_table_levels> pageTableImages;
    /// Slices last queued for upload; a rebuilt slice is uploaded only if it differs.
    std::array<QImage, terrain_render_config::page_table_levels> uploadedPageTables;
    /// Set when residency changes; the page table is rebuilt only then or when
    /// the visible region window moves.
    bool pageTableDirty{true};
    QRect pageTableWindow;
    int activeLod{1};
    int activeDimension{-1};
    int activeLayer{-1};
    /// Slices in the current color/surface arrays.
    int layerCount{0};
    quint64 frameCounter{0};
    QSizeF geometrySize;
    TerrainArrayUniformBody lastUniforms;
    bool hasLastUniforms{false};
    /// Shared transparent image that clears a reused packed layer.
    QImage clearLayerImage;
    bool layersExhaustedLogged{false};

    void clearResidency() {
        tileLayers.clear();
        layerLastUsed.clear();
        retiringLayers.clear();
        uploadedTiles.clear();
        terrainTiles.clear();
        freeLayers.clear();
        pageTableDirty = true;
    }

    void initializeFreeLayers(int layer_count) {
        freeLayers.clear();
        freeLayers.reserve(layer_count);
        for (int layer = 0; layer < layer_count; ++layer) freeLayers.push_back(layer);
    }

    void resetOverview() {
        overviewChunks = {};
        overviewCacheKey = 0;
    }

    void resetForRendererLoss(int layer_count) {
        resetOverview();
        clearResidency();
        initializeFreeLayers(layer_count);
        activeDimension = -1;
        activeLayer = -1;
        // The new page-table texture has undefined contents; upload every slice.
        uploadedPageTables.fill({});
        pageTableWindow = {};
        geometrySize = {};
        hasLastUniforms = false;
    }

    void resetForViewChange(int dimension, int layer, int layer_count) {
        resetOverview();
        clearResidency();
        initializeFreeLayers(layer_count);
        activeDimension = dimension;
        activeLayer = layer;
    }
};

struct TerrainOverviewSnapshot {
    QImage image;
    QRect chunks;
    int dimension{-1};
};

/// GUI-to-render hand-off. All fields are protected by the same mutex.
struct TileDeliveryQueue {
    mutable QMutex mutex;
    QVector<TerrainWorldTileReady> readyTiles;
    QImage overview;
    QRect overviewChunks;
    int overviewDimension{-1};

    void clear() {
        const QMutexLocker lock(&mutex);
        readyTiles.clear();
        overview = {};
        overviewChunks = {};
        overviewDimension = -1;
    }

    void pushTile(TerrainWorldTileReady tile) {
        const QMutexLocker lock(&mutex);
        readyTiles.append(std::move(tile));
    }

    [[nodiscard]] QVector<TerrainWorldTileReady> takeTiles() {
        const QMutexLocker lock(&mutex);
        QVector<TerrainWorldTileReady> result;
        result.swap(readyTiles);
        return result;
    }

    void restoreTiles(QVector<TerrainWorldTileReady> tiles) {
        const QMutexLocker lock(&mutex);
        tiles += readyTiles;
        readyTiles = std::move(tiles);
    }

    void setOverview(QImage image, const QRect& chunks, int dimension) {
        const QMutexLocker lock(&mutex);
        overview = std::move(image);
        overviewChunks = chunks;
        overviewDimension = dimension;
    }

    [[nodiscard]] TerrainOverviewSnapshot overviewSnapshot() const {
        const QMutexLocker lock(&mutex);
        return {overview, overviewChunks, overviewDimension};
    }
};

struct FrameStateChannel {
    mutable QMutex mutex;
    std::shared_ptr<const TerrainFrameState> state;

    void publish(std::shared_ptr<const TerrainFrameState> next) {
        const QMutexLocker lock(&mutex);
        state = std::move(next);
    }

    [[nodiscard]] std::shared_ptr<const TerrainFrameState> take() const {
        const QMutexLocker lock(&mutex);
        return state;
    }
};

/// Scene Graph terrain layer backed by renderer-neutral surface snapshots.
class TerrainQuickItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MapController* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(TerrainDataProvider* provider READ provider WRITE setProvider NOTIFY providerChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY hasDataChanged)
    Q_PROPERTY(QColor checkerColor READ checkerColor WRITE setCheckerColor NOTIFY checkerColorChanged)
    Q_PROPERTY(QColor checkerAlternateColor READ checkerAlternateColor WRITE setCheckerAlternateColor NOTIFY checkerAlternateColorChanged)
    Q_PROPERTY(bool metricsEnabled READ metricsEnabled WRITE setMetricsEnabled NOTIFY metricsEnabledChanged)
    Q_PROPERTY(QString timingText READ timingText NOTIFY metricsChanged)
    Q_PROPERTY(QString graphicsInfo READ graphicsInfo NOTIFY metricsChanged)
    Q_PROPERTY(double framesPerSecond READ framesPerSecond NOTIFY metricsChanged)
    Q_PROPERTY(int terrainSlotCount READ terrainSlotCount NOTIFY metricsChanged)
    Q_PROPERTY(int loadingSlotCount READ loadingSlotCount NOTIFY metricsChanged)
    Q_PROPERTY(int emptySlotCount READ emptySlotCount NOTIFY metricsChanged)
    Q_PROPERTY(int blocksPerTexel READ blocksPerTexel NOTIFY metricsChanged)
    /// Tiles uploaded into the texture arrays on the last painted frame.
    Q_PROPERTY(int uploadedTiles READ uploadedTiles NOTIFY metricsChanged)
    /// Slots along one atlas edge at the current level, uploads since the world
    /// opened, and the tiles/uploads still waiting for the GPU.
    Q_PROPERTY(int atlasSlotsPerSide READ atlasSlotsPerSide NOTIFY metricsChanged)

   public:
    explicit TerrainQuickItem(QQuickItem* parent = nullptr);
    [[nodiscard]] QString timingText() const { return timing_text_; }
    [[nodiscard]] QString graphicsInfo() const { return timings_->graphicsInfo(); }
    [[nodiscard]] double framesPerSecond() const { return frames_per_second_; }
    Q_INVOKABLE void resetTimings() { timings_->reset(); }
    Q_INVOKABLE void copyDebugText(const QString& text) const;
    Q_INVOKABLE void setDragActive(bool active);
    [[nodiscard]] MapController* controller() const { return controller_; }
    [[nodiscard]] TerrainDataProvider* provider() const { return provider_; }
    [[nodiscard]] bool hasData() const { return has_data_; }
    [[nodiscard]] QColor checkerColor() const { return checker_color_; }
    [[nodiscard]] QColor checkerAlternateColor() const { return checker_alternate_color_; }
    void setCheckerColor(const QColor& color);
    void setCheckerAlternateColor(const QColor& color);
    [[nodiscard]] std::shared_ptr<MapRenderState> renderState() const { return render_state_; }
    [[nodiscard]] bool metricsEnabled() const { return metrics_enabled_.load(std::memory_order_relaxed); }
    [[nodiscard]] int terrainSlotCount() const { return display_metrics_.terrainSlots; }
    [[nodiscard]] int loadingSlotCount() const { return display_metrics_.loadingSlots; }
    [[nodiscard]] int emptySlotCount() const { return display_metrics_.emptySlots; }
    /// World blocks one atlas texel covers, i.e. the current atlas level.
    [[nodiscard]] int blocksPerTexel() const { return display_metrics_.blocksPerTexel; }
    [[nodiscard]] int uploadedTiles() const { return display_metrics_.uploadedTiles; }
    [[nodiscard]] int atlasSlotsPerSide() const { return display_metrics_.atlasSlotsPerSide; }
    void setMetricsEnabled(bool enabled);
    void setController(MapController* controller);
    void setProvider(TerrainDataProvider* provider);

   signals:
    void controllerChanged();
    void providerChanged();
    void hasDataChanged();
    void checkerColorChanged();
    void checkerAlternateColorChanged();
    void metricsEnabledChanged();
    void metricsChanged();

   protected:
    void componentComplete() override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

   private:
    void syncViewState();
    void publishFrameState();
    QSGNode* updateArrayPaintNode(QSGNode* oldNode, const MapCameraState& camera, int requestedBlocksPerTexel, bool overviewOnly,
                                  bool transparentVoid, const std::shared_ptr<const TerrainRenderSettingsSnapshot>& render_settings);
    void queueArrayRegionPiece(int regionX, int regionZ, int dimension, int blocksPerTexel, int layer, const QImage& color,
                               const QImage& surface, bool empty);
    void connectFrameTimings(QQuickWindow* window);
    std::shared_ptr<RenderTimings> timings_{std::make_shared<RenderTimings>()};
    std::shared_ptr<MapRenderState> render_state_{std::make_shared<MapRenderState>()};
    QString timing_text_;
    double frames_per_second_{0.0};
    QList<QMetaObject::Connection> frame_connections_;
    QPointer<QQuickWindow> input_window_;
    /// Pushes the visible window and the layer to the provider (GUI thread).
    void publishViewport();

    /// Shadow ray reach in blocks; 0 when shadows are off.
    [[nodiscard]] static int shadowReachBlocks(const GpuRenderOptions& gpu);
    /// Repaint this item and the window, again on the next event-loop pass for
    /// a scene graph that is still being created.
    void requestWindowRepaint();
    /// Atlas level that covers the current viewport: the smallest level whose atlas spans
    /// the view, with hysteresis so a zoom across a band does not rebuild it.
    [[nodiscard]] int blocksPerTexelFor() const;
    [[nodiscard]] bool detailedTileMode() const;
    /// Coalesce camera-driven LOD changes until the target has stayed stable.
    void scheduleLodRequest();
    void commitLodRequest(int level);
    void initializeRenderSettings();
    void refreshMetrics();
    void publishMetrics(const TerrainFrameMetrics& metrics);

    QPointer<MapController> controller_;
    QColor checker_color_{20, 20, 20};
    QColor checker_alternate_color_{40, 40, 40};
    QPointer<TerrainDataProvider> provider_;
    MapCameraState camera_;
    /// Last published demand, with its window expressed in region indices.
    TerrainViewportRequest request_;
    FrameStateChannel frame_state_channel_;
    ArrayGpuResources array_gpu_;
    ArrayTilePool array_tile_pool_;
    TileDeliveryQueue tile_delivery_;
    std::shared_ptr<const TerrainRenderSettingsSnapshot> render_settings_;
    /// Blocks per texel the provider has been asked to re-sample the window at.
    /// Publication uses the latest layout-level refill snapshot; edge slots may fill later.
    std::atomic<int> requested_blocks_per_texel_{1};
    int candidate_blocks_per_texel_{1};
    QTimer lod_settle_timer_;
    /// Incremented whenever the renderer is attached to another world. The
    /// scene-graph thread consumes it once, before sampling anything, so state
    /// owned by the previous session can never reach the new world's atlas.
    std::atomic<quint64> render_reset_generation_{0};
    quint64 applied_render_reset_generation_{0};
    /// Slices the render thread created after clamping to the device limit;
    /// bounds the level of detail the GUI thread may request.
    std::atomic<int> created_tile_layers_{0};

    bool has_data_{false};
    QTimer metrics_timer_;
    QTimer input_probe_timer_;
    QElapsedTimer input_probe_clock_;
    std::atomic_bool metrics_enabled_{false};
    std::atomic<int> frame_uploaded_tiles_{0};
    std::atomic<int> frame_upload_lod_{1};
    mutable QMutex metrics_mutex_;
    TerrainFrameMetrics published_metrics_;
    TerrainFrameMetrics display_metrics_;
};

#endif
