#include "controller/mapcontroller.h"

#include <QClipboard>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QMimeData>
#include <QTimer>
#include <QtConcurrent>
#include <cmath>
#include <exception>

#include "base/clipboarddata.h"
#include "bedrock_key.h"
#include "color.h"
#include "loader/asynclevelloader.h"
#include "loader/chunkoperator.h"
#include "loader/chunkregion.h"
#include "magic-enum/magic_enum.hpp"

MapController::MapController(MapSession* session, QObject* parent)
    : QObject(parent), session_(session), region_import_(this) {
    connect(&operation_watcher_, &QFutureWatcher<OperationResult>::finished, this, &MapController::finishSelectionOperation);
    connect(&region_import_, &RegionImportController::failed, this, &MapController::operationFailed);
    if (!session_) return;
    connect(session_->view(), &MapView::dimChanged, this, [this] { emit dimensionChanged(); });
    connect(session_->view(), &MapView::viewChanged, this, &MapController::stateChanged);
    connect(session_->view(), &MapView::selectionChanged, this, &MapController::selectionChanged);
}

MapController::~MapController() { waitForOperation(); }

int MapController::dimension() const { return view() ? view()->dim() : 0; }

int MapController::layer() const { return view() ? static_cast<int>(view()->options().layer) : 0; }

bool MapController::transparentVoid() const { return loader_ && loader_->transparentVoid(); }

void MapController::setTransparentVoid(bool enabled) {
    if (!loader_ || loader_->transparentVoid() == enabled) return;
    loader_->setTransparentVoid(enabled);
    // Quick terrain tiles retain void heights, so switching only needs a redraw.
    emit transparentVoidChanged();
    emit stateChanged();
}

MapCameraState MapController::cameraState() const {
    return view() ? view()->cameraState() : MapCameraState{};
}

bool MapController::hasSelection() const { return view() && !view()->selection().isEmpty(); }

int MapController::selectionMode() const {
    const auto* map_view = view();
    return map_view ? static_cast<int>(map_view->selection().mode()) : 0;
}

int MapController::selectionChunkCount() const {
    const auto* map_view = view();
    return map_view ? map_view->selection().chunkCount() : 0;
}

int MapController::selectionRectCount() const {
    const auto* map_view = view();
    return map_view ? static_cast<int>(map_view->selection().rectCount()) : 0;
}

bool MapController::selectionDragging() const {
    const auto* map_view = view();
    return map_view && map_view->selection().isDragging();
}

QVector<QRect> MapController::selectionRects() const {
    QVector<QRect> result;
    const auto* map_view = view();
    if (!map_view) return result;
    for (const auto& rect : map_view->selection().region()) result.append(rect);
    return result;
}

QRect MapController::selectionDragRect() const {
    const auto* map_view = view();
    return map_view && map_view->selection().isDragging() ? map_view->selection().dragRect() : QRect();
}

bool MapController::selectionContainsChunkAt(const QPointF& view_position) const {
    const auto* map_view = view();
    if (!map_view || map_view->selection().isEmpty()) return false;
    const auto chunk = map_view->chunkPosAt(view_position, map_view->viewportSize());
    return map_view->selection().region().contains(QPoint(chunk.x, chunk.z));
}

QVariantMap MapController::blockInfoAt(const QPointF& view_position) const {
    QVariantMap result;
    const auto* map_view = view();
    if (!map_view) return result;
    const QPoint block = map_view->blockPosAt(view_position, map_view->viewportSize());
    const int dimension = map_view->dim();
    result.insert(QStringLiteral("blockX"), block.x());
    result.insert(QStringLiteral("blockZ"), block.y());
    if (!loader_ || !loader_->isOpen()) return result;

    const bl::block_pos pos(block.x(), 0, block.y());
    const std::string block_name = loader_->getBlockName(pos, dimension);
    const auto tips = loader_->getBlockTips(pos, dimension);
    result.insert(QStringLiteral("blockName"), QString::fromStdString(block_name));
    result.insert(QStringLiteral("biomeName"), QString::fromStdString(bl::get_biome_name(tips.biome)));
    result.insert(QStringLiteral("biomeId"), static_cast<int>(tips.biome));
    result.insert(QStringLiteral("altitude"), tips.height);
    result.insert(QStringLiteral("tpCommand"),
                  QStringLiteral("tp @s %1 ~ %2").arg(QString::number(block.x()), QString::number(block.y())));
    return result;
}

