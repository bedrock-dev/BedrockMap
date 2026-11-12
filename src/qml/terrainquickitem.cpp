#include "terrainquickitem.h"

#include <rhi/qrhi.h>

#include <QClipboard>
#include <QColor>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGNode>
#include <QSGRendererInterface>
#include <QSet>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

#include "base/config.h"
#include "controller/mapcontroller.h"
#include "loguru/loguru.hpp"
#include "render/gpurenderoptions.h"
#include "render/terraindataprovider.h"
#include "render/terraintiles.h"
#include "terrainmaterial.h"

namespace {
    static_assert(terrain_render_config::region_blocks == constant::RW * 16,
                  "region block size must match the configured chunk-region dimensions");

    /// Floor division, so region indices stay correct west/north of the origin.
    int floorDiv(int value, int divisor) {
        const int quotient = value / divisor;
        return (value % divisor < 0) ? quotient - 1 : quotient;
    }

    /// Smallest on-screen texel size, in item pixels, at which the tiles of a
    /// `width` x `height` view plus the protected guard band fit in `layers`
    /// tile-array slices. Each slice covers region_blocks texels per side.
    double texelPixelsForLayerBudget(double width, double height, int layers) {
        // Floor/ceil rounding plus the two-tile guard band on each side.
        constexpr double margin = 6.0;
        // Keep headroom for fallback levels and packed boundary groups.
        const double budget = 0.9 * layers;
        const double a = width / terrain_render_config::region_blocks;
        const double b = height / terrain_render_config::region_blocks;
        const double c = margin * margin - budget;
        if (a <= 0.0 || b <= 0.0) return 0.0;
        if (c >= 0.0) return std::numeric_limits<double>::max();
        // Solve (a * u + margin) * (b * u + margin) = budget for u = 1 / texel size.
        const double qa = a * b;
        const double qb = margin * (a + b);
        const double u = (-qb + std::sqrt(qb * qb - 4.0 * qa * c)) / (2.0 * qa);
        return 1.0 / u;
    }

    double minimumTerrainTexelPixels(bool highDetail, double width, double height, int layers) {
        if (!highDetail) return terrain_render_config::balanced_texel_pixels;
        return std::clamp(texelPixelsForLayerBudget(width, height, layers), terrain_render_config::high_detail_texel_pixels,
                          terrain_render_config::balanced_texel_pixels);
    }

    /// Shrink a region window symmetrically until it fits the level's atlas.
    void clampToRegionBudget(int& x0, int& x1, int& z0, int& z1, qint64 budget) {
        while (static_cast<qint64>(x1 - x0 + 1) * (z1 - z0 + 1) > budget) {
            const bool wide = (x1 - x0) >= (z1 - z0);
            if (wide && x1 > x0) {
                ++x0;
                --x1;
            } else if (!wide && z1 > z0) {
                ++z0;
                --z1;
            } else if (x1 > x0) {
                ++x0;
                --x1;
            } else if (z1 > z0) {
                ++z0;
                --z1;
            } else {
                break;  // a single region is always within budget
            }
        }
    }

}  // namespace

TerrainQuickItem::TerrainQuickItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow* newWindow) {
        connectFrameTimings(metricsEnabled() ? newWindow : nullptr);
        if (newWindow) update();
    });
    lod_settle_timer_.setSingleShot(true);
    lod_settle_timer_.setInterval(120);
    connect(&lod_settle_timer_, &QTimer::timeout, this, [this] {
        if (!provider_) return;
        const int current = requested_blocks_per_texel_.load(std::memory_order_acquire);
        const int wanted = blocksPerTexelFor();
        if (wanted == current) {
            candidate_blocks_per_texel_ = wanted;
            return;
        }
        if (wanted != candidate_blocks_per_texel_) {
            candidate_blocks_per_texel_ = wanted;
            lod_settle_timer_.start();
            return;
        }
        commitLodRequest(wanted);
        // Publish the new level's window and region budget to the provider.
        syncViewState();
    });
    metrics_timer_.setInterval(250);
    connect(&metrics_timer_, &QTimer::timeout, this, &TerrainQuickItem::refreshMetrics);
    initializeRenderSettings();
    input_probe_timer_.setInterval(16);
    input_probe_timer_.setTimerType(Qt::PreciseTimer);
    connect(&input_probe_timer_, &QTimer::timeout, this, [this] {
        const double elapsed = input_probe_clock_.nsecsElapsed() / 1.0e6;
        input_probe_clock_.start();
        timings_->recordGuiTimerSlip(std::max(0.0, elapsed - input_probe_timer_.interval()));
    });
}

void TerrainQuickItem::componentComplete() {
    QQuickItem::componentComplete();
    syncViewState();
    update();
}

void TerrainQuickItem::setDragActive(bool active) {
    const bool enabled = metricsEnabled();
    timings_->setDragActive(enabled && active);
    if (enabled && active) {
        input_probe_clock_.start();
        input_probe_timer_.start();
    } else {
        input_probe_timer_.stop();
    }
}

bool TerrainQuickItem::eventFilter(QObject* watched, QEvent* event) {
    if (metricsEnabled() && watched == input_window_ && event->type() == QEvent::MouseMove) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->buttons() & (Qt::LeftButton | Qt::MiddleButton)) timings_->pointerEventReceived();
    }
    return QQuickItem::eventFilter(watched, event);
}

void TerrainQuickItem::copyDebugText(const QString& text) const {
    if (auto* clipboard = QGuiApplication::clipboard()) clipboard->setText(text);
}

