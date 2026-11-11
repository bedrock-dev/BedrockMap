#ifndef BEDROCKMAP_TERRAINDATAPROVIDER_H
#define BEDROCKMAP_TERRAINDATAPROVIDER_H

#include <QCache>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QRegion>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QVector>
#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "loader/chunkregion.h"
#include "render/overlay.h"
#include "render/rendertimings.h"
#include "render/terraintiles.h"

class AsyncLevelLoader;

/// CPU-only worker used by the QML tile stream. It deliberately lives with the
/// stream instead of forming another public rendering layer.
class TerrainTileTask final : public QObject, public QRunnable {
    Q_OBJECT

   public:
    /// Builds `blocks_per_texel`, then every coarser level through
    /// `last_blocks_per_texel` (prefetch only).
    TerrainTileTask(std::shared_ptr<const ChunkRegion> region, region_pos pos, int blocks_per_texel, int layer, quint64 generation,
                    bool cache_only, int last_blocks_per_texel = 0);
    void run() override;

   signals:
    void finished(quint64 generation, int regionX, int regionZ, int dimension, int blocks_per_texel, int layer, qulonglong sourceToken,
                  bool cache_only, const QImage& color, const QImage& surface, bool hasChunks);

   private:
    std::shared_ptr<const ChunkRegion> region_;
    region_pos pos_;
    int blocks_per_texel_;
    int last_blocks_per_texel_;
    int layer_;
    quint64 generation_;
    bool cache_only_;
};

struct TerrainTileKey {
    int regionX{0};
    int regionZ{0};
    int dimension{0};
    int blocksPerTexel{1};
    int layer{0};

    [[nodiscard]] bool operator==(const TerrainTileKey& other) const {
        return regionX == other.regionX && regionZ == other.regionZ && dimension == other.dimension &&
               blocksPerTexel == other.blocksPerTexel && layer == other.layer;
    }
};

struct TerrainTileKeyHash {
    [[nodiscard]] std::size_t operator()(const TerrainTileKey& key) const noexcept {
        std::size_t value = static_cast<std::size_t>(static_cast<std::uint32_t>(key.regionX));
        value = value * 1315423911u + static_cast<std::uint32_t>(key.regionZ);
        value = value * 1315423911u + static_cast<std::uint32_t>(key.dimension);
        value = value * 1315423911u + static_cast<std::uint32_t>(key.blocksPerTexel);
        value = value * 1315423911u + static_cast<std::uint32_t>(key.layer);
        return value;
    }
};

inline std::size_t qHash(const TerrainTileKey& key, std::size_t seed = 0) noexcept { return TerrainTileKeyHash{}(key) ^ seed; }

/// Hands finished atlas tiles from the shared core to a renderer.
///
/// QML's event-driven terrain tile stream. AsyncLevelLoader owns LevelDB and
/// canonical region data; this object turns viewport changes into tile-build
/// requests and validates their results on the GUI thread. Tile pixel sampling
/// runs in a small worker pool, never in the GUI or scene-graph thread.
class TerrainDataProvider final : public QObject {
    Q_OBJECT
    /// Renderer stats: drawable regions, delivered tiles and window scan cost.
    Q_PROPERTY(bool chunkCoordsEnabled READ chunkCoordsEnabled NOTIFY chunkCoordsBoundsChanged)
    Q_PROPERTY(int windowRegions READ windowRegions NOTIFY metricsChanged)
    Q_PROPERTY(int pendingRegions READ pendingRegions NOTIFY metricsChanged)
    Q_PROPERTY(int emptyRegions READ emptyRegions NOTIFY metricsChanged)
    Q_PROPERTY(int deliveredTiles READ deliveredTiles NOTIFY metricsChanged)
    Q_PROPERTY(int cachedTiles READ cachedTiles NOTIFY metricsChanged)
    Q_PROPERTY(double pollTimeMs READ pollTimeMs NOTIFY metricsChanged)
    Q_PROPERTY(int residentRegions READ residentRegions NOTIFY metricsChanged)
    /// Occupancy of the shared core: region tiles, empty markers and the bakes
    /// the scheduler has queued or running.
    Q_PROPERTY(int regionCacheCount READ regionCacheCount NOTIFY metricsChanged)
    Q_PROPERTY(int emptyRegionCount READ emptyRegionCount NOTIFY metricsChanged)
    Q_PROPERTY(int pendingBakes READ pendingBakes NOTIFY metricsChanged)
    Q_PROPERTY(int activeBakes READ activeBakes NOTIFY metricsChanged)
    /// Multi-line loader report: cache occupancy, thread pool, chunk edits and
    /// region load/render timings.
    Q_PROPERTY(QStringList loaderDebug READ loaderDebug NOTIFY metricsChanged)
    Q_PROPERTY(double memoryUsageMiB READ memoryUsageMiB NOTIFY metricsChanged)

