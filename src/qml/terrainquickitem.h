#ifndef BEDROCKMAP_TERRAINQUICKITEM_H
#define BEDROCKMAP_TERRAINQUICKITEM_H

#include <QElapsedTimer>
#include <QColor>
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

#include "controller/mapcontroller.h"
#include "maprenderstate.h"
#include "render/gpu_render_options.h"
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
};

/// Immutable GUI-to-render view state. The render thread consumes one instance
/// for the complete frame, so camera, viewport and LOD cannot mix across input
/// updates.
struct TerrainFrameState {
    MapCameraState camera;
    TerrainViewportRequest request;
    int requestedLevel{1};
    bool overviewOnly{false};
    bool transparentVoid{false};
    quint64 revision{0};
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
    QImage height;
    QImage material;
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

/// Scene Graph terrain layer backed by renderer-neutral height snapshots.
class TerrainQuickItem : public QQuickItem {
    Q_OBJECT
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
    Q_INVOKABLE void applySettings();
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
    QSGNode* updateArrayPaintNode(QSGNode* oldNode, const MapCameraState& camera, int requestedLevel, bool overviewOnly,
                                 bool transparentVoid);
    void queueArrayRegionPiece(int regionX, int regionZ, int dimension, int blocksPerTexel, int layer, const QImage& color,
                               const QImage& height, const QImage& material, const QImage& preparedColor,
                               const QImage& preparedHeight, const QImage& preparedMaterial, const QImage& overviewColor,
                               bool empty, bool makeDetail);
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
    [[nodiscard]] int shadowReachBlocks() const;
    /// Atlas level that covers the current viewport: the smallest level whose atlas spans
    /// the view, with hysteresis so a zoom across a band does not rebuild it.
    [[nodiscard]] int blocksPerTexelFor() const;
    [[nodiscard]] bool detailedTileMode() const;
    /// Coalesce camera-driven LOD changes until the target has stayed stable.
    void scheduleLodRequest();
    void commitLodRequest(int level);
    void refreshRenderSettings();
    [[nodiscard]] std::shared_ptr<const TerrainRenderSettingsSnapshot> renderSettings() const;
    void refreshMetrics();
    void publishMetrics(const TerrainFrameMetrics& metrics);

    QPointer<MapController> controller_;
    QColor checker_color_{20, 20, 20};
    QColor checker_alternate_color_{40, 40, 40};
    QPointer<TerrainDataProvider> provider_;
    MapCameraState camera_;
    /// Last published demand, with its window expressed in region indices.
    TerrainViewportRequest request_;
    /// Immutable snapshot consumed by updatePaintNode(). libc++ shipped with
    /// the Android NDK does not implement atomic<shared_ptr>, so the pointer
    /// swap is protected by a short-lived mutex instead.
    mutable QMutex frame_state_mutex_;
    std::shared_ptr<const TerrainFrameState> frame_state_;
    quint64 frame_state_revision_{0};
    QImage cpu_biome_palette_;
    QSGGeometryNode* array_node_{nullptr};
    TerrainArrayMaterial* array_material_{nullptr};
    TerrainAtlasTexture* array_color_texture_{nullptr};
    TerrainAtlasTexture* array_height_texture_{nullptr};
    TerrainAtlasTexture* array_material_texture_{nullptr};
    TerrainAtlasTexture* array_page_table_texture_{nullptr};
    TerrainAtlasTexture* array_overview_texture_{nullptr};
    QVector<TerrainWorldTileReady> array_ready_tiles_;
    QImage sampled_overview_;
    QRect sampled_overview_chunks_;
    int sampled_overview_dimension_{-1};
    QRect array_overview_chunks_;
    qint64 array_overview_cache_key_{0};
    QHash<TerrainWorldTileKey, int> array_tile_layers_;
    QHash<int, quint64> array_layer_last_used_;
    QHash<int, quint64> array_retiring_layers_;
    QSet<TerrainWorldTileKey> array_uploaded_tiles_;
    QSet<TerrainWorldTileKey> array_terrain_tiles_;
    QVector<int> array_free_layers_;
    std::array<QImage, 8> array_page_table_images_;
    int array_active_lod_{1};
    int array_active_dimension_{-1};
    int array_active_layer_{-1};
    quint64 array_page_table_signature_{0};
    std::array<quint64, 8> array_page_table_layer_signatures_{};
    quint64 array_frame_counter_{0};
    QSizeF array_geometry_size_;
    TerrainArrayUniformBody array_last_uniforms_;
    bool array_has_last_uniforms_{false};
    /// GUI-thread snapshot consumed by the scene-graph thread. Reading
    /// setting::current() from updatePaintNode() would race with Save Settings.
    mutable QMutex render_settings_mutex_;
    std::shared_ptr<const TerrainRenderSettingsSnapshot> render_settings_;
    quint64 render_settings_revision_{0};
    /// Level the provider has been asked to re-sample the window at. Publication
    /// uses the latest layout-level refill snapshot; edge slots may fill later.
    std::atomic<int> requested_level_{1};
    int lod_candidate_level_{1};
    QTimer lod_settle_timer_;
    /// Incremented whenever the renderer is attached to another world. The
    /// scene-graph thread consumes it once, before sampling anything, so state
    /// owned by the previous session can never reach the new world's atlas.
    std::atomic<quint64> render_reset_generation_{0};
    quint64 applied_render_reset_generation_{0};

    mutable QMutex tiles_mutex_;
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
