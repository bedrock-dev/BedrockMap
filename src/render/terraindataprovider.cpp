#include "render/terraindataprovider.h"

#include <QElapsedTimer>
#include <QSet>
#include <QThread>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <limits>

#include "base/platform.h"
#include "loader/asynclevelloader.h"
#include "render/terraintiles.h"

namespace {
    /// Delay between budgeted passes of a visible-window scan. A scan only requests
    /// bakes and submits tile builds; it never samples pixels on the GUI thread.
    constexpr int poll_interval_ms = 16;
    constexpr qint64 poll_budget_ns = 6'000'000;

    int floorDiv(int value, int divisor) {
        const int quotient = value / divisor;
        return (value % divisor < 0) ? quotient - 1 : quotient;
    }

    QPoint regionIndexAt(QPoint chunkOrigin) {
        return QPoint(floorDiv(chunkOrigin.x(), constant::RW), floorDiv(chunkOrigin.y(), constant::RW));
    }

    bool fullTileRefresh() {
        static const bool enabled = !qEnvironmentVariable("BEDROCKMAP_FULL_TILE_REFRESH").isEmpty();
        return enabled;
    }


}  // namespace

TerrainTileTask::TerrainTileTask(std::shared_ptr<const ChunkRegion> region, region_pos pos, int blocks_per_texel, int layer,
                                 quint64 generation, bool cache_only, bool overview_only)
    : region_(std::move(region)), pos_(pos), blocks_per_texel_(blocks_per_texel), layer_(layer), generation_(generation),
      cache_only_(cache_only), overview_only_(overview_only) {
    setAutoDelete(true);
}

void TerrainTileTask::run() {
    if (!region_) return;
    const auto buildAndEmit = [&](int blocks_per_texel) {
        const auto snapshot = TerrainTiles::buildSnapshot(*region_, pos_, blocks_per_texel, layer_);
        const bool prepareForDelivery = !overview_only_ && blocks_per_texel == 1;
        const QImage preparedColor =
            prepareForDelivery
                ? snapshot.color.scaled(128, 128, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGBA8888)
                : QImage();
        const QImage preparedHeight =
            prepareForDelivery
                ? snapshot.height.scaled(128, 128, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGBA8888)
                : QImage();
        const QImage preparedMaterial =
            prepareForDelivery
                ? snapshot.material.scaled(128, 128, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGBA8888)
                : QImage();
        const QImage overviewColor;
        emit finished(generation_, pos_.x, pos_.z, pos_.dim, blocks_per_texel, layer_,
                      static_cast<qulonglong>(region_->revision), cache_only_, snapshot.color, snapshot.height, snapshot.material,
                      preparedColor, preparedHeight, preparedMaterial, overviewColor);
    };

    buildAndEmit(blocks_per_texel_);
    if (cache_only_) {
        // Background work fills the full pyramid after the initial visible level
        // is already on screen.
        for (int blocks_per_texel = 1; blocks_per_texel <= terrain_render_config::max_blocks_per_texel;
             blocks_per_texel *= 2) {
            if (blocks_per_texel != blocks_per_texel_ &&
                blocks_per_texel != terrain_render_config::overview_blocks_per_texel)
                buildAndEmit(blocks_per_texel);
        }
    }
}

TerrainDataProvider::TerrainDataProvider(QObject* parent) : QObject(parent) {
    overview_pool_.setMaxThreadCount(1);
    connect(&overview_watcher_, &QFutureWatcher<QImage>::finished, this, [this] {
        if (overview_watcher_.isRunning()) return;
        const QRect chunks(viewportRequest_.regions.x() * constant::RW, viewportRequest_.regions.y() * constant::RW,
                           viewportRequest_.regions.width() * constant::RW, viewportRequest_.regions.height() * constant::RW);
        if (active_ && overview_only_ && overviewEnabled() && overview_world_ == world_generation_ &&
            overview_dimension_ == viewportRequest_.dimension && overview_window_.contains(chunks) &&
            overview_revision_ == loader_->chunkCoords().overviewRevision()) {
            overview_delivered_ = true;
            emit overviewReady(overview_watcher_.result(), overview_window_, overview_dimension_);
        }
        if (overview_only_) schedulePoll();
    });
    poll_timer_.setSingleShot(true);
    poll_timer_.setInterval(poll_interval_ms);
    connect(&poll_timer_, &QTimer::timeout, this, &TerrainDataProvider::poll);
    prefetch_timer_.setInterval(16);
    connect(&prefetch_timer_, &QTimer::timeout, this, &TerrainDataProvider::processPrefetchChunk);
    syncSnapshotCacheLimit();
    // Sampling is much lighter than a region bake and should not compete with
    // the loader's LevelDB workers or saturate the machine while zooming out.
    tile_pool_.setMaxThreadCount(std::clamp(QThread::idealThreadCount() / 4, 1, 3));
}

TerrainDataProvider::~TerrainDataProvider() {
    overview_pool_.waitForDone();
    tile_pool_.waitForDone();
}

bool TerrainDataProvider::overviewEnabled() const {
    return loader_ && setting::current().preloadAllChunkCoords && setting::current().chunkOverviewEnabled &&
           loader_->chunkOverviewEnabled();
}

