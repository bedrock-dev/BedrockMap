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
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include "base/config.h"
#include "controller/mapcontroller.h"
#include "loguru/loguru.hpp"
#include "model/mapviewoptions.h"
#include "render/gpu_render_options.h"
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

    /// A baked region may be visually empty even when the loader reports Ready.
    /// Its height tile then contains only void codes, so it must remain in the
    /// empty set instead of replacing the dark placeholder with a gray tile.
    bool heightTileHasTerrain(const QImage& image) {
        if (image.isNull()) return false;
        for (int y = 0; y < image.height(); ++y) {
            const uchar* row = image.constScanLine(y);
            for (int x = 0; x < image.width() * 4; ++x) {
                if (row[x] != 0) return true;
            }
        }
        return false;
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
        const int current = requested_level_.load(std::memory_order_acquire);
        const int wanted = blocksPerTexelFor();
        if (wanted == current) {
            lod_candidate_level_ = wanted;
            return;
        }
        if (wanted != lod_candidate_level_) {
            lod_candidate_level_ = wanted;
            lod_settle_timer_.start();
            return;
        }
        commitLodRequest(wanted);
    });
    metrics_timer_.setInterval(250);
    connect(&metrics_timer_, &QTimer::timeout, this, &TerrainQuickItem::refreshMetrics);
    refreshRenderSettings();
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

void TerrainQuickItem::refreshMetrics() {
    refreshRenderSettings();
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
    update();
    if (auto* currentWindow = window()) {
        currentWindow->update();
        QTimer::singleShot(0, this, [this] {
            if (auto* queuedWindow = window()) queuedWindow->update();
        });
    }
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
    {
        const QMutexLocker lock(&tiles_mutex_);
        array_ready_tiles_.clear();
        sampled_overview_ = {};
        sampled_overview_chunks_ = {};
        sampled_overview_dimension_ = -1;
    }
    render_reset_generation_.fetch_add(1, std::memory_order_acq_rel);
    timings_->reset();
    if (has_data_) {
        has_data_ = false;
        emit hasDataChanged();
    }
    provider_ = provider;
    if (provider_) provider_->setTimings(metricsEnabled() ? timings_ : std::shared_ptr<RenderTimings>{});
    requested_level_.store(1, std::memory_order_release);
    lod_candidate_level_ = 1;
    lod_settle_timer_.stop();
    if (provider_) {
        connect(provider_, &TerrainDataProvider::overviewReady, this, [this](const QImage& image, const QRect& chunks, int dimension) {
            if (!image.isNull() && dimension != camera_.dimension) return;
            {
                const QMutexLocker lock(&tiles_mutex_);
                sampled_overview_ = image;
                sampled_overview_chunks_ = chunks;
                sampled_overview_dimension_ = dimension;
            }
            if (!image.isNull() && !has_data_) {
                has_data_ = true;
                emit hasDataChanged();
            }
            update();
        });
        connect(provider_, &TerrainDataProvider::tileReady, this,
                [this](int regionX, int regionZ, int dimension, int blocks_per_texel, int layer, const QImage& color, const QImage& height,
                       const QImage& material, const QImage& preparedColor, const QImage& preparedHeight, const QImage& preparedMaterial,
                       const QImage& overviewColor) {
                    // This runs on the GUI thread, where the provider drives the
                    // shared scheduler. Only the finished tiles cross over to the
                    // scene-graph render thread, which just uploads them.
                    const bool hasTerrain = heightTileHasTerrain(height);
                    if (hasTerrain && !has_data_) {
                        has_data_ = true;
                        emit hasDataChanged();
                    }
                    const bool overview_mode = !detailedTileMode();
                    const bool wanted_tile = overview_mode ? blocks_per_texel == terrain_render_config::overview_blocks_per_texel
                                                           : blocks_per_texel == provider_->blocksPerTexel();
                    if (wanted_tile)
                        queueArrayRegionPiece(regionX, regionZ, dimension, blocks_per_texel, layer, color, height, material, preparedColor,
                                              preparedHeight, preparedMaterial, overviewColor, !hasTerrain, !overview_mode);
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
                    queueArrayRegionPiece(regionX, regionZ, dimension, blocksPerTexel, layer, QImage(), QImage(), QImage(), QImage(),
                                          QImage(), QImage(), QImage(), true, true);
                    update();
                });
        syncViewState();
    }
    emit providerChanged();
    update();
    if (auto* currentWindow = window()) {
        currentWindow->update();
        QTimer::singleShot(0, this, [this] {
            if (auto* queuedWindow = window()) queuedWindow->update();
        });
    }
}