QVariantMap MapController::voxelViewArea() const {
    QVariantMap result;
    const auto* map_view = view();
    if (!map_view) return result;

    QRect area = map_view->selection().region().boundingRect();
    if (area.isEmpty()) {
        const auto [min_chunk, max_chunk, unused] = map_view->renderRange();
        Q_UNUSED(unused);
        area = QRect(QPoint(min_chunk.x, min_chunk.z), QPoint(max_chunk.x, max_chunk.z));
    }
    if (area.isEmpty()) return result;

    // Bound the interactive preview so a zoomed-out viewport cannot enqueue
    // hundreds of chunks at once. Selection rectangles are normally far below
    // this limit and are preserved whenever they already fit.
    constexpr int max_side = 16;
    if (area.width() > max_side) {
        const int center = area.center().x();
        area.setLeft(center - max_side / 2);
        area.setRight(area.left() + max_side - 1);
    }
    if (area.height() > max_side) {
        const int center = area.center().y();
        area.setTop(center - max_side / 2);
        area.setBottom(area.top() + max_side - 1);
    }
    result.insert(QStringLiteral("minX"), area.left());
    result.insert(QStringLiteral("minZ"), area.top());
    result.insert(QStringLiteral("maxX"), area.right());
    result.insert(QStringLiteral("maxZ"), area.bottom());
    result.insert(QStringLiteral("dimension"), map_view->dim());
    return result;
}

bool MapController::worldLoaded() const { return loader_ && loader_->isOpen(); }

void MapController::requestRegionTile(int regionX, int regionZ, int dimension) {
    if (!loader_ || !loader_->isOpen()) return;
    const region_pos region{regionX, regionZ, dimension};
    // regionState() only inspects the cache and asks the existing scheduler to
    // enqueue a miss. The actual bake and LevelDB work remain asynchronous.
    const auto state = loader_->regionState(region, nullptr);
    if (state != AsyncLevelLoader::RegionState::Unloaded) emit regionTileReady(regionX, regionZ, dimension);
}

void MapController::setLevelLoader(AsyncLevelLoader* loader) {
    if (loader_ == loader) return;
    region_import_.cancel();
    if (loader_) disconnect(loader_, nullptr, this, nullptr);
    loader_ = loader;
    if (loader_) {
        connect(loader_, &AsyncLevelLoader::regionReady, this, [this] {
            if (tile_refresh_pending_) return;
            tile_refresh_pending_ = true;
            QTimer::singleShot(100, this, [this] {
                tile_refresh_pending_ = false;
                ++tile_revision_;
                emit tileRevisionChanged();
                emit worldStateChanged();
            });
        });
        connect(loader_, &AsyncLevelLoader::chunkCoordsPreloadFinished, this, &MapController::worldStateChanged);
        connect(loader_, &AsyncLevelLoader::regionTileReady, this,
                [this](region_pos region) { emit regionTileReady(region.x, region.z, region.dim); });
    }
    emit transparentVoidChanged();
    emit worldStateChanged();
}

void MapController::applyZoomLimits() {
    const bool overview = loader_ && loader_->chunkOverviewEnabled() && setting::current().preloadAllChunkCoords &&
                          setting::current().chunkOverviewEnabled;
    if (auto* map_view = view()) map_view->applyTerrainZoomLimits(overview);
}

void MapController::setViewportSize(const QSize& size) {
    applyZoomLimits();
    if (auto* map_view = view()) {
        map_view->setViewportSize(size);
        if (loader_) {
            const auto [min_region, max_region, unused] = map_view->renderRange();
            Q_UNUSED(unused);
            loader_->setRenderViewport(constant::c2r(min_region), constant::c2r(max_region));
        }
    }
}

void MapController::setDimension(int dimension) {
    if (dimension != this->dimension()) {
        if (operation_running_) return;
        region_import_.cancel();
    }
    if (auto* map_view = view()) map_view->setDim(dimension);
}

void MapController::setScale(qreal scale_value, const QPointF& anchor) {
    applyZoomLimits();
    if (auto* map_view = view()) map_view->setScale(scale_value, anchor);
}