void TerrainDataProvider::buildOverview() {
    if (!overviewEnabled() || !loader_->chunkCoordsReady() || overview_watcher_.isRunning()) return;
    const QRect chunks(viewportRequest_.regions.x() * constant::RW, viewportRequest_.regions.y() * constant::RW,
                       viewportRequest_.regions.width() * constant::RW, viewportRequest_.regions.height() * constant::RW);
    if (overview_delivered_ && overview_world_ == world_generation_ && overview_dimension_ == viewportRequest_.dimension &&
        overview_revision_ == loader_->chunkCoords().overviewRevision() && overview_window_.contains(chunks) &&
        overview_window_.width() <= chunks.width() * 1.5 && overview_window_.height() <= chunks.height() * 1.5) return;
    overview_window_ = chunks.adjusted(-constant::COORDS_REGION_SIZE, -constant::COORDS_REGION_SIZE,
                                      constant::COORDS_REGION_SIZE, constant::COORDS_REGION_SIZE);
    overview_dimension_ = viewportRequest_.dimension;
    overview_world_ = world_generation_;
    overview_revision_ = loader_->chunkCoords().overviewRevision();
    overview_delivered_ = false;
    const auto* index = &loader_->chunkCoords();
    overview_watcher_.setFuture(QtConcurrent::run(&overview_pool_, [index, chunks = overview_window_, dim = overview_dimension_] {
        constexpr int side = terrain_render_config::overview_texture_pixels;
        return index->overviewImage(dim, chunks, QSize(side, side));
    }));
}

QStringList TerrainDataProvider::loaderDebug() const {
    QStringList lines;
    const quint64 snapshot_lookups = snapshot_hits_ + snapshot_misses_;
    const double snapshot_rate =
        snapshot_lookups ? 100.0 * static_cast<double>(snapshot_hits_) / static_cast<double>(snapshot_lookups) : 0.0;
    const double average_kib =
        snapshot_inserts_ ? static_cast<double>(snapshot_bytes_) / static_cast<double>(snapshot_inserts_) / 1024.0 : 0.0;
    lines.emplace_back(QStringLiteral("Tile stream:"));
    lines.emplace_back(QStringLiteral(" - snapshot cache: %1 entries, %2/%3 MiB, hit %4/%5 (%6%)")
                           .arg(snapshot_cache_.size())
                           .arg(snapshot_cache_.totalCost() / (1024.0 * 1024.0), 0, 'f', 1)
                           .arg(snapshot_cache_.maxCost() / (1024.0 * 1024.0), 0, 'f', 1)
                           .arg(snapshot_hits_)
                           .arg(snapshot_lookups)
                           .arg(snapshot_rate, 0, 'f', 1));
    lines.emplace_back(QStringLiteral(" - built tiles: %1, average %2 KiB, stale hits %3, rejected tasks %4")
                           .arg(snapshot_inserts_)
                           .arg(average_kib, 0, 'f', 1)
                           .arg(snapshot_stale_)
                           .arg(stale_tile_tasks_));
    lines.emplace_back(QStringLiteral(" - demand: window %1, pending %2, empty %3, scan queue %4")
                           .arg(window_regions_)
                           .arg(pendingRegions())
                           .arg(empty_regions_)
                           .arg(scan_queue_.size()));
    lines.emplace_back(QStringLiteral(" - delivery: %1 tiles, last scan %2 ms").arg(delivered_tiles_).arg(poll_time_ms_, 0, 'f', 2));
    if (!loader_) return lines;
    lines.emplace_back(QStringLiteral("Loader:"));
    for (const auto& line : loader_->debugInfo()) lines.append(line);
    return lines;
}

double TerrainDataProvider::memoryUsageMiB() const { return platform::memoryUsageMiB(); }

int TerrainDataProvider::regionCacheCount() const { return loader_ ? loader_->regionCacheCount() : 0; }
int TerrainDataProvider::emptyRegionCount() const { return loader_ ? loader_->emptyRegionCount() : 0; }
int TerrainDataProvider::heightMapCacheCount() const { return loader_ ? loader_->heightMapCacheCount() : 0; }
int TerrainDataProvider::pendingBakes() const { return loader_ ? loader_->pendingBakeCount() : 0; }
int TerrainDataProvider::activeBakes() const { return loader_ ? loader_->activeBakeCount() : 0; }

void TerrainDataProvider::setLoader(AsyncLevelLoader* loader) {
    if (loader_ == loader) return;
    overview_pool_.waitForDone();
    overview_delivered_ = false;
    emit overviewReady({}, {}, -1);
    ++world_generation_;
    if (loader_) {
        loader_->setTimings({});
        disconnect(loader_, nullptr, this, nullptr);
    }
    loader_ = loader;
    snapshot_cache_.clear();
    resetDelivery();
    if (!loader_) {
        poll_timer_.stop();
        return;
    }
    loader_->setTimings(timings_);
    loader_->setRenderActive(active_);
    syncLoaderTileRequest();
    connect(loader_, &AsyncLevelLoader::regionTileReady, this, &TerrainDataProvider::onRegionReady, Qt::QueuedConnection);
    connect(loader_, &AsyncLevelLoader::regionSnapshotReady, this, &TerrainDataProvider::onRegionSnapshot, Qt::QueuedConnection);
    connect(
        loader_, &AsyncLevelLoader::chunkCoordsPreloadFinished, this,
        [this] {
            if (overview_only_) {
                // Groups drawn before the index completed may have used an
                // empty placeholder representative. Rebuild them now that a
                // non-empty region can be selected for every group.
                resetDelivery();
                requestCatchUp();
                rebuildFullScan();
                syncLoaderTileRequest();
                refillWindow();
            } else {
                schedulePoll();
            }
        },
        Qt::QueuedConnection);
    schedulePoll();
}