void TerrainQuickItem::connectFrameTimings(QQuickWindow* newWindow) {
    if (input_window_) input_window_->removeEventFilter(this);
    input_window_ = newWindow;
    if (input_window_) input_window_->installEventFilter(this);
    for (const auto& connection : frame_connections_) disconnect(connection);
    frame_connections_.clear();
    if (!newWindow) return;
    // These callbacks run directly on the render thread; no queued GUI delivery
    // is included in their timestamps. Frame interval also includes idle time.
    struct FrameClock {
        std::shared_ptr<RenderTimings> owner;
        QElapsedTimer interval;
        QElapsedTimer frame_elapsed;
        std::unique_ptr<RenderTimingScope> frame;
        std::unique_ptr<RenderTimingScope> stage;
        std::unique_ptr<RenderTimingScope> render_detail;
        bool graphics_reported{false};
    };
    const auto clock = std::make_shared<FrameClock>();
    clock->owner = timings_;
    const auto timings = timings_;
    frame_connections_.append(connect(
        newWindow, &QQuickWindow::beforeFrameBegin, this,
        [clock, timings] {
            clock->render_detail.reset();
            clock->stage.reset();
            clock->frame.reset();
            clock->frame_elapsed.start();
            clock->frame = std::make_unique<RenderTimingScope>(timings.get(), QStringLiteral("Render frame CPU"));
            clock->stage = std::make_unique<RenderTimingScope>(timings.get(), QStringLiteral("Begin / acquire"));
        },
        Qt::DirectConnection));
    frame_connections_.append(connect(
        newWindow, &QQuickWindow::beforeSynchronizing, this,
        [clock, timings] {
            if (clock->stage) clock->stage->next(QStringLiteral("Scene graph sync"));
        },
        Qt::DirectConnection));
    frame_connections_.append(connect(
        newWindow, &QQuickWindow::afterSynchronizing, this,
        [clock, timings] {
            if (clock->stage) clock->stage->next(QStringLiteral("Render preparation"));
        },
        Qt::DirectConnection));
    frame_connections_.append(connect(
        newWindow, &QQuickWindow::beforeRendering, this,
        [clock, timings] {
            if (clock->stage) {
                clock->stage->next(QStringLiteral("Render CPU"));
                clock->render_detail = std::make_unique<RenderTimingScope>(timings.get(), QStringLiteral("Prepare / upload resources"));
            }
        },
        Qt::DirectConnection));
    frame_connections_.append(connect(
        newWindow, &QQuickWindow::beforeRenderPassRecording, this,
        [clock, timings] {
            if (clock->render_detail) clock->render_detail->next(QStringLiteral("Record draw commands"));
        },
        Qt::DirectConnection));
    frame_connections_.append(connect(
        newWindow, &QQuickWindow::afterRenderPassRecording, this,
        [clock, timings] {
            if (clock->render_detail) clock->render_detail->next(QStringLiteral("Render finalization"));
        },
        Qt::DirectConnection));
    frame_connections_.append(connect(
        newWindow, &QQuickWindow::afterRendering, this,
        [clock, timings, newWindow] {
            clock->render_detail.reset();
            // D3D11 executes recorded commands here as part of endFrame(), before
            // Present(). This interval is not exclusively display/vsync waiting.
            if (clock->stage) clock->stage->next(QStringLiteral("Execute / present"));
            // Query only completed work; never stall the GPU to obtain a timestamp.
            auto* renderer = newWindow->rendererInterface();
            if (!clock->graphics_reported) {
                auto* rhi = static_cast<QRhi*>(renderer->getResource(newWindow, QSGRendererInterface::RhiResource));
                if (rhi) {
                    timings->setGraphicsInfo(QStringLiteral("%1 | %2").arg(QString::fromLatin1(rhi->backendName()),
                                                                           QString::fromUtf8(rhi->driverInfo().deviceName)));
                    clock->graphics_reported = true;
                }
            }
            auto* swapchain = static_cast<QRhiSwapChain*>(renderer->getResource(newWindow, QSGRendererInterface::RhiSwapchainResource));
            if (swapchain) {
                if (auto* commands = swapchain->currentFrameCommandBuffer()) {
                    timings->recordGpuTime(commands->lastCompletedGpuTime() * 1000.0);
                }
            }
        },
        Qt::DirectConnection));
    frame_connections_.append(connect(
        newWindow, &QQuickWindow::afterFrameEnd, this,
        [this, clock, timings] {
            if (!clock->frame) return;
            timings->dragFrameSubmitted();
            if (clock->frame_elapsed.isValid()) {
                timings->recordFrameUploadPeak(clock->frame_elapsed.nsecsElapsed() / 1.0e6,
                                               frame_uploaded_tiles_.exchange(0, std::memory_order_relaxed),
                                               frame_upload_lod_.load(std::memory_order_relaxed));
            }
            clock->render_detail.reset();
            clock->stage.reset();
            clock->frame.reset();
            if (clock->interval.isValid()) timings->recordFrameInterval(clock->interval.nsecsElapsed() / 1.0e6);
            clock->interval.start();
        },
        Qt::DirectConnection));
}

void TerrainQuickItem::setMetricsEnabled(bool enabled) {
    if (metricsEnabled() == enabled) return;
    metrics_enabled_.store(enabled, std::memory_order_relaxed);
    if (enabled) {
        refreshMetrics();
        metrics_timer_.start();
        connectFrameTimings(window());
        if (provider_) provider_->setTimings(timings_);
    } else {
        metrics_timer_.stop();
        connectFrameTimings(nullptr);
        frame_uploaded_tiles_.store(0, std::memory_order_relaxed);
        if (provider_) provider_->setTimings({});
    }
    emit metricsEnabledChanged();
}

void TerrainQuickItem::setCheckerColor(const QColor& color) {
    if (!color.isValid() || checker_color_ == color) return;
    checker_color_ = color;
    emit checkerColorChanged();
    update();
}

void TerrainQuickItem::setCheckerAlternateColor(const QColor& color) {
    if (!color.isValid() || checker_alternate_color_ == color) return;
    checker_alternate_color_ = color;
    emit checkerAlternateColorChanged();
    update();
}

void TerrainQuickItem::setTerrainOutline(bool enabled) {
    if (terrain_outline_ == enabled) return;
    terrain_outline_ = enabled;
    emit terrainOutlineChanged();
    update();
}

void TerrainQuickItem::refreshMetrics() {
    timing_text_ = timings_->toString();
    frames_per_second_ = timings_->framesPerSecond();
    {
        const QMutexLocker lock(&metrics_mutex_);
        display_metrics_ = published_metrics_;
    }
    emit metricsChanged();
}

void TerrainQuickItem::publishMetrics(const TerrainFrameMetrics& metrics) {
    if (!metricsEnabled()) return;
    const QMutexLocker lock(&metrics_mutex_);
    published_metrics_ = metrics;
}