void TerrainQuickItem::syncViewState() {
    const RenderTimingScope timing(metricsEnabled() ? timings_.get() : nullptr, QStringLiteral("Camera / viewport GUI"));
    if (!controller_) return;

    const MapCameraState camera = controller_->cameraState();
    if (camera_ != camera) camera_ = camera;
    if (!provider_) return;
    provider_->setOverviewMode(!detailedTileMode(), requested_level_.load(std::memory_order_acquire));
    scheduleLodRequest();
    // Cover the viewport plus the shading margin, rather than a fixed tile radius:
    // the window has to follow the view, or a zoomed-out map stays mostly
    // placeholder. The level of detail picked for the frame keeps the visible
    // span inside the atlas; the budget keeps the resident set bounded.
    const qreal px_per_block = std::max<qreal>(0.0001, camera_.pixelsPerChunk / 16.0);
    const int requestLevel = requested_level_.load(std::memory_order_acquire);
    const double margin = std::max<double>(shadowReachBlocks(), std::max(requestLevel, 16));
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
        clampToRegionBudget(region_x0, region_x1, region_z0, region_z1,
                            static_cast<qint64>(terrain_render_config::max_requested_regions) * requestLevel * requestLevel);
    if (region_x1 < region_x0 || region_z1 < region_z0) return;
    const QRect regionWindow(region_x0, region_z0, region_x1 - region_x0 + 1, region_z1 - region_z0 + 1);
    const TerrainViewportRequest request{regionWindow, camera_.dimension, camera_.layer};
    // The layer can change without the window moving, so publish it on both paths.
    if (request_ == request) {
        publishViewport();
        publishFrameState();
        return;
    }
    request_ = request;

    // Publish the window to the provider: that call is also what makes the
    // shared scheduler bake, prune and prioritise for this viewport.
    publishViewport();
    publishFrameState();
}

void TerrainQuickItem::applySettings() {
    refreshRenderSettings();
    if (controller_) controller_->setScale(controller_->cameraState().pixelsPerChunk, QPointF(width() / 2, height() / 2));
    syncViewState();
    update();
}

void TerrainQuickItem::publishViewport() {
    if (!provider_) return;
    provider_->setViewport(request_);
}

void TerrainQuickItem::publishFrameState() {
    auto next = std::make_shared<TerrainFrameState>();
    next->camera = camera_;
    next->request = request_;
    next->requestedLevel = requested_level_.load(std::memory_order_acquire);
    next->overviewOnly = !detailedTileMode();
    next->transparentVoid = controller_ && controller_->transparentVoid();
    next->revision = ++frame_state_revision_;
    const QMutexLocker lock(&frame_state_mutex_);
    frame_state_ = std::move(next);
}

int TerrainQuickItem::blocksPerTexelFor() const {
    const qreal pxPerBlock = std::max<qreal>(0.0001, camera_.pixelsPerChunk / 16.0);
    int level = 1;
    while (level < terrain_render_config::max_blocks_per_texel && level * pxPerBlock < 1.0) level *= 2;
    return level;
}

bool TerrainQuickItem::detailedTileMode() const {
    return !provider_ || !provider_->overviewEnabled() || camera_.pixelsPerChunk >= constant::MINIMUM_TERRAIN_SCALE;
}