void TerrainDataProvider::setTimings(const std::shared_ptr<RenderTimings>& timings) {
    timings_ = timings;
    if (loader_) loader_->setTimings(timings_);
}

void TerrainDataProvider::setActive(bool active) {
    if (active_ == active) return;
    active_ = active;
    if (!active_) {
        poll_timer_.stop();
        setTimings({});
        tile_pool_.clear();
        ++view_generation_;
        pending_bakes_.clear();
        in_flight_.clear();
        catch_up_ = false;
        if (loader_) {
            loader_->setRenderActive(false);
            syncLoaderTileRequest();
        }
        return;
    }
    if (!loader_) return;
    loader_->setRenderActive(!overview_only_);
    syncLoaderTileRequest();
    resetDelivery();
    requestCatchUp();
    rebuildFullScan();
    schedulePoll();
}

void TerrainDataProvider::schedulePoll(int delay_ms) {
    if (!active_ || !loader_ || !loader_->isOpen() || !viewportRequest_.regions.isValid()) {
        poll_timer_.stop();
        return;
    }
    if (!poll_timer_.isActive()) poll_timer_.start(delay_ms);
}

void TerrainDataProvider::resetDelivery() {
    ++view_generation_;
    poll_timer_.stop();
    tile_pool_.clear();
    delivered_.clear();
    pending_bakes_.clear();
    in_flight_.clear();
    delivered_tiles_ = 0;
    empty_regions_ = 0;
    resetScan();
}

void TerrainDataProvider::setViewport(const TerrainViewportRequest& request) {
    syncSnapshotCacheLimit();
    const TerrainViewportRequest old = viewportRequest_;
    if (old == request) {
        if (catch_up_) schedulePoll();
        return;
    }
    viewportRequest_ = request;
    window_regions_ = static_cast<int>(std::min<qint64>(std::numeric_limits<int>::max(),
                                      qint64(request.regions.width()) * request.regions.height()));
    if (overview_only_) {
        resetScan();
        schedulePoll();
        return;
    }

    if (!old.canReuseWith(request)) {
        resetDelivery();
        requestCatchUp();
        rebuildFullScan();
    } else {
        // Keep the unresolved overlap and append only regions introduced by
        // the new edges.
        std::deque<region_pos> retained;
        for (const auto& pos : scan_queue_) {
            if (regionInWindow(pos)) retained.push_back(pos);
        }
        scan_queue_ = std::move(retained);
        scan_complete_ = false;

        std::unordered_set<TerrainTileKey, TerrainTileKeyHash> retained_bakes;
        for (const auto& key : pending_bakes_) {
            const region_pos pos{key.regionX, key.regionZ, key.dimension};
            if (regionInWindow(pos) && key.blocksPerTexel == blocks_per_texel_ && key.layer == viewportRequest_.layer) {
                retained_bakes.insert(key);
            }
        }
        pending_bakes_ = std::move(retained_bakes);

        const QRect& oldRegions = old.regions;
        const QRect& newRegions = request.regions;
        if (oldRegions.left() > newRegions.left())
            appendScanRange(newRegions.left(), newRegions.top(), std::min(newRegions.right(), oldRegions.left() - 1), newRegions.bottom());
        if (oldRegions.right() < newRegions.right())
            appendScanRange(std::max(newRegions.left(), oldRegions.right() + 1), newRegions.top(), newRegions.right(), newRegions.bottom());
        const int overlapLeft = std::max(newRegions.left(), oldRegions.left());
        const int overlapRight = std::min(newRegions.right(), oldRegions.right());
        if (oldRegions.top() > newRegions.top())
            appendScanRange(overlapLeft, newRegions.top(), overlapRight, std::min(newRegions.bottom(), oldRegions.top() - 1));
        if (oldRegions.bottom() < newRegions.bottom())
            appendScanRange(overlapLeft, std::max(newRegions.top(), oldRegions.bottom() + 1), overlapRight, newRegions.bottom());

        for (auto it = delivered_.begin(); it != delivered_.end();) {
            const auto& key = it->first;
            const bool keep = key.dimension == viewportRequest_.dimension && key.blocksPerTexel == blocks_per_texel_ &&
                              key.layer == viewportRequest_.layer && regionInWindow({key.regionX, key.regionZ, key.dimension});
            it = keep ? std::next(it) : delivered_.erase(it);
        }
    }
    // Keep the retained overlap and the freshly appended edges radiating from
    // the new viewport centre.
    sortScanQueueByCenter();
    // Overlap changes keep the existing scan but still require a fresh
    // completion notification for the new viewport.
    requestCatchUp();
    syncLoaderTileRequest();
    if (loader_ && active_ && request.regions.isValid()) {
        loader_->setRenderViewport({request.regions.left() * constant::RW, request.regions.top() * constant::RW, request.dimension},
                                   {request.regions.right() * constant::RW, request.regions.bottom() * constant::RW, request.dimension});
    }
    schedulePoll();
}

void TerrainDataProvider::setOverviewMode(bool overview_only, int detail_level) {
    overview_only = overview_only && overviewEnabled();
    requested_detail_level_ = std::max(1, detail_level);
    const int targetLevel =
        overview_only ? terrain_render_config::overview_blocks_per_texel : requested_detail_level_;
    if (overview_only_ == overview_only && blocks_per_texel_ == targetLevel) return;
    overview_only_ = overview_only;
    overview_delivered_ = false;
    blocks_per_texel_ = targetLevel;
    resetDelivery();
    requestCatchUp();
    rebuildFullScan();
    syncLoaderTileRequest();
    if (loader_ && active_) loader_->setRenderActive(!overview_only_);
    refillWindow();
}