void MapController::zoomBy(qreal factor, const QPointF& anchor) {
    applyZoomLimits();
    if (auto* map_view = view()) map_view->zoomBy(factor, anchor);
}

void MapController::transformGesture(qreal factor, const QPointF& previous, const QPointF& position) {
    if (!std::isfinite(factor) || factor <= 0.0 || !std::isfinite(previous.x()) || !std::isfinite(previous.y()) ||
        !std::isfinite(position.x()) || !std::isfinite(position.y())) return;
    auto* map_view = view();
    if (!map_view) return;
    applyZoomLimits();
    const qreal scale = map_view->scale();
    if (!std::isfinite(scale) || scale <= 0.0) return;
    // Move the old centroid onto the new one before zooming around it. Publish
    // only the combined transform so the renderer cannot observe half a pinch.
    map_view->translate((position - previous) / scale, false);
    map_view->zoomBy(factor, position);
}

void MapController::zoomAdjacent(int direction, const QPointF& anchor) {
    applyZoomLimits();
    if (auto* map_view = view()) map_view->zoomToAdjacentLevel(direction, anchor);
}

void MapController::pan(qreal dx, qreal dy) {
    if (auto* map_view = view()) {
        const qreal scale = std::abs(map_view->scale());
        if (scale > 0.0) {
            map_view->translate(QPointF(dx, dy) / scale, false);
            emit cameraMoved();
        }
    }
}

void MapController::gotoBlock(int x, int z) {
    auto* map_view = view();
    if (!map_view || qFuzzyIsNull(map_view->scaleLevel())) return;
    const QPointF view_pos = map_view->blockPosToViewPos(bl::block_pos(x, 0, z));
    const QPointF delta = (QPointF(map_view->camera().center()) - view_pos) / std::abs(map_view->scaleLevel());
    map_view->translate(delta);
}

void MapController::setLayer(int layer) {
    if (auto* map_view = view()) map_view->setLayer(static_cast<RenderOption::LayerType>(layer));
}

void MapController::setOther(int other, bool enabled) {
    if (auto* map_view = view()) map_view->setOther(static_cast<RenderOption::OtherType>(other), enabled);
}

bool MapController::toggleOther(int other) {
    if (!session_) return false;
    return session_->toggleOther(static_cast<RenderOption::OtherType>(other));
}

void MapController::setSelectionMode(int mode) {
    if (auto* map_view = view()) {
        map_view->setSelectionMode(static_cast<SelectionController::Mode>(mode));
        emit selectionModeChanged();
    }
}

void MapController::clearSelection() {
    if (auto* map_view = view()) map_view->clearSelection();
}

void MapController::beginSelectionDragAt(const QPointF& view_position) {
    if (region_import_.active() || region_import_.loading() || operation_running_) return;
    auto* map_view = view();
    if (!map_view) return;
    map_view->beginSelectionDrag(map_view->chunkPosAt(view_position, map_view->viewportSize()));
}

void MapController::updateSelectionDragAt(const QPointF& view_position) {
    auto* map_view = view();
    if (!map_view || !map_view->selection().isDragging()) return;
    map_view->updateSelectionDrag(map_view->chunkPosAt(view_position, map_view->viewportSize()));
}

void MapController::finishSelectionDrag() {
    if (auto* map_view = view(); map_view && map_view->selection().isDragging()) map_view->finishSelectionDrag();
}

void MapController::cancelSelectionDrag() {
    if (auto* map_view = view(); map_view && map_view->selection().isDragging()) map_view->cancelSelectionDrag();
}

bool MapController::deleteSelectionAsync() {
    return startSelectionOperation([](AsyncLevelLoader& loader, const QRegion& region, int dimension) {
        const bool success = ChunkOperator::deleteRegion(region, loader, dimension, false);
        return OperationResult{success, true, success ? QString() : QStringLiteral("Some selected chunks could not be deleted."), {}};
    });
}

bool MapController::createVoidSelectionAsync() {
    return startSelectionOperation([](AsyncLevelLoader& loader, const QRegion& region, int dimension) {
        const bool success = ChunkOperator::createVoid(region, loader, dimension, false);
        return OperationResult{success, true, success ? QString() : QStringLiteral("Some selected chunks could not be cleared."), {}};
    });
}