void TerrainQuickItem::scheduleLodRequest() {
    if (!provider_) return;
    const int current = requested_level_.load(std::memory_order_acquire);
    const int wanted = blocksPerTexelFor();
    if (wanted == current) {
        lod_candidate_level_ = wanted;
        lod_settle_timer_.stop();
        return;
    }
    // Coarser levels cover more of the current view. Waiting here exposes the
    // uncovered part of the old fine atlas as gray, so zoom-out switches now.
    // Finer levels still wait for a stable target before rebuilding.
    if (wanted > current) {
        lod_settle_timer_.stop();
        lod_candidate_level_ = wanted;
        commitLodRequest(wanted);
        return;
    }
    if (wanted != lod_candidate_level_ || !lod_settle_timer_.isActive()) {
        lod_candidate_level_ = wanted;
        lod_settle_timer_.start();
    }
}

void TerrainQuickItem::commitLodRequest(int level) {
    if (!provider_) return;
    requested_level_.store(level, std::memory_order_release);
    // Publish the new level's region budget before the provider resets its scan.
    syncViewState();
    provider_->setBlocksPerTexel(level);
    update();
}

int TerrainQuickItem::shadowReachBlocks() const {
    // Shadow reach in blocks for the current shader settings.
    const auto settings = renderSettings();
    return settings && settings->gpu.shadow_enabled && settings->gpu.shadow_strength > 0.0f ? settings->gpu.shadow_steps : 0;
}

void TerrainQuickItem::refreshRenderSettings() {
    const quint64 revision = setting::revision();
    if (render_settings_ && render_settings_revision_ == revision) return;
    auto next = std::make_shared<TerrainRenderSettingsSnapshot>();
    next->gpu = gpuRenderOptionsFromSettings();
    next->cpuShadowLevel = setting::current().cpuShadowLevel;
    {
        const QMutexLocker lock(&render_settings_mutex_);
        render_settings_ = std::move(next);
        render_settings_revision_ = revision;
    }
    update();
}

std::shared_ptr<const TerrainRenderSettingsSnapshot> TerrainQuickItem::renderSettings() const {
    const QMutexLocker lock(&render_settings_mutex_);
    return render_settings_;
}

void TerrainQuickItem::itemChange(ItemChange change, const ItemChangeData& value) {
    QQuickItem::itemChange(change, value);
    if (change == ItemSceneChange && window()) {
        update();
        window()->update();
        QTimer::singleShot(0, this, [this] {
            if (auto* currentWindow = window()) currentWindow->update();
        });
    }
}

void TerrainQuickItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        syncViewState();
        update();
    }
}

void TerrainQuickItem::queueArrayRegionPiece(int regionX, int regionZ, int dimension, int blocksPerTexel, int layer, const QImage& color,
                                             const QImage& height, const QImage& material, const QImage& preparedColor,
                                             const QImage& preparedHeight, const QImage& preparedMaterial, const QImage& overviewColor,
                                             bool empty, bool makeDetail) {
    const int bp = std::max(1, blocksPerTexel);
    const int regionIndexX = floorDiv(regionX, constant::RW);
    const int regionIndexZ = floorDiv(regionZ, constant::RW);
    Q_UNUSED(preparedColor);
    Q_UNUSED(preparedHeight);
    Q_UNUSED(preparedMaterial);
    Q_UNUSED(overviewColor);
    if (!makeDetail) return;
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
    {
        const QMutexLocker lock(&tiles_mutex_);
        array_ready_tiles_.append({key, piece(color), piece(height), piece(material), destination, empty});
    }
    update();
}