void TerrainDataProvider::refillWindow() {
    requestCatchUp();
    if (active_) poll();
}

void TerrainDataProvider::setBlocksPerTexel(int blocks_per_texel) {
    blocks_per_texel = std::max(1, blocks_per_texel);
    requested_detail_level_ = blocks_per_texel;
    if (overview_only_) return;
    if (blocks_per_texel_ != blocks_per_texel) blocks_per_texel_ = blocks_per_texel;
    resetDelivery();
    requestCatchUp();
    rebuildFullScan();
    syncLoaderTileRequest();
    refillWindow();
}

void TerrainDataProvider::setLayer(int layer) {
    if (viewportRequest_.layer == layer) return;
    viewportRequest_.layer = layer;
    resetDelivery();
    requestCatchUp();
    rebuildFullScan();
    syncLoaderTileRequest();
    refillWindow();
}

void TerrainDataProvider::prefetchCoarserLevels(int max_levels) {
    if (overview_only_ || max_levels <= 0 || !active_ || !loader_ || !loader_->isOpen() || !viewportRequest_.regions.isValid()) return;
    if (prefetch_timer_.isActive()) return;
    prefetch_regions_ = viewportRequest_.regions;
    prefetch_region_x_ = prefetch_regions_.left();
    prefetch_region_z_ = prefetch_regions_.top();
    prefetch_levels_ = max_levels;
    prefetch_timer_.start();
}

void TerrainDataProvider::processPrefetchChunk() {
    if (overview_only_ || !active_ || !loader_ || !loader_->isOpen() || !prefetch_regions_.isValid()) {
        prefetch_timer_.stop();
        return;
    }
    syncSnapshotCacheLimit();
    constexpr int max_visits_per_chunk = 32;
    const int layer = viewportRequest_.layer;
    int visits = 0;
    while (prefetch_region_z_ <= prefetch_regions_.bottom() && visits < max_visits_per_chunk) {
        const region_pos pos{prefetch_region_x_ * constant::RW, prefetch_region_z_ * constant::RW, viewportRequest_.dimension};
        int firstMissingLevel = 0;
        for (int step = 1; step <= prefetch_levels_; ++step) {
            const int level = std::min(terrain_render_config::max_blocks_per_texel, blocks_per_texel_ << step);
            if (level == blocks_per_texel_) continue;
            const TerrainTileKey key{pos.x, pos.z, pos.dim, level, layer};
            if (!snapshot_cache_.contains(key) && in_flight_.find(key) == in_flight_.end()) {
                firstMissingLevel = level;
                break;
            }
        }
        ++visits;
        if (firstMissingLevel) {
            const ChunkRegion* data = nullptr;
            if (loader_->regionState(pos, &data) == AsyncLevelLoader::RegionState::Ready && data) {
                auto lease = loader_->regionLease(pos);
                if (lease) {
                    const TerrainTileKey firstKey{pos.x, pos.z, pos.dim, firstMissingLevel, layer};
                    in_flight_.insert(firstKey);
                    auto* task =
                        new TerrainTileTask(std::move(lease), pos, firstMissingLevel, layer, world_generation_, true, false);
                    connect(task, &TerrainTileTask::finished, this, &TerrainDataProvider::onTileFinished, Qt::QueuedConnection);
                    tile_pool_.start(task);
                }
            }
        }
        if (++prefetch_region_x_ > prefetch_regions_.right()) {
            prefetch_region_x_ = prefetch_regions_.left();
            ++prefetch_region_z_;
        }
    }
    if (prefetch_region_z_ > prefetch_regions_.bottom()) prefetch_timer_.stop();
}

QVector<ActorSprite> TerrainDataProvider::actorSprites(int min_chunk_x, int min_chunk_z, int max_chunk_x, int max_chunk_z, int dimension,
                                                       int render_style, int max_sprites) {
    QVector<ActorSprite> result;
    if (!active_ || !loader_ || !loader_->isOpen() || max_chunk_x < min_chunk_x || max_chunk_z < min_chunk_z || max_sprites <= 0)
        return result;
    const int region_x0 = floorDiv(min_chunk_x, constant::RW) * constant::RW;
    const int region_z0 = floorDiv(min_chunk_z, constant::RW) * constant::RW;
    const int region_x1 = floorDiv(max_chunk_x, constant::RW) * constant::RW;
    const int region_z1 = floorDiv(max_chunk_z, constant::RW) * constant::RW;
    for (int z = region_z0; z <= region_z1; z += constant::RW) {
        for (int x = region_x0; x <= region_x1; x += constant::RW) {
            const region_pos pos{x, z, dimension};
            if (render_style == 0) {
                for (const auto& [icon, positions] : loader_->getActorList(pos)) {
                    if (!icon) continue;
                    for (const auto& actor : positions) {
                        result.append(ActorSprite{icon, actor.x, actor.z, 1.0f});
                        if (result.size() >= max_sprites) return result;
                    }
                }
            } else {
                for (const auto& [chunk, icons] : loader_->getActorCountList(pos)) {
                    Q_UNUSED(chunk);
                    for (const auto& [icon, info] : icons) {
                        if (!icon || info.count <= 0) continue;
                        result.append(ActorSprite{icon, info.pos.x, info.pos.z, static_cast<float>(std::log2(info.count + 1))});
                        if (result.size() >= max_sprites) return result;
                    }
                }
            }
        }
    }
    return result;
}