void TerrainQuickItem::setController(MapController* controller) {
    if (controller_ == controller) return;
    if (controller_) disconnect(controller_, nullptr, this, nullptr);
    controller_ = controller;
    if (controller_) {
        connect(controller_, &MapController::stateChanged, this, [this] {
            syncViewState();
            update();
        });
        connect(controller_, &MapController::cameraMoved, this, [this] {
            timings_->dragCameraUpdated();
            syncViewState();
            update();
        });
        connect(controller_, &MapController::dimensionChanged, this, [this] {
            syncViewState();
            update();
        });
        connect(controller_, &MapController::worldStateChanged, this, [this] {
            syncViewState();
            update();
        });
        syncViewState();
    }
    emit controllerChanged();
    requestWindowRepaint();
}

void TerrainQuickItem::setProvider(TerrainDataProvider* provider) {
    if (provider_ == provider) return;
    if (provider_) {
        disconnect(provider_, nullptr, this, nullptr);
        // The provider outlives the active renderer. Detach its debug sink so
        // background work can never be reported as part of another world.
        provider_->setTimings({});
    }
    // Tile hand-offs are keyed by region and dimension, which two worlds can
    // share. They must never survive a session switch or a queued tile from the
    // old world could be uploaded into the new world's atlas.
    tile_delivery_.clear();
    render_reset_generation_.fetch_add(1, std::memory_order_acq_rel);
    timings_->reset();
    if (has_data_) {
        has_data_ = false;
        emit hasDataChanged();
    }
    provider_ = provider;
    if (provider_) provider_->setTimings(metricsEnabled() ? timings_ : std::shared_ptr<RenderTimings>{});
    requested_blocks_per_texel_.store(1, std::memory_order_release);
    candidate_blocks_per_texel_ = 1;
    lod_settle_timer_.stop();
    if (provider_) {
        connect(provider_, &TerrainDataProvider::overviewReady, this, [this](const QImage& image, const QRect& chunks, int dimension) {
            if (!image.isNull() && dimension != camera_.dimension) return;
            tile_delivery_.setOverview(image, chunks, dimension);
            if (!image.isNull() && !has_data_) {
                has_data_ = true;
                emit hasDataChanged();
            }
            update();
        });
        connect(provider_, &TerrainDataProvider::tileReady, this,
                [this](int regionX, int regionZ, int dimension, int blocks_per_texel, int layer, const QImage& color, const QImage& surface,
                       bool hasChunks) {
                    // This runs on the GUI thread, where the provider drives the
                    // shared scheduler. Only the finished tiles cross over to the
                    // scene-graph render thread, which just uploads them. A baked
                    // region whose surface is all void stays in the empty set.
                    if (hasChunks && !has_data_) {
                        has_data_ = true;
                        emit hasDataChanged();
                    }
                    // Overview mode draws the aggregated overview image, not tiles.
                    if (detailedTileMode() && blocks_per_texel == provider_->blocksPerTexel())
                        queueArrayRegionPiece(regionX, regionZ, dimension, blocks_per_texel, layer, color, surface, !hasChunks);
                    // QQuickItem::update() already coalesces to one scene-graph
                    // frame. A whole-window update plus a zero-delay timer per
                    // tile turns a region refill into an event storm.
                    update();
                });
        connect(provider_, &TerrainDataProvider::windowRefilled, this, [this](int, quint64, const QRect&, int, int) {
            QTimer::singleShot(1500, this, [this] {
                if (provider_) provider_->prefetchCoarserLevels(terrain_render_config::prefetch_lod_levels);
            });
            update();
        });
        connect(provider_, &TerrainDataProvider::regionEmpty, this,
                [this](int regionX, int regionZ, int dimension, int blocksPerTexel, int layer) {
                    queueArrayRegionPiece(regionX, regionZ, dimension, blocksPerTexel, layer, QImage(), QImage(), true);
                    update();
                });
        syncViewState();
    }
    emit providerChanged();
    requestWindowRepaint();
}

void TerrainQuickItem::syncViewState() {
    const RenderTimingScope timing(metricsEnabled() ? timings_.get() : nullptr, QStringLiteral("Camera / viewport GUI"));
    if (!controller_) return;

    const MapCameraState camera = controller_->cameraState();
    if (camera_ != camera) camera_ = camera;
    if (!provider_) return;
    // Settle the level first so the provider sees the final mode, level and
    // window in one pass.
    scheduleLodRequest();
    provider_->setOverviewMode(!detailedTileMode(), requested_blocks_per_texel_.load(std::memory_order_acquire));
    // Cover the viewport plus the shading margin, rather than a fixed tile radius:
    // the window has to follow the view, or a zoomed-out map stays mostly
    // placeholder. The level of detail picked for the frame keeps the visible
    // span inside the atlas; the budget keeps the resident set bounded.
    const qreal px_per_block = std::max<qreal>(0.0001, camera_.pixelsPerChunk / 16.0);
    const int requestBlocksPerTexel = requested_blocks_per_texel_.load(std::memory_order_acquire);
    const double margin =
        std::max<double>(render_settings_ ? shadowReachBlocks(render_settings_->gpu) : 0, std::max(requestBlocksPerTexel, 16));
    const int half_x = static_cast<int>(std::ceil(width() / (2.0 * px_per_block) + margin));
    const int half_z = static_cast<int>(std::ceil(height() / (2.0 * px_per_block) + margin));
    const int center_block_x = static_cast<int>(std::floor(camera_.center.x() * 16.0));
    const int center_block_z = static_cast<int>(std::floor(camera_.center.y() * 16.0));
    constexpr int region_blocks = terrain_render_config::region_blocks;
    int region_x0 = floorDiv(center_block_x - half_x, region_blocks);
    int region_x1 = floorDiv(center_block_x + half_x, region_blocks);
    int region_z0 = floorDiv(center_block_z - half_z, region_blocks);
    int region_z1 = floorDiv(center_block_z + half_z, region_blocks);

    // A coarser level packs bp^2 times more region slots into the same atlas, so
    // the window may span proportionally more regions before it is clipped. At
    // one texel per block this is exactly max_requested_regions.
    if (detailedTileMode())
        clampToRegionBudget(
            region_x0, region_x1, region_z0, region_z1,
            static_cast<qint64>(terrain_render_config::max_requested_regions) * requestBlocksPerTexel * requestBlocksPerTexel);
    if (region_x1 < region_x0 || region_z1 < region_z0) return;
    const QRect regionWindow(region_x0, region_z0, region_x1 - region_x0 + 1, region_z1 - region_z0 + 1);
    request_ = TerrainViewportRequest{regionWindow, camera_.dimension, camera_.layer};
    // Publish the window to the provider: that call is also what makes the
    // shared scheduler bake, prune and prioritise for this viewport.
    publishViewport();
    publishFrameState();
}