   public:
    explicit TerrainDataProvider(QObject* parent);
    ~TerrainDataProvider() override;
    [[nodiscard]] bool overviewEnabled() const;
    void setTimings(const std::shared_ptr<RenderTimings>& timings);
    void setLoader(AsyncLevelLoader* loader);
    /// Visible sessions poll on demand. Hidden sessions keep their caches and
    /// view state, but stop sampling the loader and drop queued render work.
    void setActive(bool active);
    [[nodiscard]] bool isActive() const { return active_; }
    [[nodiscard]] AsyncLevelLoader* loader() const { return loader_; }
    [[nodiscard]] bool chunkCoordsEnabled() const;
    /// Inclusive chunk-space bounds for the current dimension, or an invalid QRect.
    [[nodiscard]] QRect chunkCoordsBounds(int dimension) const;

    /// Visible region window in chunk coordinates plus the layer to sample.
    /// Forwarded to the loader so the scheduler prioritises visible bakes.
    void setViewport(const TerrainViewportRequest& request);
    /// Switch between detailed LOD delivery and aggregated overview delivery, and
    /// set the detailed atlas level. Changing either re-delivers every region.
    void setOverviewMode(bool overview_only, int detail_level);
    /// Resend every tile of the current window, e.g. after the renderer
    /// recreated its tile arrays. Snapshot caches are kept.
    void redeliver();
    /// Drop all cached tiles and refill the current window after source data
    /// has been regenerated, such as after changing the map filter.
    void reload();
    /// Layer to sample (MapRenderOptions::Terrain / Biome). Changing it re-delivers,
    /// because the biome layer uses a different bake.
    void setLayer(int layer);
    /// Warm snapshots for the next one or two coarser LODs. Cache-only tasks do
    /// not alter delivery/catch-up state, but become immediately deliverable if
    /// the viewport switches to that level while they are running.
    void prefetchCoarserLevels(int max_levels = 2);
    [[nodiscard]] int blocksPerTexel() const { return blocks_per_texel_; }
    /// Actors and consolidated actor markers inside a chunk-space window. The
    /// caller owns drawing; this only snapshots the already-cached region data.
    [[nodiscard]] QVector<ActorSprite> actorSprites(int min_chunk_x, int min_chunk_z, int max_chunk_x, int max_chunk_z, int dimension,
                                                    int render_style, int max_sprites = 20000);
    [[nodiscard]] QVector<OverlayRect> hsaRects(int min_chunk_x, int min_chunk_z, int max_chunk_x, int max_chunk_z, int dimension);
    [[nodiscard]] QImage slimeChunkImage(const region_pos& pos);
    Q_INVOKABLE void clear();
    /// Drop cached tiles whose regions intersect an edited chunk region.
    void invalidateRegions(const QRegion& chunkRegion, int dimension);

    [[nodiscard]] int windowRegions() const { return window_regions_; }
    /// Window regions whose bake is not ready yet.
    [[nodiscard]] int pendingRegions() const { return static_cast<int>(pending_bakes_.size() + in_flight_.size()); }
    [[nodiscard]] int emptyRegions() const { return empty_regions_; }
    [[nodiscard]] int deliveredTiles() const { return delivered_tiles_; }
    [[nodiscard]] int cachedTiles() const { return snapshot_cache_.size(); }
    [[nodiscard]] double pollTimeMs() const { return poll_time_ms_; }
    [[nodiscard]] int residentRegions() const { return static_cast<int>(delivered_.size()); }
    [[nodiscard]] int regionCacheCount() const;
    [[nodiscard]] int emptyRegionCount() const;
    [[nodiscard]] int pendingBakes() const;
    [[nodiscard]] int activeBakes() const;
    [[nodiscard]] QStringList loaderDebug() const;
    [[nodiscard]] double memoryUsageMiB() const;

   signals:
    void overviewReady(const QImage& image, const QRect& chunkWindow, int dimension);
    void metricsChanged();
    /// One finished atlas tile for a region, already sampled at
    /// `blocks_per_texel`, so the render thread only has to upload it. The level
    /// travels with the tile: a re-sample for a new zoom level is handed over
    /// while the renderer is still drawing the previous one.
    void tileReady(int regionX, int regionZ, int dimension, int blocks_per_texel, int layer, const QImage& color, const QImage& surface,
                   bool hasChunks);
    /// A catch-up pass finished: everything the loader had ready for the window
    /// has been handed over at `blocks_per_texel`. The renderer switches levels on
    /// its next frame, so it draws that frame from tiles it already holds instead
    /// of from placeholders.
    void windowRefilled(int blocks_per_texel, quint64 catch_up_generation, const QRect& regions, int dimension, int layer);
    /// The region has no chunks. The renderer distinguishes this from a region
    /// that is still loading.
    void regionEmpty(int regionX, int regionZ, int dimension, int blocks_per_texel, int layer);
    /// The chunk-coordinate index became ready or its bounds may have changed.
    void chunkCoordsBoundsChanged();