QVector<OverlayRect> TerrainDataProvider::hsaRects(int min_chunk_x, int min_chunk_z, int max_chunk_x, int max_chunk_z, int dimension) {
    QVector<OverlayRect> result;
    if (overview_only_) return result;
    if (!active_ || !loader_ || !loader_->isOpen() || max_chunk_x < min_chunk_x || max_chunk_z < min_chunk_z) return result;
    const auto colorFor = [](bl::HSAType type) -> QColor {
        switch (type) {
            case bl::NetherFortress:
                return QColor(0, 223, 162, 255);
            case bl::SwampHut:
                return QColor(255, 0, 96, 255);
            case bl::OceanMonument:
                return QColor(246, 250, 112, 255);
            case bl::PillagerOutpost:
                return QColor(0, 121, 255, 255);
            case bl::Unknown:
                return QColor(0, 0, 0, 0);
        }
        return QColor(0, 0, 0, 0);
    };
    const int region_x0 = floorDiv(min_chunk_x, constant::RW) * constant::RW;
    const int region_z0 = floorDiv(min_chunk_z, constant::RW) * constant::RW;
    const int region_x1 = floorDiv(max_chunk_x, constant::RW) * constant::RW;
    const int region_z1 = floorDiv(max_chunk_z, constant::RW) * constant::RW;
    for (int z = region_z0; z <= region_z1; z += constant::RW) {
        for (int x = region_x0; x <= region_x1; x += constant::RW) {
            for (const auto& hsa : loader_->getHSAs(region_pos{x, z, dimension})) {
                QColor outline = colorFor(hsa.type);
                if (outline.alpha() == 0) continue;
                QColor fill = outline;
                fill.setAlpha(100);
                result.push_back(OverlayRect{static_cast<float>(hsa.min_pos.x), static_cast<float>(hsa.min_pos.z),
                                             static_cast<float>(hsa.max_pos.x + 1), static_cast<float>(hsa.max_pos.z + 1), dimension,
                                             outline, fill});
            }
        }
    }
    return result;
}

QImage TerrainDataProvider::slimeChunkImage(const region_pos& pos) {
    if (!active_ || !loader_ || !loader_->isOpen() || pos.dim != 0) return {};
    return loader_->bakedSlimeChunkImage(pos);
}

void TerrainDataProvider::clear() {
    overview_delivered_ = false;
    resetDelivery();
    snapshot_cache_.clear();
    catch_up_ = false;
    viewportRequest_ = {};
}

void TerrainDataProvider::invalidateRegions(const QRegion& chunkRegion, int dimension) {
    if (chunkRegion.isEmpty()) return;
    const QList<TerrainTileKey> keys = snapshot_cache_.keys();
    for (const auto& key : keys) {
        if (key.dimension != dimension) continue;
        const QRect regionRect(key.regionX, key.regionZ, constant::RW, constant::RW);
        if (chunkRegion.intersects(regionRect)) snapshot_cache_.remove(key);
    }

    // Sparse scans omit regions with no remaining chunks. Clear their patches
    // explicitly so cached GPU groups cannot retain terrain after deletion.
    if (loader_ && loader_->chunkCoordsReady()) {
        QSet<QPoint> cleared;
        for (const QRect& rect : chunkRegion) {
            for (int z = floorDiv(rect.top(), constant::RW); z <= floorDiv(rect.bottom(), constant::RW); ++z) {
                for (int x = floorDiv(rect.left(), constant::RW); x <= floorDiv(rect.right(), constant::RW); ++x) {
                    const QPoint origin(x * constant::RW, z * constant::RW);
                    if (cleared.contains(origin) ||
                        loader_->chunkCoords().containsAnyChunk(origin.x(), origin.y(), dimension, constant::RW)) continue;
                    cleared.insert(origin);
                    for (int bp = 1; bp <= terrain_render_config::max_blocks_per_texel; bp *= 2)
                        emit regionEmpty(origin.x(), origin.y(), dimension, bp, viewportRequest_.layer);
                }
            }
        }
    }

    resetDelivery();
    requestCatchUp();
    rebuildFullScan();
    syncLoaderTileRequest();
    schedulePoll();
}

bool TerrainDataProvider::regionInWindow(const region_pos& pos) const {
    return pos.dim == viewportRequest_.dimension && viewportRequest_.regions.contains(regionIndexAt(QPoint(pos.x, pos.z)));
}

TerrainTileKey TerrainDataProvider::currentKey(const region_pos& pos) const {
    return TerrainTileKey{pos.x, pos.z, pos.dim, blocks_per_texel_, viewportRequest_.layer};
}

void TerrainDataProvider::appendScanRange(int minRegionX, int minRegionZ, int maxRegionX, int maxRegionZ) {
    const QRect requested(minRegionX, minRegionZ, maxRegionX - minRegionX + 1, maxRegionZ - minRegionZ + 1);
    const QRect clipped = requested.intersected(viewportRequest_.regions);
    if (!clipped.isValid()) return;

    if (loader_ && loader_->chunkCoordsReady()) {
        for (const auto& pos : loader_->chunkCoords().terrainRegions(viewportRequest_.dimension, clipped))
            scan_queue_.push_back(pos);
        return;
    }
    // Without the index, expand large windows incrementally in poll().
    if (qint64(clipped.width()) * clipped.height() > terrain_render_config::max_requested_regions) {
        scan_ranges_.push_back(clipped);
        return;
    }
    for (int regionZ = clipped.top(); regionZ <= clipped.bottom(); ++regionZ) {
        for (int regionX = clipped.left(); regionX <= clipped.right(); ++regionX) {
            scan_queue_.push_back(region_pos{regionX * constant::RW, regionZ * constant::RW, viewportRequest_.dimension});
        }
    }
}