bool MapController::setSelectionBiomeAsync(int biome) {
    if (!magic_enum::enum_contains<bl::biome>(biome) || biome == static_cast<int>(bl::biome::none)) return false;
    const auto selected_biome = static_cast<bl::biome>(biome);
    return startSelectionOperation([selected_biome](AsyncLevelLoader& loader, const QRegion& region, int dimension) {
        const bool success = ChunkOperator::setRegionBiome(region, loader, selected_biome, dimension, false);
        return OperationResult{success, true, success ? QString() : QStringLiteral("The biome could not be applied to every selected chunk."), {}};
    });
}

bool MapController::copySelectionAsync() {
    if (!QGuiApplication::clipboard()) {
        emit operationFailed(tr("The clipboard is not available."));
        return false;
    }
    return startSelectionOperation([](AsyncLevelLoader& loader, const QRegion& region, int dimension) {
        ExportedRegion exported;
        for (const auto& rect : region) {
            for (int x = rect.x(); x < rect.x() + rect.width(); ++x) {
                for (int z = rect.y(); z < rect.y() + rect.height(); ++z) {
                    const auto raw = loader.getRawChunk(bl::chunk_pos(x, z, dimension));
                    if (raw) exported.addChunk(*raw);
                }
            }
        }
        if (exported.isEmpty()) return OperationResult{false, false, MapController::tr("No chunks were found in the selection."), {}};
        const auto bytes = exported.serialize();
        return OperationResult{true, false, {},
                               QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size()))};
    });
}

bool MapController::exportSelectionAsync(const QUrl& destination) {
    QString path = destination.isLocalFile() ? destination.toLocalFile() : destination.toString();
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().compare(QStringLiteral("bchks"), Qt::CaseInsensitive) != 0) path += QStringLiteral(".bchks");
    return startSelectionOperation([path = std::move(path)](AsyncLevelLoader& loader, const QRegion& region, int dimension) {
        const bool success = ChunkOperator::exportRegion(region, path, loader, dimension);
        return OperationResult{success, false, success ? QString() : QStringLiteral("The selected chunks could not be exported."), {}};
    });
}

bool MapController::beginImportAsync(const QUrl& source, int chunkX, int chunkZ) {
    if (!loader_ || !loader_->isOpen() || operation_running_ || editing_blocked_) return false;
    cancelSelectionDrag();
    return region_import_.loadFile(source, QPoint(chunkX, chunkZ), dimension());
}

bool MapController::beginPasteAsync(int chunkX, int chunkZ) {
    if (!loader_ || !loader_->isOpen() || operation_running_ || editing_blocked_ ||
        region_import_.active() || region_import_.loading()) return false;
    auto* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        emit operationFailed(tr("The clipboard is not available."));
        return false;
    }
    // Capture the clipboard on the GUI thread. The worker owns this snapshot,
    // so later clipboard changes cannot affect an in-progress placement.
    const auto* mime_data = clipboard->mimeData(QClipboard::Clipboard);
    QByteArray bytes = clipboard_data::readRegion(mime_data);
    if (bytes.isEmpty()) {
        emit operationFailed(tr("The clipboard does not contain any copied chunks."));
        return false;
    }
    cancelSelectionDrag();
    return region_import_.loadBytes(std::move(bytes), tr("Clipboard"), QPoint(chunkX, chunkZ), dimension());
}

bool MapController::beginImportDragAt(const QPointF& position) {
    const auto* map_view = view();
    if (!map_view || operation_running_ || editing_blocked_) return false;
    return region_import_.beginDrag(map_view->transformForViewport(map_view->viewportSize()).inverted().map(position));
}

void MapController::updateImportDragAt(const QPointF& position) {
    const auto* map_view = view();
    if (!map_view || operation_running_ || editing_blocked_) return;
    region_import_.updateDrag(map_view->transformForViewport(map_view->viewportSize()).inverted().map(position));
}

void MapController::endImportDrag() { region_import_.endDrag(); }

void MapController::cancelImport() {
    if (!operation_running_) region_import_.cancel();
}