   private:
    void buildOverview();
    QThreadPool overview_pool_;
    QFutureWatcher<QImage> overview_watcher_;
    QRect overview_window_;
    int overview_dimension_{0};
    quint64 overview_world_{0};
    quint64 overview_revision_{0};
    bool overview_delivered_{false};
    void poll();
    void processPrefetchChunk();
    void schedulePoll(int delay_ms = 0);
    void resetScan() {
        scan_queue_.clear();
        scan_ranges_.clear();
        scan_empty_ = 0;
        scan_complete_ = false;
    }
    /// Order the scan queue nearest-the-viewport-centre first, so already-baked
    /// regions are sampled and uploaded as an outward-radiating fill rather than a
    /// top-to-bottom sweep. Matches RegionRenderScheduler's bake priority so the
    /// whole pipeline fills in the same order.
    void sortScanQueueByCenter();
    void resetDelivery();
    void rebuildFullScan();
    void rebuildPendingScan();
    void appendScanRange(int minX, int minZ, int maxX, int maxZ);
    void processRegion(const region_pos& pos, int& submitted);
    void syncLoaderTileRequest();
    void storeSnapshot(const TerrainTileKey& key, quint64 sourceRevision, const QImage& color, const QImage& surface, bool hasChunks);
    void deliverSnapshot(const TerrainTileKey& key, quint64 sourceRevision, const QImage& color, const QImage& surface, bool hasChunks);
    [[nodiscard]] bool enqueueTile(const region_pos& pos);
    void onRegionReady(const region_pos& pos);
    void onRegionSnapshot(const region_pos& pos, int blocksPerTexel, int layer, const QImage& color, const QImage& surface,
                          bool hasChunks);
    void onTileFinished(quint64 generation, int regionX, int regionZ, int dimension, int blocksPerTexel, int layer, qulonglong sourceToken,
                        bool cache_only, const QImage& color, const QImage& surface, bool hasChunks);
    void maybeCompleteCatchUp();
    [[nodiscard]] bool hasRelevantInFlight() const;
    [[nodiscard]] bool regionInWindow(const region_pos& pos) const;
    [[nodiscard]] TerrainTileKey currentKey(const region_pos& pos) const;
    /// Request completion notification after a whole window has been delivered.
    void requestCatchUp() {
        catch_up_ = true;
        ++catch_up_generation_;
    }
    /// Start a refill now; budgeted scans resume until the window is complete.
    void refillWindow();

    struct DeliveredInfo {
        quint64 sourceRevision{0};
        bool empty{false};
    };
    struct CachedTile {
        quint64 sourceRevision{0};
        QImage color;
        QImage surface;
        bool hasChunks{false};
    };

    AsyncLevelLoader* loader_{nullptr};
    std::shared_ptr<RenderTimings> timings_;
    QTimer poll_timer_;
    QTimer prefetch_timer_;
    QRect prefetch_regions_;
    int prefetch_region_x_{0};
    int prefetch_region_z_{0};
    int prefetch_levels_{0};
    QThreadPool tile_pool_;
    QCache<TerrainTileKey, CachedTile> snapshot_cache_{32 * 1024 * 1024};
    bool active_{false};
    bool overview_only_{false};
    quint64 view_generation_{0};
    quint64 world_generation_{0};
    TerrainViewportRequest viewportRequest_;
    int blocks_per_texel_{1};
    std::unordered_map<TerrainTileKey, DeliveredInfo, TerrainTileKeyHash> delivered_;
    std::unordered_set<TerrainTileKey, TerrainTileKeyHash> pending_bakes_;
    std::unordered_set<TerrainTileKey, TerrainTileKeyHash> in_flight_;
    QElapsedTimer metrics_clock_;
    /// Regions whose demand has not been resolved yet. Panning keeps the
    /// overlap and appends only the new edge strips.
    std::deque<region_pos> scan_queue_;
    std::deque<QRect> scan_ranges_;
    int scan_empty_{0};
    bool scan_complete_{false};
    /// Remains set until an entire scan confirms delivery at this level.
    bool catch_up_{false};
    quint64 catch_up_generation_{0};
    int window_regions_{0};
    int empty_regions_{0};
    int delivered_tiles_{0};
    double poll_time_ms_{0.0};
    quint64 snapshot_hits_{0};
    quint64 snapshot_misses_{0};
    quint64 snapshot_stale_{0};
    quint64 snapshot_inserts_{0};
    quint64 snapshot_bytes_{0};
    quint64 stale_tile_tasks_{0};
};

#endif  // BEDROCKMAP_TERRAINDATAPROVIDER_H