void TerrainDataProvider::sortScanQueueByCenter() {
    if (scan_queue_.size() < 2 || !viewportRequest_.regions.isValid()) return;
    const QRect& regions = viewportRequest_.regions;
    const double centerX = regions.left() + regions.width() / 2.0;
    const double centerZ = regions.top() + regions.height() / 2.0;
    const auto distanceSq = [&](const region_pos& pos) {
        const double dx = floorDiv(pos.x, constant::RW) - centerX;
        const double dz = floorDiv(pos.z, constant::RW) - centerZ;
        return dx * dx + dz * dz;
    };
    std::sort(scan_queue_.begin(), scan_queue_.end(),
              [&](const region_pos& a, const region_pos& b) { return distanceSq(a) < distanceSq(b); });
}

void TerrainDataProvider::rebuildFullScan() {
    scan_queue_.clear();
    scan_ranges_.clear();
    scan_empty_ = 0;
    scan_complete_ = false;
    if (overview_only_) return;
    if (!viewportRequest_.regions.isValid()) {
        window_regions_ = 0;
        return;
    }
    window_regions_ = static_cast<int>(std::min<qint64>(std::numeric_limits<int>::max(),
                                      qint64(viewportRequest_.regions.width()) * viewportRequest_.regions.height()));
    appendScanRange(viewportRequest_.regions.left(), viewportRequest_.regions.top(), viewportRequest_.regions.right(),
                    viewportRequest_.regions.bottom());
    sortScanQueueByCenter();
}

void TerrainDataProvider::rebuildPendingScan() {
    scan_ranges_.clear();
    scan_queue_.clear();
    scan_empty_ = 0;
    scan_complete_ = false;
    for (const auto& key : pending_bakes_) {
        if (key.blocksPerTexel == blocks_per_texel_ && key.layer == viewportRequest_.layer) {
            scan_queue_.push_back(region_pos{key.regionX, key.regionZ, key.dimension});
        }
    }
    sortScanQueueByCenter();
}

void TerrainDataProvider::syncLoaderTileRequest() {
    if (loader_) {
        loader_->setPendingTileRequest(TerrainTileRequest{blocks_per_texel_, viewportRequest_.layer, active_ && !overview_only_});
    }
}

void TerrainDataProvider::syncSnapshotCacheLimit() {
    const quint64 revision = setting::revision();
    if (snapshot_cache_limit_initialized_ && snapshot_cache_limit_revision_ == revision) return;
    const int megabytes = std::clamp(setting::current().gpuTileSnapshotCacheSizeMB, 8, 512);
    snapshot_cache_.setMaxCost(static_cast<qsizetype>(megabytes) * 1024 * 1024);
    snapshot_cache_limit_revision_ = revision;
    snapshot_cache_limit_initialized_ = true;
}

void TerrainDataProvider::storeSnapshot(const TerrainTileKey& key, quint64 sourceRevision, const QImage& color, const QImage& height,
                                        const QImage& material, const QImage& preparedColor, const QImage& preparedHeight,
                                        const QImage& preparedMaterial, const QImage& overviewColor) {
    if (!sourceRevision || color.isNull() || height.isNull() || material.isNull()) return;
    const qint64 bytes =
        static_cast<qint64>(color.sizeInBytes()) + static_cast<qint64>(height.sizeInBytes()) + static_cast<qint64>(material.sizeInBytes());
    ++snapshot_inserts_;
    snapshot_bytes_ += static_cast<quint64>(std::max<qint64>(bytes, 0));
    snapshot_cache_.insert(key,
                           new CachedTile{sourceRevision, color, height, material, preparedColor, preparedHeight, preparedMaterial,
                                          overviewColor},
                           static_cast<int>(std::min<qint64>(bytes, std::numeric_limits<int>::max())));
}

void TerrainDataProvider::deliverSnapshot(const TerrainTileKey& key, quint64 sourceRevision, const QImage& color, const QImage& height,
                                          const QImage& material, const QImage& preparedColor, const QImage& preparedHeight,
                                          const QImage& preparedMaterial, const QImage& overviewColor) {
    const auto existing = delivered_.find(key);
    if (!fullTileRefresh() && existing != delivered_.end() && !existing->second.empty &&
        existing->second.sourceRevision == sourceRevision) {
        return;
    }
    delivered_[key] = DeliveredInfo{sourceRevision, false};
    ++delivered_tiles_;
    const RenderTimingScope scope(timings_.get(), QStringLiteral("Tile delivery"));
    emit tileReady(key.regionX, key.regionZ, key.dimension, key.blocksPerTexel, key.layer, color, height, material, preparedColor,
                   preparedHeight, preparedMaterial, overviewColor);
}