void TerrainQuickItem::publishViewport() {
    if (!provider_) return;
    provider_->setViewport(request_);
}

void TerrainQuickItem::publishFrameState() {
    auto next = std::make_shared<TerrainFrameState>();
    next->camera = camera_;
    next->request = request_;
    next->settings = render_settings_;
    next->requestedBlocksPerTexel = requested_blocks_per_texel_.load(std::memory_order_acquire);
    next->overviewOnly = !detailedTileMode();
    next->transparentVoid = controller_ && controller_->transparentVoid();
    frame_state_channel_.publish(std::move(next));
}

int TerrainQuickItem::blocksPerTexelFor() const {
    const qreal pxPerBlock = std::max<qreal>(0.0001, camera_.pixelsPerChunk / 16.0);
    // Respect the device's actual slice budget when retaining finer levels.
    int layers = render_settings_ ? render_settings_->tileLayers : terrain_render_config::tile_array_layers;
    if (const int created = created_tile_layers_.load(std::memory_order_acquire); created > 0) layers = std::min(layers, created);
    const double minimumTexelPixels =
        minimumTerrainTexelPixels(render_settings_ && render_settings_->highDetailMap, width(), height(), layers);
    int level = 1;
    while (level < terrain_render_config::max_blocks_per_texel && level * pxPerBlock < minimumTexelPixels) level *= 2;
    return level;
}

bool TerrainQuickItem::detailedTileMode() const {
    return !provider_ || !provider_->overviewEnabled() || camera_.pixelsPerChunk >= constant::MINIMUM_TERRAIN_SCALE;
}

void TerrainQuickItem::scheduleLodRequest() {
    if (!provider_) return;
    const int current = requested_blocks_per_texel_.load(std::memory_order_acquire);
    const int wanted = blocksPerTexelFor();
    if (wanted == current) {
        candidate_blocks_per_texel_ = wanted;
        lod_settle_timer_.stop();
        return;
    }
    // Coarser levels cover more of the current view. Waiting here exposes the
    // uncovered part of the old fine atlas as gray, so zoom-out switches now.
    // Finer levels still wait for a stable target before rebuilding.
    if (wanted > current) {
        lod_settle_timer_.stop();
        candidate_blocks_per_texel_ = wanted;
        commitLodRequest(wanted);
        return;
    }
    if (wanted != candidate_blocks_per_texel_ || !lod_settle_timer_.isActive()) {
        candidate_blocks_per_texel_ = wanted;
        lod_settle_timer_.start();
    }
}

void TerrainQuickItem::commitLodRequest(int level) {
    // syncViewState() hands the level to the provider through setOverviewMode(),
    // which resets delivery once for the new level.
    requested_blocks_per_texel_.store(level, std::memory_order_release);
    update();
}

int TerrainQuickItem::shadowReachBlocks(const GpuRenderOptions& gpu) {
    return gpu.shadow_enabled && gpu.shadow_strength > 0.0f ? gpu.shadow_steps : 0;
}

void TerrainQuickItem::initializeRenderSettings() {
    auto next = std::make_shared<TerrainRenderSettingsSnapshot>();
    next->gpu = gpuRenderOptionsFromSettings();
    next->cpuShadowLevel = setting::current().cpuShadowLevel;
    next->highDetailMap = setting::current().gpuHighDetailMap;
    next->tileLayers =
        next->highDetailMap ? terrain_render_config::high_detail_tile_array_layers : terrain_render_config::tile_array_layers;
    render_settings_ = std::move(next);
}

void TerrainQuickItem::requestWindowRepaint() {
    update();
    if (auto* currentWindow = window()) {
        currentWindow->update();
        // A new scene graph may not exist until the next event-loop pass, so
        // repeat the request once the window has had a chance to create it.
        QTimer::singleShot(0, this, [this] {
            if (auto* queuedWindow = window()) queuedWindow->update();
        });
    }
}

void TerrainQuickItem::itemChange(ItemChange change, const ItemChangeData& value) {
    QQuickItem::itemChange(change, value);
    if (change == ItemSceneChange && window()) requestWindowRepaint();
}

void TerrainQuickItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        syncViewState();
        update();
    }
}

void TerrainQuickItem::queueArrayRegionPiece(int regionX, int regionZ, int dimension, int blocksPerTexel, int layer, const QImage& color,
                                             const QImage& surface, bool empty) {
    const int bp = std::max(1, blocksPerTexel);
    const int regionIndexX = floorDiv(regionX, constant::RW);
    const int regionIndexZ = floorDiv(regionZ, constant::RW);
    // Coarse snapshots occupy only 128 / bp pixels. Pack bp x bp canonical
    // regions into one layer instead of magnifying every region to 128 pixels.
    const int pixels = terrain_render_config::region_blocks / bp;
    const auto piece = [pixels, empty](const QImage& source) {
        QImage result;
        if (empty || source.isNull()) {
            result = QImage(pixels, pixels, QImage::Format_RGBA8888);
            result.fill(0);
        } else {
            result = source.size() == QSize(pixels, pixels) ? source : source.scaled(pixels, pixels);
            result = result.convertToFormat(QImage::Format_RGBA8888);
        }
        return result;
    };
    const int groupX = floorDiv(regionIndexX, bp);
    const int groupZ = floorDiv(regionIndexZ, bp);
    const TerrainWorldTileKey key{bp, groupX, groupZ, dimension, layer};
    const QPoint destination((regionIndexX - groupX * bp) * pixels, (regionIndexZ - groupZ * bp) * pixels);
    tile_delivery_.pushTile({key, piece(color), piece(surface), destination, empty});
    update();
}