QSGNode* TerrainQuickItem::updateArrayPaintNode(QSGNode* oldNode, const MapCameraState& camera, int requestedLevel, bool overviewOnly,
                                               bool transparentVoid) {
    constexpr int tilePixels = 128;
    constexpr int gridSide = 256;
    constexpr int layerCount = terrain_render_config::tile_array_layers;
    auto* root = oldNode ? oldNode : new QSGNode;
    const quint64 resetGeneration = render_reset_generation_.load(std::memory_order_acquire);
    const bool rendererReset = resetGeneration != applied_render_reset_generation_;
    if (rendererReset) {
        array_overview_chunks_ = {};
        array_overview_cache_key_ = 0;
        applied_render_reset_generation_ = resetGeneration;
        // A null oldNode means Qt already destroyed the previous scene graph.
        // Cached node pointers then dangle and must only be reset, never freed.
        if (oldNode && array_node_) {
            root->removeChildNode(array_node_);
            delete array_node_;
            array_node_ = nullptr;
        }
        array_material_ = nullptr;
        array_color_texture_ = nullptr;
        array_height_texture_ = nullptr;
        array_material_texture_ = nullptr;
        array_page_table_texture_ = nullptr;
        array_overview_texture_ = nullptr;
        array_tile_layers_.clear();
        array_layer_last_used_.clear();
        array_retiring_layers_.clear();
        array_uploaded_tiles_.clear();
        array_terrain_tiles_.clear();
        array_free_layers_.clear();
        array_active_dimension_ = -1;
        array_active_layer_ = -1;
        array_page_table_signature_ = 0;
        array_page_table_layer_signatures_.fill(0);
        array_has_last_uniforms_ = false;
        array_geometry_size_ = QSizeF();
    }
    if (!oldNode) {
        array_overview_chunks_ = {};
        array_overview_cache_key_ = 0;
        array_node_ = nullptr;
        array_material_ = nullptr;
        array_color_texture_ = nullptr;
        array_page_table_texture_ = nullptr;
        array_overview_texture_ = nullptr;
        array_geometry_size_ = QSizeF();
        array_has_last_uniforms_ = false;
        array_page_table_signature_ = 0;
        array_page_table_layer_signatures_.fill(0);
        array_tile_layers_.clear();
        array_layer_last_used_.clear();
        array_retiring_layers_.clear();
        array_uploaded_tiles_.clear();
        array_terrain_tiles_.clear();
        array_free_layers_.clear();
        for (int i = 0; i < layerCount; ++i) array_free_layers_.push_back(i);
        array_has_last_uniforms_ = false;
    }
    if (camera.pixelsPerChunk <= 0 || width() <= 0 || height() <= 0) return root;

    const int nextLevel = std::max(1, requestedLevel);
    if (camera.dimension != array_active_dimension_ || camera.layer != array_active_layer_) {
        array_overview_chunks_ = {};
        array_overview_cache_key_ = 0;
        array_tile_layers_.clear();
        array_layer_last_used_.clear();
        array_retiring_layers_.clear();
        array_uploaded_tiles_.clear();
        array_terrain_tiles_.clear();
        array_free_layers_.clear();
        for (int i = 0; i < layerCount; ++i) array_free_layers_.push_back(i);
        array_active_dimension_ = camera.dimension;
        array_active_layer_ = camera.layer;
    }
    array_active_lod_ = nextLevel;

    if (!array_color_texture_) {
        if (array_free_layers_.isEmpty()) {
            for (int i = 0; i < layerCount; ++i) array_free_layers_.push_back(i);
        }
        array_color_texture_ = new TerrainAtlasTexture(QSize(tilePixels, tilePixels), layerCount);
        array_color_texture_->setFiltering(QSGTexture::Nearest);
        array_color_texture_->setTimings(timings_);
        array_height_texture_ = new TerrainAtlasTexture(QSize(tilePixels, tilePixels), layerCount);
        array_height_texture_->setFiltering(QSGTexture::Nearest);
        array_height_texture_->setTimings(timings_);
        array_material_texture_ = new TerrainAtlasTexture(QSize(tilePixels, tilePixels), layerCount);
        array_material_texture_->setFiltering(QSGTexture::Nearest);
        array_material_texture_->setTimings(timings_);
        for (auto& image : array_page_table_images_) {
            image = QImage(gridSide, gridSide, QImage::Format_RGBA8888);
            image.fill(0);
        }
        array_page_table_texture_ = new TerrainAtlasTexture(QSize(gridSide, gridSide), static_cast<int>(array_page_table_images_.size()));
        array_page_table_texture_->setFiltering(QSGTexture::Nearest);
        array_page_table_texture_->setTimings(timings_);
        QVector<TerrainAtlasUpload> initial;
        for (int lod = 0; lod < static_cast<int>(array_page_table_images_.size()); ++lod)
            initial.append({array_page_table_images_[lod], QPoint(0, 0), lod});
        array_page_table_texture_->queueUploads(std::move(initial));
        array_overview_texture_ =
            new TerrainAtlasTexture(QSize(terrain_render_config::overview_texture_pixels, terrain_render_config::overview_texture_pixels));
        array_overview_texture_->setFiltering(QSGTexture::Nearest);
        array_overview_texture_->setTimings(timings_);
        if (cpu_biome_palette_.isNull()) cpu_biome_palette_ = TerrainTiles::biomePaletteImage();
        auto* paletteTexture = new TerrainAtlasTexture(cpu_biome_palette_);
        paletteTexture->setFiltering(QSGTexture::Nearest);
        paletteTexture->setTimings(timings_);
        array_material_ = new TerrainArrayMaterial(array_color_texture_, array_height_texture_, array_material_texture_,
                                                   array_page_table_texture_, array_overview_texture_, paletteTexture);
        array_node_ = new QSGGeometryNode;
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);
        geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        geometry->setVertexDataPattern(QSGGeometry::StaticPattern);
        array_node_->setGeometry(geometry);
        array_node_->setFlag(QSGNode::OwnsGeometry, true);
        array_node_->setMaterial(array_material_);
        array_node_->setFlag(QSGNode::OwnsMaterial, true);
        root->appendChildNode(array_node_);
    }

    ++array_frame_counter_;
    for (auto it = array_retiring_layers_.begin(); it != array_retiring_layers_.end();) {
        if (array_frame_counter_ >= it.value() + 2) {
            array_free_layers_.push_back(it.key());
            it = array_retiring_layers_.erase(it);
        } else {
            ++it;
        }
    }

    {
        const QMutexLocker lock(&tiles_mutex_);
        if (sampled_overview_.isNull()) array_overview_chunks_ = {};
        if (sampled_overview_dimension_ == camera.dimension && !sampled_overview_.isNull() &&
            sampled_overview_.cacheKey() != array_overview_cache_key_) {
            array_overview_texture_->queueUploads({TerrainAtlasUpload{sampled_overview_, QPoint(0, 0), 0}});
            array_overview_chunks_ = sampled_overview_chunks_;
            array_overview_cache_key_ = sampled_overview_.cacheKey();
        }
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

    QVector<TerrainWorldTileReady> readyTiles;
    {
        const QMutexLocker lock(&tiles_mutex_);
        readyTiles.swap(array_ready_tiles_);
    }
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
        const QMutexLocker lock(&tiles_mutex_);
        QVector<TerrainWorldTileReady> combinedDetail = std::move(deferredDetail);
        combinedDetail += array_ready_tiles_;
        array_ready_tiles_ = std::move(combinedDetail);
        update();
    }
    QVector<TerrainAtlasUpload> tileUploads;
    QVector<TerrainAtlasUpload> heightUploads;
    QVector<TerrainAtlasUpload> materialUploads;
    QVector<TerrainWorldTileReady> layerDeferred;
    const auto takeLayer = [&]() -> int {
        if (!array_free_layers_.isEmpty()) return array_free_layers_.takeLast();
        auto victim = array_tile_layers_.end();
        quint64 oldest = std::numeric_limits<quint64>::max();
        for (auto it = array_tile_layers_.begin(); it != array_tile_layers_.end(); ++it) {
            if (it.key().dimension != array_active_dimension_ || it.key().layer != array_active_layer_ ||
                (isProtected(it.key()) && it.key().lod == nextLevel) || array_retiring_layers_.contains(it.value()))
                continue;
            const quint64 lastUsed = array_layer_last_used_.value(it.value(), 0);
            if (lastUsed < oldest) {
                oldest = lastUsed;
                victim = it;
            }
        }
        if (victim == array_tile_layers_.end()) return -1;
        const int retiredLayer = victim.value();
        array_uploaded_tiles_.remove(victim.key());
        array_terrain_tiles_.remove(victim.key());
        array_tile_layers_.erase(victim);
        if (retiredLayer < 0) return -1;
        array_layer_last_used_.remove(retiredLayer);
        array_retiring_layers_.insert(retiredLayer, array_frame_counter_);
        return -1;
    };
    for (auto& tile : readyTiles) {
        if (tile.key.dimension != array_active_dimension_ || tile.key.layer != array_active_layer_) continue;
        if (tile.empty && tile.key.lod > 1 && !array_tile_layers_.contains(tile.key)) continue;
        if (tile.empty && tile.key.lod == 1) {
            auto existing = array_tile_layers_.find(tile.key);
            if (existing != array_tile_layers_.end() && existing.value() >= 0) {
                array_retiring_layers_.insert(existing.value(), array_frame_counter_);
                array_layer_last_used_.remove(existing.value());
            }
            array_tile_layers_[tile.key] = -1;
            array_uploaded_tiles_.insert(tile.key);
            array_terrain_tiles_.remove(tile.key);
            continue;
        }
        auto assigned = array_tile_layers_.constFind(tile.key);
        if (assigned != array_tile_layers_.cend() && assigned.value() < 0) {
            array_tile_layers_.erase(array_tile_layers_.find(tile.key));
            assigned = array_tile_layers_.cend();
        }
        bool first = false;
        if (assigned == array_tile_layers_.cend()) {
            const int layer = takeLayer();
            if (layer < 0) {
                layerDeferred.append(std::move(tile));
                continue;
            }
            array_tile_layers_.insert(tile.key, layer);
            assigned = array_tile_layers_.constFind(tile.key);
            first = true;
        }
        array_layer_last_used_[assigned.value()] = array_frame_counter_;
        if (first) {
            QImage clear(tilePixels, tilePixels, QImage::Format_RGBA8888);
            clear.fill(0);
            tileUploads.append({clear, QPoint(0, 0), assigned.value()});
            heightUploads.append({clear, QPoint(0, 0), assigned.value()});
            materialUploads.append({clear, QPoint(0, 0), assigned.value()});
        }
        if (!tile.empty) array_terrain_tiles_.insert(tile.key);
        tileUploads.append({std::move(tile.color), tile.destination, assigned.value()});
        heightUploads.append({std::move(tile.height), tile.destination, assigned.value()});
        materialUploads.append({std::move(tile.material), tile.destination, assigned.value()});
        array_uploaded_tiles_.insert(tile.key);
    }
    if (!layerDeferred.isEmpty()) {
        const QMutexLocker lock(&tiles_mutex_);
        QVector<TerrainWorldTileReady> combined = std::move(layerDeferred);
        combined += array_ready_tiles_;
        array_ready_tiles_ = std::move(combined);
        update();
    }
    const int uploadedTileCount = static_cast<int>(tileUploads.size());
    if (!tileUploads.isEmpty()) array_color_texture_->queueUploads(std::move(tileUploads));
    if (!heightUploads.isEmpty()) array_height_texture_->queueUploads(std::move(heightUploads));
    if (!materialUploads.isEmpty()) array_material_texture_->queueUploads(std::move(materialUploads));

    for (auto& image : array_page_table_images_) image.fill(0);
    quint64 signature = 1469598103934665603ull;
    std::array<quint64, 8> layerSignatures{};
    layerSignatures.fill(1469598103934665603ull);
    for (auto it = array_tile_layers_.cbegin(); it != array_tile_layers_.cend(); ++it) {
        if (it.key().dimension != array_active_dimension_ || it.key().layer != array_active_layer_ ||
            !array_uploaded_tiles_.contains(it.key()) || !isProtected(it.key()))
            continue;
        int lodIndex = 0;
        for (int level = it.key().lod; level > 1; level >>= 1) ++lodIndex;
        const int x = ((it.key().x % gridSide) + gridSide) % gridSide;
        const int z = ((it.key().z % gridSide) + gridSide) % gridSide;
        uchar* row = array_page_table_images_[lodIndex].scanLine(z);
        row[x * 4] = static_cast<uchar>(it.value() & 0xFF);
        row[x * 4 + 1] = static_cast<uchar>((it.value() >> 8) & 0xFF);
        const uint32_t hashValue = [&] {
            uint32_t h = static_cast<uint16_t>(it.key().x) * 40503u;
            h ^= static_cast<uint16_t>(it.key().z) * 60307u;
            h ^= h >> 8;
            h &= 0x3FFFu;
            return h == 0 ? 1u : h;
        }();
        const bool hasTerrain = array_terrain_tiles_.contains(it.key());
        row[x * 4 + 2] = static_cast<uchar>(0x80u | (hasTerrain ? 0u : 0x40u) | ((hashValue >> 8) & 0x3Fu));
        row[x * 4 + 3] = static_cast<uchar>(hashValue & 0xFFu);
        array_layer_last_used_[it.value()] = array_frame_counter_;
        const quint64 entryHash =
            (static_cast<quint64>(it.value()) << 32) ^ static_cast<quint64>((x << 16) | (z & 0xFFFF) | (hashValue << 8));
        layerSignatures[lodIndex] = (layerSignatures[lodIndex] ^ entryHash) * 1099511628211ull;
        signature = (signature ^ entryHash) * 1099511628211ull;
    }
    if (signature != array_page_table_signature_ || layerSignatures != array_page_table_layer_signatures_) {
        QVector<TerrainAtlasUpload> uploads;
        for (int lod = 0; lod < 8; ++lod) {
            if (layerSignatures[lod] == array_page_table_layer_signatures_[lod]) continue;
            uploads.append({array_page_table_images_[lod], QPoint(0, 0), lod});
        }
        if (!uploads.isEmpty()) array_page_table_texture_->queueUploads(std::move(uploads));
        array_page_table_signature_ = signature;
        array_page_table_layer_signatures_ = layerSignatures;
    }

    if (!array_geometry_size_.isValid() || array_geometry_size_ != QSizeF(width(), height())) {
        auto* vertices = static_cast<QSGGeometry::TexturedPoint2D*>(array_node_->geometry()->vertexData());
        vertices[0].set(0.0f, 0.0f, 0.0f, 0.0f);
        vertices[1].set(static_cast<float>(width()), 0.0f, 1.0f, 0.0f);
        vertices[2].set(0.0f, static_cast<float>(height()), 0.0f, 1.0f);
        vertices[3].set(static_cast<float>(width()), static_cast<float>(height()), 1.0f, 1.0f);
        array_node_->geometry()->markVertexDataDirty();
        array_node_->markDirty(QSGNode::DirtyGeometry);
        array_geometry_size_ = QSizeF(width(), height());
    }
    TerrainArrayUniformBody body;
    body.void_options[0] = transparentVoid ? 1.0f : 0.0f;
    body.checker = {float(checker_color_.redF()), float(checker_color_.greenF()), float(checker_color_.blueF()), 1.0f};
    body.checker_alternate = {float(checker_alternate_color_.redF()), float(checker_alternate_color_.greenF()),
                              float(checker_alternate_color_.blueF()), 1.0f};
    body.view = {static_cast<float>(worldX), static_cast<float>(worldZ), static_cast<float>(spanX), static_cast<float>(spanZ)};
    int lodIndex = 0;
    for (int level = array_active_lod_; level > 1; level >>= 1) ++lodIndex;
    body.params = {static_cast<float>(tileWorld), static_cast<float>(gridSide), static_cast<float>(lodIndex),
                   static_cast<float>(std::max(1, array_active_lod_))};
    body.tint[3] = overviewOnly ? 1.0f : 0.0f;
    body.overview = {float(array_overview_chunks_.x() * 16), float(array_overview_chunks_.y() * 16),
                     float(array_overview_chunks_.width() * 16), float(array_overview_chunks_.height() * 16)};
    if (const auto settings = renderSettings()) {
        const auto& gpu = settings->gpu;
        int sunX = -1;
        int sunZ = -1;
        gpuSunStep(sunX, sunZ);
        const int shadowReach = gpu.shadow_enabled && gpu.shadow_strength > 0.0f ? gpu.shadow_steps : 0;
        const qreal devicePxPerBlock = pxPerBlock * (window() ? window()->effectiveDevicePixelRatio() : 1.0);
        const qreal texelPixels = devicePxPerBlock * std::max(1, array_active_lod_);
        const float baseEdgeWidth = static_cast<float>(
            std::clamp(std::max<qreal>(static_cast<qreal>(0.25), 1.0 / std::max<qreal>(texelPixels, static_cast<qreal>(0.01))),
                       static_cast<qreal>(0.25), static_cast<qreal>(0.5)));
        body.shade = {static_cast<float>(sunX), static_cast<float>(sunZ), static_cast<float>(shadowReach), gpu.shadow_step};
        body.shadow = {static_cast<float>(1.0 - std::clamp(settings->cpuShadowLevel, 0, 255) / 255.0 * 0.75), gpu.shadow_strength,
                       std::clamp(baseEdgeWidth * gpu.bevel_width, 0.0625f, 0.75f), gpu.biome_blend_blocks};
        body.ao = {gpu.ao_strength, static_cast<float>(gpu.ao_directions), static_cast<float>(gpu.ao_steps), gpu.ao_radius};
        body.tint = {gpu.saturation, gpu.brightness, gpu.bevel_strength, overviewOnly ? 1.0f : 0.0f};
        body.water = {gpu.water_base_color[0], gpu.water_base_color[1], gpu.water_base_color[2], 0.0f};
        body.grass = {gpu.grass_height_base, gpu.grass_height_range, gpu.grass_height_enabled ? 1.0f : 0.0f, gpu.biome_blend_blocks};
        body.grass_color = {gpu.grass_height_color[0], gpu.grass_height_color[1], gpu.grass_height_color[2], 0.0f};
    }
    if (!array_has_last_uniforms_ || std::memcmp(&array_last_uniforms_, &body, sizeof(body)) != 0) {
        array_last_uniforms_ = body;
        array_has_last_uniforms_ = true;
        array_material_->setUniforms(body);
        array_node_->markDirty(QSGNode::DirtyMaterial);
    }
    if (array_color_texture_->hasPendingUploads() || array_height_texture_->hasPendingUploads() ||
        array_material_texture_->hasPendingUploads() || array_page_table_texture_->hasPendingUploads() ||
        array_overview_texture_->hasPendingUploads())
        update();
    TerrainFrameMetrics metrics;
    metrics.blocksPerTexel = array_active_lod_;
    metrics.atlasSlotsPerSide = gridSide;
    metrics.uploadedTiles = uploadedTileCount;
    for (auto it = array_tile_layers_.cbegin(); it != array_tile_layers_.cend(); ++it) {
        if (it.key().dimension != array_active_dimension_ || it.key().layer != array_active_layer_) continue;
        if (array_terrain_tiles_.contains(it.key()))
            ++metrics.terrainSlots;
        else if (array_uploaded_tiles_.contains(it.key()))
            ++metrics.emptySlots;
    }
    metrics.loadingSlots = std::max(0, static_cast<int>(array_tile_layers_.size()) - metrics.terrainSlots - metrics.emptySlots);
    publishMetrics(metrics);
    if (metricsEnabled()) {
        frame_uploaded_tiles_.fetch_add(metrics.uploadedTiles, std::memory_order_relaxed);
        frame_upload_lod_.store(array_active_lod_, std::memory_order_relaxed);
    }
    render_state_->publish(camera.center.x(), camera.center.y(), camera.pixelsPerChunk, camera.dimension);
    return root;
}

QSGNode* TerrainQuickItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    const RenderTimingScope timing(metricsEnabled() ? timings_.get() : nullptr, QStringLiteral("Map update"));
    auto* root = oldNode ? oldNode : new QSGNode;
    std::shared_ptr<const TerrainFrameState> frameState;
    {
        const QMutexLocker lock(&frame_state_mutex_);
        frameState = frame_state_;
    }
    if (!frameState) return root;
    const MapCameraState& viewCamera = frameState->camera;
    const int viewRequestedLevel = frameState->requestedLevel;
    const bool overviewOnly = frameState->overviewOnly;
    const int effectiveLevel = overviewOnly ? terrain_render_config::overview_blocks_per_texel : viewRequestedLevel;
    return updateArrayPaintNode(oldNode, viewCamera, effectiveLevel, overviewOnly, frameState->transparentVoid);
}