bool TerrainDataProvider::enqueueTile(const region_pos& pos) {
    if (!active_ || !loader_ || !regionInWindow(pos)) return false;
    const ChunkRegion* data = nullptr;
    if (loader_->regionState(pos, &data) != AsyncLevelLoader::RegionState::Ready || !data) return false;
    auto lease = loader_->regionLease(pos);
    if (!lease) return false;

    const auto key = currentKey(pos);
    const auto delivered = delivered_.find(key);
    if (!fullTileRefresh() && delivered != delivered_.end() && !delivered->second.empty &&
        delivered->second.sourceRevision == lease->revision) {
        return false;
    }
    if (in_flight_.find(key) != in_flight_.end()) return false;

    if (auto* cached = snapshot_cache_.object(key); cached && !fullTileRefresh()) {
        if (cached->sourceRevision == lease->revision) {
            ++snapshot_hits_;
            deliverSnapshot(key, lease->revision, cached->color, cached->height, cached->material, cached->preparedColor,
                            cached->preparedHeight, cached->preparedMaterial, cached->overviewColor);
            return false;
        }
        ++snapshot_stale_;
    } else {
        ++snapshot_misses_;
    }

    in_flight_.insert(key);
    auto* task = new TerrainTileTask(std::move(lease), pos, blocks_per_texel_, viewportRequest_.layer, view_generation_, false,
                                     overview_only_);
    connect(task, &TerrainTileTask::finished, this, &TerrainDataProvider::onTileFinished, Qt::QueuedConnection);
    tile_pool_.start(task);
    return true;
}

void TerrainDataProvider::onRegionReady(const region_pos& pos) {
    if (overview_only_ || !active_ || !regionInWindow(pos)) return;
    const auto key = currentKey(pos);
    pending_bakes_.erase(key);
    if (enqueueTile(pos)) {
        emit metricsChanged();
    }
}

void TerrainDataProvider::onRegionSnapshot(const region_pos& pos, int blocksPerTexel, int layer, const QImage& color, const QImage& height,
                                           const QImage& material) {
    if (!loader_ || color.isNull() || height.isNull() || material.isNull()) return;
    auto lease = loader_->regionLease(pos);
    if (!lease) return;
    const TerrainTileKey key{pos.x, pos.z, pos.dim, blocksPerTexel, layer};
    const quint64 sourceRevision = lease->revision;
    const QImage preparedColor = color;
    const QImage preparedHeight = height;
    const QImage preparedMaterial = material;
    const QImage overviewColor;
    storeSnapshot(key, sourceRevision, color, height, material, preparedColor, preparedHeight, preparedMaterial, overviewColor);
    if (!active_ || blocksPerTexel != blocks_per_texel_ || layer != viewportRequest_.layer || !regionInWindow(pos)) return;
    deliverSnapshot(key, sourceRevision, color, height, material, preparedColor, preparedHeight, preparedMaterial, overviewColor);
    maybeCompleteCatchUp();
}

void TerrainDataProvider::onTileFinished(quint64 generation, int regionX, int regionZ, int dimension, int blocksPerTexel, int layer,
                                         qulonglong sourceToken, bool cacheOnly, const QImage& color, const QImage& height,
                                         const QImage& material, const QImage& preparedColor, const QImage& preparedHeight,
                                         const QImage& preparedMaterial, const QImage& overviewColor) {
    const TerrainTileKey key{regionX, regionZ, dimension, blocksPerTexel, layer};
    in_flight_.erase(key);
    if (!active_) return;
    if (cacheOnly ? generation != world_generation_ : generation != view_generation_) return;
    auto lease = loader_ ? loader_->regionLease({regionX, regionZ, dimension}) : std::shared_ptr<const ChunkRegion>{};
    if (!lease || lease->revision != sourceToken || color.isNull() || height.isNull() || material.isNull()) {
        if (lease && lease->revision != sourceToken) ++stale_tile_tasks_;
        maybeCompleteCatchUp();
        return;
    }
    storeSnapshot(key, lease->revision, color, height, material, preparedColor, preparedHeight, preparedMaterial, overviewColor);
    if (blocksPerTexel == terrain_render_config::overview_blocks_per_texel && layer == viewportRequest_.layer &&
        regionInWindow({regionX, regionZ, dimension})) {
        deliverSnapshot(key, lease->revision, color, height, material, preparedColor, preparedHeight, preparedMaterial,
                        overviewColor);
    }
    if (cacheOnly) {
        const region_pos pos{regionX, regionZ, dimension};
        if (blocksPerTexel == blocks_per_texel_ && layer == viewportRequest_.layer && regionInWindow(pos)) {
            if (enqueueTile(pos)) emit metricsChanged();
        }
        maybeCompleteCatchUp();
        return;
    }
    if (blocksPerTexel != blocks_per_texel_ || layer != viewportRequest_.layer || !regionInWindow({regionX, regionZ, dimension})) {
        maybeCompleteCatchUp();
        return;
    }
    deliverSnapshot(key, lease->revision, color, height, material, preparedColor, preparedHeight, preparedMaterial, overviewColor);
    maybeCompleteCatchUp();
    if (!metrics_clock_.isValid() || metrics_clock_.elapsed() >= 250) {
        metrics_clock_.start();
        emit metricsChanged();
    }
}

void TerrainDataProvider::maybeCompleteCatchUp() {
    if (!catch_up_ || !scan_complete_ || !pending_bakes_.empty() || hasRelevantInFlight()) return;
    catch_up_ = false;
    emit windowRefilled(blocks_per_texel_, catch_up_generation_, viewportRequest_.regions, viewportRequest_.dimension,
                        viewportRequest_.layer);
}