QSGNode* TerrainQuickItem::updateArrayPaintNode(QSGNode* oldNode, const MapCameraState& camera, int requestedBlocksPerTexel,
                                                bool overviewOnly, bool transparentVoid,
                                                const std::shared_ptr<const TerrainRenderSettingsSnapshot>& render_settings) {
    constexpr int tilePixels = 128;
    constexpr int gridSide = 256;
    // The detail setting picks the slice count; the device may support fewer.
    int layerCount = render_settings ? render_settings->tileLayers : terrain_render_config::tile_array_layers;
    if (auto* currentWindow = window()) {
        auto* rhi = static_cast<QRhi*>(currentWindow->rendererInterface()->getResource(currentWindow, QSGRendererInterface::RhiResource));
        if (rhi) layerCount = std::min(layerCount, rhi->resourceLimit(QRhi::TextureArraySizeMax));
    }
    layerCount = std::max(layerCount, 1);
    auto* root = oldNode ? oldNode : new QSGNode;
    const quint64 resetGeneration = render_reset_generation_.load(std::memory_order_acquire);
    // A new slice count needs new tile arrays; the provider resends every tile.
    const bool layersChanged = array_gpu_.color && layerCount != array_tile_pool_.layerCount;
    const bool rendererReset = resetGeneration != applied_render_reset_generation_ || layersChanged;
    if (rendererReset || !oldNode) {
        applied_render_reset_generation_ = resetGeneration;
        // A null oldNode means Qt already destroyed the previous scene graph.
        // Cached node pointers then dangle and must only be reset, never freed.
        if (rendererReset && oldNode && array_gpu_.node) {
            root->removeChildNode(array_gpu_.node);
            delete array_gpu_.node;
            array_gpu_.node = nullptr;
        }
        array_gpu_.resetPointers();
        array_tile_pool_.resetForRendererLoss(layerCount);
    }
    if (camera.pixelsPerChunk <= 0 || width() <= 0 || height() <= 0) return root;

    const int nextBlocksPerTexel = std::max(1, requestedBlocksPerTexel);
    if (camera.dimension != array_tile_pool_.activeDimension || camera.layer != array_tile_pool_.activeLayer) {
        array_tile_pool_.resetForViewChange(camera.dimension, camera.layer, layerCount);
    }
    array_tile_pool_.activeLod = nextBlocksPerTexel;

    if (!array_gpu_.color) {
        if (array_tile_pool_.freeLayers.isEmpty()) {
            for (int i = 0; i < layerCount; ++i) array_tile_pool_.freeLayers.push_back(i);
        }
        array_tile_pool_.layerCount = layerCount;
        created_tile_layers_.store(layerCount, std::memory_order_release);
        array_gpu_.color = new TerrainAtlasTexture(QSize(tilePixels, tilePixels), layerCount);
        array_gpu_.color->setFiltering(QSGTexture::Nearest);
        array_gpu_.color->setTimings(timings_);
        array_gpu_.surface = new TerrainAtlasTexture(QSize(tilePixels, tilePixels), layerCount);
        array_gpu_.surface->setFiltering(QSGTexture::Nearest);
        array_gpu_.surface->setTimings(timings_);
        // The page-table rebuild below uploads every slice on this first frame,
        // because the renderer reset cleared the uploaded copies.
        for (auto& image : array_tile_pool_.pageTableImages) image = QImage(gridSide, gridSide, QImage::Format_RGBA8888);
        array_gpu_.pageTable = new TerrainAtlasTexture(QSize(gridSide, gridSide), terrain_render_config::page_table_levels);
        array_gpu_.pageTable->setFiltering(QSGTexture::Nearest);
        array_gpu_.pageTable->setTimings(timings_);
        array_gpu_.overview =
            new TerrainAtlasTexture(QSize(terrain_render_config::overview_texture_pixels, terrain_render_config::overview_texture_pixels));
        array_gpu_.overview->setFiltering(QSGTexture::Nearest);
        array_gpu_.overview->setTimings(timings_);
        if (array_gpu_.biomePalette.isNull()) array_gpu_.biomePalette = TerrainTiles::biomePaletteImage();
        auto* paletteTexture = new TerrainAtlasTexture(array_gpu_.biomePalette);
        paletteTexture->setFiltering(QSGTexture::Nearest);
        paletteTexture->setTimings(timings_);
        array_gpu_.material =
            new TerrainArrayMaterial(array_gpu_.color, array_gpu_.surface, array_gpu_.pageTable, array_gpu_.overview, paletteTexture);
        array_gpu_.node = new QSGGeometryNode;
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);
        geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        geometry->setVertexDataPattern(QSGGeometry::StaticPattern);
        array_gpu_.node->setGeometry(geometry);
        array_gpu_.node->setFlag(QSGNode::OwnsGeometry, true);
        array_gpu_.node->setMaterial(array_gpu_.material);
        array_gpu_.node->setFlag(QSGNode::OwnsMaterial, true);
        root->appendChildNode(array_gpu_.node);
    }

    ++array_tile_pool_.frameCounter;
    for (auto it = array_tile_pool_.retiringLayers.begin(); it != array_tile_pool_.retiringLayers.end();) {
        if (array_tile_pool_.frameCounter >= it.value() + 2) {
            array_tile_pool_.freeLayers.push_back(it.key());
            it = array_tile_pool_.retiringLayers.erase(it);
        } else {
            ++it;
        }
    }

    const auto overview = tile_delivery_.overviewSnapshot();
    if (overview.image.isNull()) array_tile_pool_.overviewChunks = {};
    if (overview.dimension == camera.dimension && !overview.image.isNull() &&
        overview.image.cacheKey() != array_tile_pool_.overviewCacheKey) {
        array_gpu_.overview->queueUploads({TerrainAtlasUpload{overview.image, QPoint(0, 0), 0}});
        array_tile_pool_.overviewChunks = overview.chunks;
        array_tile_pool_.overviewCacheKey = overview.image.cacheKey();
    }
    const qreal pxPerBlock = std::max<qreal>(0.0001, camera.pixelsPerChunk / 16.0);
    const qreal spanX = width() / pxPerBlock;
    const qreal spanZ = height() / pxPerBlock;
    const qreal worldX = camera.center.x() * 16.0 - spanX / 2.0;
    const qreal worldZ = camera.center.y() * 16.0 - spanZ / 2.0;
    const int tileWorld = terrain_render_config::region_blocks;
    const int visibleX0 = floorDiv(static_cast<int>(std::floor(worldX)), tileWorld) - 2;
    const int visibleX1 = floorDiv(static_cast<int>(std::ceil(worldX + spanX)), tileWorld) + 2;
    const int visibleZ0 = floorDiv(static_cast<int>(std::floor(worldZ)), tileWorld) - 2;
    const int visibleZ1 = floorDiv(static_cast<int>(std::ceil(worldZ + spanZ)), tileWorld) + 2;
    const auto isProtected = [&](const TerrainWorldTileKey& key) {
        // Any resident LOD in the visible window may be sampled as a fallback
        // while the requested level is being filled, so do not evict it during
        // this frame's uploads.
        return key.x >= floorDiv(visibleX0, key.lod) && key.x <= floorDiv(visibleX1, key.lod) && key.z >= floorDiv(visibleZ0, key.lod) &&
               key.z <= floorDiv(visibleZ1, key.lod);
    };

    QVector<TerrainWorldTileReady> readyTiles = tile_delivery_.takeTiles();
    // Cache hits can arrive before the render thread resets its atlas for a
    // layer or provider change. Keep those deliveries; the provider has already
    // marked them as delivered and will not resend them until the view changes.
    readyTiles.erase(std::remove_if(readyTiles.begin(), readyTiles.end(),
                                    [&camera](const TerrainWorldTileReady& tile) {
                                        return tile.key.dimension != camera.dimension || tile.key.layer != camera.layer;
                                    }),
                     readyTiles.end());
    constexpr int detailUploadBudget = 64;
    QVector<TerrainWorldTileReady> deferredDetail;
    if (readyTiles.size() > detailUploadBudget) {
        deferredDetail = readyTiles.mid(detailUploadBudget);
        readyTiles.resize(detailUploadBudget);
    }
    if (!deferredDetail.isEmpty()) {
        tile_delivery_.restoreTiles(std::move(deferredDetail));
        update();
    }
    QVector<TerrainAtlasUpload> tileUploads;
    QVector<TerrainAtlasUpload> surfaceUploads;
    QVector<TerrainWorldTileReady> layerDeferred;
    bool layersExhausted = false;
    const auto takeLayer = [&]() -> int {
        if (!array_tile_pool_.freeLayers.isEmpty()) return array_tile_pool_.freeLayers.takeLast();
        auto victim = array_tile_pool_.tileLayers.end();
        quint64 oldest = std::numeric_limits<quint64>::max();
        for (auto it = array_tile_pool_.tileLayers.begin(); it != array_tile_pool_.tileLayers.end(); ++it) {
            // Residency is reset on every dimension or layer change, so every
            // key belongs to the active view.
            if (it.value() < 0 || (isProtected(it.key()) && it.key().lod == nextBlocksPerTexel) ||
                array_tile_pool_.retiringLayers.contains(it.value()))
                continue;
            const quint64 lastUsed = array_tile_pool_.layerLastUsed.value(it.value(), 0);
            if (lastUsed < oldest) {
                oldest = lastUsed;
                victim = it;
            }
        }
        if (victim == array_tile_pool_.tileLayers.end()) {
            layersExhausted = true;
            return -1;
        }
        const int retiredLayer = victim.value();
        array_tile_pool_.pageTableDirty = true;
        array_tile_pool_.uploadedTiles.remove(victim.key());
        array_tile_pool_.terrainTiles.remove(victim.key());
        array_tile_pool_.tileLayers.erase(victim);
        array_tile_pool_.layerLastUsed.remove(retiredLayer);
        array_tile_pool_.retiringLayers.insert(retiredLayer, array_tile_pool_.frameCounter);
        return -1;
    };
    for (auto& tile : readyTiles) {
        if (tile.empty && tile.key.lod > 1 && !array_tile_pool_.tileLayers.contains(tile.key)) continue;
        if (tile.empty && tile.key.lod == 1) {
            array_tile_pool_.pageTableDirty = true;
            auto existing = array_tile_pool_.tileLayers.find(tile.key);
            if (existing != array_tile_pool_.tileLayers.end() && existing.value() >= 0) {
                array_tile_pool_.retiringLayers.insert(existing.value(), array_tile_pool_.frameCounter);
                array_tile_pool_.layerLastUsed.remove(existing.value());
            }
            array_tile_pool_.tileLayers[tile.key] = -1;
            array_tile_pool_.uploadedTiles.insert(tile.key);
            array_tile_pool_.terrainTiles.remove(tile.key);
            continue;
        }
        auto assigned = array_tile_pool_.tileLayers.constFind(tile.key);
        if (assigned != array_tile_pool_.tileLayers.cend() && assigned.value() < 0) {
            array_tile_pool_.tileLayers.erase(array_tile_pool_.tileLayers.find(tile.key));
            assigned = array_tile_pool_.tileLayers.cend();
        }
        bool first = false;
        if (assigned == array_tile_pool_.tileLayers.cend()) {
            const int layer = takeLayer();
            if (layer < 0) {
                layerDeferred.append(std::move(tile));
                continue;
            }
            array_tile_pool_.tileLayers.insert(tile.key, layer);
            assigned = array_tile_pool_.tileLayers.constFind(tile.key);
            first = true;
        }
        array_tile_pool_.layerLastUsed[assigned.value()] = array_tile_pool_.frameCounter;
        // A packed coarse layer must be cleared so unfilled sub-regions do not
        // show the previous owner; a full-layer piece overwrites it anyway.
        if (first && tile.color.size() != QSize(tilePixels, tilePixels)) {
            if (array_tile_pool_.clearLayerImage.isNull()) {
                array_tile_pool_.clearLayerImage = QImage(tilePixels, tilePixels, QImage::Format_RGBA8888);
                array_tile_pool_.clearLayerImage.fill(0);
            }
            tileUploads.append({array_tile_pool_.clearLayerImage, QPoint(0, 0), assigned.value()});
            surfaceUploads.append({array_tile_pool_.clearLayerImage, QPoint(0, 0), assigned.value()});
        }
        array_tile_pool_.pageTableDirty = true;
        if (!tile.empty) array_tile_pool_.terrainTiles.insert(tile.key);
        tileUploads.append({std::move(tile.color), tile.destination, assigned.value()});
        surfaceUploads.append({std::move(tile.surface), tile.destination, assigned.value()});
        array_tile_pool_.uploadedTiles.insert(tile.key);
    }
    if (!layerDeferred.isEmpty()) {
        tile_delivery_.restoreTiles(std::move(layerDeferred));
        // With every layer pinned by the visible window, retrying each frame
        // cannot free one; wait for a camera or retirement change instead.
        if (!layersExhausted || !array_tile_pool_.retiringLayers.isEmpty()) {
            update();
        } else if (!array_tile_pool_.layersExhaustedLogged) {
            LOG_F(WARNING, "Terrain tile array exhausted: %d layers are all visible at %d blocks per texel", layerCount,
                  nextBlocksPerTexel);
            array_tile_pool_.layersExhaustedLogged = true;
        }
    } else {
        array_tile_pool_.layersExhaustedLogged = false;
    }
    const int uploadedTileCount = static_cast<int>(tileUploads.size());
    if (!tileUploads.isEmpty()) array_gpu_.color->queueUploads(std::move(tileUploads));
    if (!surfaceUploads.isEmpty()) array_gpu_.surface->queueUploads(std::move(surfaceUploads));

    // The page table depends only on residency and the visible window, so it is
    // rebuilt when either changes rather than on every frame.
    const QRect visibleWindow(QPoint(visibleX0, visibleZ0), QPoint(visibleX1, visibleZ1));
    if (array_tile_pool_.pageTableDirty || visibleWindow != array_tile_pool_.pageTableWindow) {
        array_tile_pool_.pageTableDirty = false;
        array_tile_pool_.pageTableWindow = visibleWindow;
        for (auto& image : array_tile_pool_.pageTableImages) image.fill(0);
        // Residency is reset on every dimension or layer change, so every key
        // belongs to the active view.
        for (auto it = array_tile_pool_.tileLayers.cbegin(); it != array_tile_pool_.tileLayers.cend(); ++it) {
            if (!array_tile_pool_.uploadedTiles.contains(it.key()) || !isProtected(it.key())) continue;
            const int lodIndex = std::countr_zero(static_cast<unsigned>(it.key().lod));
            const int x = ((it.key().x % gridSide) + gridSide) % gridSide;
            const int z = ((it.key().z % gridSide) + gridSide) % gridSide;
            uchar* row = array_tile_pool_.pageTableImages[lodIndex].scanLine(z);
            row[x * 4] = static_cast<uchar>(it.value() & 0xFF);
            row[x * 4 + 1] = static_cast<uchar>((it.value() >> 8) & 0xFF);
            const uint32_t hashValue = [&] {
                uint32_t h = static_cast<uint16_t>(it.key().x) * 40503u;
                h ^= static_cast<uint16_t>(it.key().z) * 60307u;
                h ^= h >> 8;
                h &= 0x3FFFu;
                return h == 0 ? 1u : h;
            }();
            const bool hasChunks = array_tile_pool_.terrainTiles.contains(it.key());
            row[x * 4 + 2] = static_cast<uchar>(0x80u | (hasChunks ? 0u : 0x40u) | ((hashValue >> 8) & 0x3Fu));
            row[x * 4 + 3] = static_cast<uchar>(hashValue & 0xFFu);
            // Visible entries count as used for LRU eviction.
            array_tile_pool_.layerLastUsed[it.value()] = array_tile_pool_.frameCounter;
        }
        // Compare whole slices instead of hashing entries: the result does not
        // depend on hash iteration order and cannot miss a change by collision.
        QVector<TerrainAtlasUpload> uploads;
        for (int lod = 0; lod < terrain_render_config::page_table_levels; ++lod) {
            const QImage& image = array_tile_pool_.pageTableImages[lod];
            QImage& uploaded = array_tile_pool_.uploadedPageTables[lod];
            if (!uploaded.isNull() && std::memcmp(uploaded.constBits(), image.constBits(), image.sizeInBytes()) == 0) continue;
            uploaded = image.copy();
            uploads.append({uploaded, QPoint(0, 0), lod});
        }
        if (!uploads.isEmpty()) array_gpu_.pageTable->queueUploads(std::move(uploads));
    }

    if (!array_tile_pool_.geometrySize.isValid() || array_tile_pool_.geometrySize != QSizeF(width(), height())) {
        auto* vertices = static_cast<QSGGeometry::TexturedPoint2D*>(array_gpu_.node->geometry()->vertexData());
        vertices[0].set(0.0f, 0.0f, 0.0f, 0.0f);
        vertices[1].set(static_cast<float>(width()), 0.0f, 1.0f, 0.0f);
        vertices[2].set(0.0f, static_cast<float>(height()), 0.0f, 1.0f);
        vertices[3].set(static_cast<float>(width()), static_cast<float>(height()), 1.0f, 1.0f);
        array_gpu_.node->geometry()->markVertexDataDirty();
        array_gpu_.node->markDirty(QSGNode::DirtyGeometry);
        array_tile_pool_.geometrySize = QSizeF(width(), height());
    }
    TerrainArrayUniformBody body;
    body.void_options[0] = transparentVoid ? 1.0f : 0.0f;
    {
        constexpr qreal outline_device_pixels = 4.0;
        constexpr qreal outline_min_device_pixels_per_block = 0.125;
        const qreal device_px_per_block = pxPerBlock * (window() ? window()->effectiveDevicePixelRatio() : 1.0);
        if (terrain_outline_ && !overviewOnly && device_px_per_block >= outline_min_device_pixels_per_block) {
            body.void_options[1] = static_cast<float>(outline_device_pixels / device_px_per_block);
            body.void_options[2] = static_cast<float>(1.0 / device_px_per_block);
        }
    }
    body.checker = {float(checker_color_.redF()), float(checker_color_.greenF()), float(checker_color_.blueF()), 1.0f};
    body.checker_alternate = {float(checker_alternate_color_.redF()), float(checker_alternate_color_.greenF()),
                              float(checker_alternate_color_.blueF()), 1.0f};
    body.view = {static_cast<float>(worldX), static_cast<float>(worldZ), static_cast<float>(spanX), static_cast<float>(spanZ)};
    const int lodIndex = std::countr_zero(static_cast<unsigned>(array_tile_pool_.activeLod));
    body.params = {static_cast<float>(tileWorld), static_cast<float>(gridSide), static_cast<float>(lodIndex),
                   static_cast<float>(std::max(1, array_tile_pool_.activeLod))};
    body.tint[3] = overviewOnly ? 1.0f : 0.0f;
    body.overview = {float(array_tile_pool_.overviewChunks.x() * 16), float(array_tile_pool_.overviewChunks.y() * 16),
                     float(array_tile_pool_.overviewChunks.width() * 16), float(array_tile_pool_.overviewChunks.height() * 16)};
    if (const auto& settings = render_settings) {
        const auto& gpu = settings->gpu;
        int sunX = -1;
        int sunZ = -1;
        gpuSunStep(sunX, sunZ);
        const int shadowReach = shadowReachBlocks(gpu);
        const qreal devicePxPerBlock = pxPerBlock * (window() ? window()->effectiveDevicePixelRatio() : 1.0);
        const qreal texelPixels = devicePxPerBlock * std::max(1, array_tile_pool_.activeLod);
        const float baseEdgeWidth = static_cast<float>(std::clamp<qreal>(1.0 / std::max<qreal>(texelPixels, 0.01), 0.25, 0.5));
        body.shade = {static_cast<float>(sunX), static_cast<float>(sunZ), static_cast<float>(shadowReach), gpu.shadow_step};
        body.shadow = {static_cast<float>(1.0 - std::clamp(settings->cpuShadowLevel, 0, 255) / 255.0 * 0.75), gpu.shadow_strength,
                       std::clamp(baseEdgeWidth * gpu.bevel_width, 0.0625f, 0.75f), gpu.biome_blend_blocks};
        // Fade AO to zero before the full-resolution tiles switch to a coarser LOD.
        const double minimumTexelPixels = minimumTerrainTexelPixels(settings->highDetailMap, width(), height(), layerCount);
        const float aoZoom = static_cast<float>(std::clamp(pxPerBlock / minimumTexelPixels - 1.0, 0.0, 1.0));
        const float aoFade = aoZoom * aoZoom * (3.0f - 2.0f * aoZoom);
        body.ao = {gpu.ao_strength, aoFade, gpu.ao_radius, static_cast<float>(gpu.ao_steps)};
        body.tint = {gpu.saturation, gpu.brightness, gpu.bevel_strength, overviewOnly ? 1.0f : 0.0f};
        body.water = {gpu.water_base_color[0], gpu.water_base_color[1], gpu.water_base_color[2], 0.0f};
        body.grass = {gpu.grass_height_base, gpu.grass_height_range, gpu.grass_height_enabled ? 1.0f : 0.0f, gpu.biome_blend_blocks};
        body.grass_color = {gpu.grass_height_color[0], gpu.grass_height_color[1], gpu.grass_height_color[2], 0.0f};
    }
    if (!array_tile_pool_.hasLastUniforms || std::memcmp(&array_tile_pool_.lastUniforms, &body, sizeof(body)) != 0) {
        array_tile_pool_.lastUniforms = body;
        array_tile_pool_.hasLastUniforms = true;
        array_gpu_.material->setUniforms(body);
        array_gpu_.node->markDirty(QSGNode::DirtyMaterial);
    }
    if (array_gpu_.color->hasPendingUploads() || array_gpu_.surface->hasPendingUploads() || array_gpu_.pageTable->hasPendingUploads() ||
        array_gpu_.overview->hasPendingUploads())
        update();
    if (metricsEnabled()) {
        TerrainFrameMetrics metrics;
        metrics.blocksPerTexel = array_tile_pool_.activeLod;
        metrics.atlasSlotsPerSide = gridSide;
        metrics.uploadedTiles = uploadedTileCount;
        // Residency is reset on every dimension or layer change, so every key
        // belongs to the active view.
        for (auto it = array_tile_pool_.tileLayers.cbegin(); it != array_tile_pool_.tileLayers.cend(); ++it) {
            if (array_tile_pool_.terrainTiles.contains(it.key()))
                ++metrics.terrainSlots;
            else if (array_tile_pool_.uploadedTiles.contains(it.key()))
                ++metrics.emptySlots;
        }
        metrics.loadingSlots =
            std::max(0, static_cast<int>(array_tile_pool_.tileLayers.size()) - metrics.terrainSlots - metrics.emptySlots);
        publishMetrics(metrics);
        frame_uploaded_tiles_.fetch_add(metrics.uploadedTiles, std::memory_order_relaxed);
        frame_upload_lod_.store(array_tile_pool_.activeLod, std::memory_order_relaxed);
    }
    render_state_->publish(camera.center.x(), camera.center.y(), camera.pixelsPerChunk, camera.dimension);
    return root;
}

QSGNode* TerrainQuickItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    const RenderTimingScope timing(metricsEnabled() ? timings_.get() : nullptr, QStringLiteral("Map update"));
    auto* root = oldNode ? oldNode : new QSGNode;
    const auto frameState = frame_state_channel_.take();
    if (!frameState) return root;
    const MapCameraState& viewCamera = frameState->camera;
    const int viewRequestedBlocksPerTexel = frameState->requestedBlocksPerTexel;
    const bool overviewOnly = frameState->overviewOnly;
    const int effectiveBlocksPerTexel = overviewOnly ? terrain_render_config::overview_blocks_per_texel : viewRequestedBlocksPerTexel;
    return updateArrayPaintNode(oldNode, viewCamera, effectiveBlocksPerTexel, overviewOnly, frameState->transparentVoid,
                                frameState->settings);
}