bool MapController::confirmImport() {
    if (!region_import_.active() || region_import_.loading() || region_import_.dimension() != dimension()) return false;
    region_import_.endDrag();
    const auto source = region_import_.source();
    const QPoint offset = region_import_.offset();
    return startRegionOperation([source, offset](AsyncLevelLoader& loader, const QRegion&, int dimension) {
        try {
            // Always relocate a copy. Canceled placement and retries preserve the
            // source coordinates, entity IDs and NBT exactly as loaded.
            ExportedRegion imported = *source;
            for (auto& chunk : imported.chunks()) {
                const auto pos = chunk.pos();
                chunk.move_to(bl::chunk_pos(pos.x + offset.x(), pos.z + offset.y(), dimension), &loader.level());
            }
            const bool success = ChunkOperator::importRegion(imported, loader, false);
            return OperationResult{success, true,
                                   success ? QString() : MapController::tr("Some imported chunks could not be written."), {}, true};
        } catch (const std::exception& error) {
            return OperationResult{false, true, MapController::tr("The region could not be imported: %1")
                                                       .arg(QString::fromUtf8(error.what())), {}, true};
        } catch (...) {
            return OperationResult{false, true, MapController::tr("The region could not be imported."), {}, true};
        }
    }, region_import_.region(), region_import_.dimension());
}

QVariantList MapController::biomeOptions() const {
    QVariantList result;
    magic_enum::enum_for_each<bl::biome>([&](auto value) {
        constexpr auto entry = value;
        if constexpr (entry != bl::biome::none) {
            QVariantMap item;
            item.insert(QStringLiteral("value"), static_cast<int>(entry));
            item.insert(QStringLiteral("name"), QString::fromStdString(bl::get_biome_name(entry)));
            result.append(item);
        }
    });
    return result;
}

bool MapController::startSelectionOperation(SelectionOperation operation) {
    auto* map_view = view();
    if (!map_view || map_view->selection().isEmpty() || region_import_.active() || region_import_.loading()) return false;

    const QRegion region = map_view->selection().region();
    const int dimension = map_view->dim();
    return startRegionOperation(std::move(operation), region, dimension);
}

bool MapController::startRegionOperation(SelectionOperation operation, const QRegion& region, int dimension) {
    if (!operation || operation_running_ || editing_blocked_ || !loader_ || !loader_->isOpen() || region.isEmpty()) return false;
    operation_region_ = region;
    operation_dimension_ = dimension;
    operation_running_ = true;
    emit operationChanged();
    operation_watcher_.setFuture(QtConcurrent::run([loader = loader_, region, dimension, operation = std::move(operation)]() mutable {
        try {
            return operation(*loader, region, dimension);
        } catch (const std::exception& error) {
            return OperationResult{false, false, QString::fromUtf8(error.what()), {}};
        } catch (...) {
            return OperationResult{false, false, QStringLiteral("The map operation failed."), {}};
        }
    }));
    return true;
}

void MapController::finishSelectionOperation() {
    const auto result = operation_watcher_.result();
    operation_running_ = false;
    emit operationChanged();

    if (result.refresh_terrain && loader_ && !operation_region_.isEmpty()) {
        // The edit ran on a worker. Drop the old region tiles here, before the
        // provider starts refilling, or cached centre tiles would be delivered
        // again and only uncached surrounding regions would show the edit.
        loader_->invalidateRegionTiles(operation_region_, operation_dimension_);
        emit mapDataChanged(operation_region_, operation_dimension_);
    }
    const QRegion completed_region = operation_region_;
    operation_region_ = {};
    if (!result.success) {
        emit operationFailed(result.error.isEmpty() ? QStringLiteral("The map operation failed.") : result.error);
        return;
    }
    if (!result.clipboard.isEmpty()) {
        auto* clipboard = QGuiApplication::clipboard();
        if (!clipboard) {
            emit operationFailed(tr("The clipboard is not available."));
            return;
        }
        auto* mime_data = new QMimeData();
        clipboard_data::writeRegion(*mime_data, result.clipboard);
        clipboard->setMimeData(mime_data, QClipboard::Clipboard);
    }
    if (result.imported) {
        region_import_.cancel();
        if (auto* map_view = view()) map_view->setSelectionRegion(completed_region);
    }
    emit operationFinished();
}

void MapController::waitForOperation() {
    region_import_.cancel();
    region_import_.waitForLoad();
    if (operation_watcher_.isRunning()) operation_watcher_.waitForFinished();
}

void MapController::copyText(const QString& text) const {
    if (auto* clipboard = QGuiApplication::clipboard()) clipboard->setText(text);
}

MapView* MapController::view() const { return session_ ? session_->view() : nullptr; }