bool TerrainDataProvider::hasRelevantInFlight() const {
    // Tasks started for an earlier, larger viewport still finish and store their
    // snapshots, but they must not hold back the current window's catch-up. A
    // fast zoom-out can leave thousands of those tasks behind, which used to keep
    // the renderer on its old LOD for several seconds after the camera had already
    // moved back in.
    for (const auto& key : in_flight_) {
        if (key.dimension == viewportRequest_.dimension && key.blocksPerTexel == blocks_per_texel_ && key.layer == viewportRequest_.layer &&
            regionInWindow({key.regionX, key.regionZ, key.dimension}))
            return true;
    }
    return false;
}

void TerrainDataProvider::processRegion(const region_pos& pos, int& submitted) {
    if (!regionInWindow(pos)) return;
    const RenderTimingScope regionScope(timings_.get(), QStringLiteral("Region"));
    const ChunkRegion* data = nullptr;
    const auto state = [&] {
        const RenderTimingScope scope(timings_.get(), QStringLiteral("Lookup"));
        return loader_->regionState(pos, &data);
    }();
    const auto key = currentKey(pos);
    if (state == AsyncLevelLoader::RegionState::Empty) {
        pending_bakes_.erase(key);
        if (regionInWindow(pos)) ++scan_empty_;
        auto delivered = delivered_.find(key);
        if (delivered == delivered_.end() || !delivered->second.empty) {
            delivered_[key] = DeliveredInfo{0, true};
            emit regionEmpty(pos.x, pos.z, pos.dim, key.blocksPerTexel, key.layer);
        }
        return;
    }
    if (state == AsyncLevelLoader::RegionState::Ready) {
        pending_bakes_.erase(key);
        if (enqueueTile(pos)) {
            ++submitted;
        }
        return;
    }
    pending_bakes_.insert(key);
}

void TerrainDataProvider::poll() {
    poll_timer_.stop();
    syncSnapshotCacheLimit();
    if (!active_ || !loader_ || !loader_->isOpen() || !viewportRequest_.regions.isValid()) return;
    if (overview_only_) {
        buildOverview();
        if (!loader_->chunkCoordsReady()) schedulePoll(50);
        return;
    }

    const RenderTimingScope timing(timings_.get(), QStringLiteral("GUI terrain poll"));
    QElapsedTimer scan_timer;
    scan_timer.start();
    const bool unlimited = fullTileRefresh();
    const qint64 budget_ns = unlimited ? std::numeric_limits<qint64>::max() : poll_budget_ns;

    {
        const RenderTimingScope scope(timings_.get(), QStringLiteral("Viewport scheduler"));
        loader_->setRenderViewport(
            {viewportRequest_.regions.left() * constant::RW, viewportRequest_.regions.top() * constant::RW, viewportRequest_.dimension},
            {viewportRequest_.regions.right() * constant::RW, viewportRequest_.regions.bottom() * constant::RW,
             viewportRequest_.dimension});
    }

    if (scan_complete_ && !pending_bakes_.empty() && scan_queue_.empty()) rebuildPendingScan();

    int submitted = 0;
    int visited = 0;
    RenderTimingScope walk(timings_.get(), QStringLiteral("Visible regions"));
    while (!scan_queue_.empty() || !scan_ranges_.empty()) {
        if (visited && (submitted >= terrain_render_config::max_tiles_per_poll || scan_timer.nsecsElapsed() > budget_ns)) {
            break;
        }
        if (scan_queue_.empty()) {
            auto& range = scan_ranges_.front();
            const int row = range.top();
            const int end = std::min(range.right(), range.left() + 255);
            for (int x = range.left(); x <= end; ++x)
                scan_queue_.push_back({x * constant::RW, row * constant::RW, viewportRequest_.dimension});
            if (end < range.right()) {
                const QRect rest(end + 1, row, range.right() - end, 1);
                range.setTop(row + 1);
                scan_ranges_.push_front(rest);
            } else {
                range.setTop(row + 1);
            }
            while (!scan_ranges_.empty() && scan_ranges_.front().isEmpty()) scan_ranges_.pop_front();
        }
        ++visited;
        const auto pos = scan_queue_.front();
        scan_queue_.pop_front();
        processRegion(pos, submitted);
    }
    // A pass stopped by the per-poll tile cap is cheap work that only yielded to
    // keep the event loop moving. Resume almost immediately instead of waiting a
    // full frame: a large LOD catch-up has thousands of small coarse tiles, and
    // the old unconditional 16 ms gap turned that into seconds of frozen atlas
    // preparation. A pass stopped by the time budget still gets the full delay.
    const bool time_budget_used = scan_timer.nsecsElapsed() > budget_ns;

    if (scan_queue_.empty() && scan_ranges_.empty()) {
        scan_complete_ = true;
        empty_regions_ = scan_empty_;
    }
    walk.next(QStringLiteral("Notifications"));
    poll_time_ms_ = scan_timer.nsecsElapsed() / 1.0e6;

    maybeCompleteCatchUp();
    if (!metrics_clock_.isValid() || metrics_clock_.elapsed() >= 250) {
        metrics_clock_.start();
        emit metricsChanged();
    }

    if (fullTileRefresh()) {
        rebuildFullScan();
        schedulePoll(poll_interval_ms);
    } else if (!scan_complete_) {
        schedulePoll(time_budget_used ? poll_interval_ms : 1);
    } else if (!pending_bakes_.empty() || !in_flight_.empty()) {
        // Completion signals normally wake us first. This slow pass only
        // recovers from a missed or dropped notification.
        schedulePoll(250);
    }
}
