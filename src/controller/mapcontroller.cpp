#include "controller/mapcontroller.h"

#include <QClipboard>
#include <QColor>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStringList>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <exception>

#include "base/appcontext.h"
#include "base/binaryclipboard.h"
#include "bedrock_key.h"
#include "color.h"
#include "loader/asynclevelloader.h"
#include "loader/chunkoperator.h"
#include "loader/exportedregion.h"
#include "magic-enum/magic_enum.hpp"

namespace {
    QVariantMap mapFilterOptions(const MapFilter& filter) {
        QStringList blocks;
        for (const auto& block : filter.blocks_list_) blocks.append(QString::fromStdString(block));
        blocks.sort(Qt::CaseInsensitive);

        QStringList actors;
        for (const auto& actor : filter.actors_list_) actors.append(QString::fromStdString(actor));
        actors.sort(Qt::CaseInsensitive);

        QList<int> biomes;
        for (const int biome : filter.biomes_list_) biomes.append(biome);
        std::sort(biomes.begin(), biomes.end());
        QVariantList biome_values;
        for (const int biome : biomes) biome_values.append(biome);

        QVariantMap result;
        result.insert(QStringLiteral("blockBlackMode"), filter.block_black_mode_);
        result.insert(QStringLiteral("blockList"), blocks);
        result.insert(QStringLiteral("biomeBlackMode"), filter.biome_black_mode_);
        result.insert(QStringLiteral("biomeList"), biome_values);
        result.insert(QStringLiteral("actorBlackMode"), filter.actor_black_mode_);
        result.insert(QStringLiteral("actorList"), actors);
        result.insert(QStringLiteral("layerEnabled"), filter.enable_layer_);
        result.insert(QStringLiteral("layer"), filter.layer);
        return result;
    }

    std::unordered_set<std::string> stringSet(const QVariant& value) {
        std::unordered_set<std::string> result;
        for (const auto& item : value.toList()) {
            const QString text = item.toString().trimmed();
            if (!text.isEmpty()) result.insert(text.toStdString());
        }
        return result;
    }

    std::unordered_set<int> biomeSet(const QVariant& value) {
        std::unordered_set<int> result;
        for (const auto& item : value.toList()) {
            bool valid = false;
            const int biome = item.toInt(&valid);
            if (valid) result.insert(biome);
        }
        return result;
    }
}  // namespace

MapController::MapController(MapViewState* map_view_state, AppContext& context, QObject* parent)
    : QObject(parent), map_view_state_(map_view_state), context_(context), region_import_(this) {
    connect(&operation_watcher_, &QFutureWatcher<OperationResult>::finished, this, &MapController::finishSelectionOperation);
    connect(&region_import_, &RegionImportController::failed, this, &MapController::operationFailed);
    if (!map_view_state_) return;
    connect(map_view_state_, &MapViewState::dimChanged, this, [this] { emit dimensionChanged(); });
    connect(map_view_state_, &MapViewState::viewChanged, this, &MapController::stateChanged);
    connect(map_view_state_, &MapViewState::selectionChanged, this, &MapController::selectionChanged);
}

MapController::~MapController() { waitForOperation(); }

int MapController::dimension() const { return mapViewState() ? mapViewState()->dim() : 0; }

int MapController::layer() const { return mapViewState() ? static_cast<int>(mapViewState()->options().layer) : 0; }

bool MapController::transparentVoid() const { return loader_ && loader_->transparentVoid(); }

void MapController::setTransparentVoid(bool enabled) {
    if (!loader_ || loader_->transparentVoid() == enabled) return;
    loader_->setTransparentVoid(enabled);
    // Quick terrain tiles retain void heights, so switching only needs a redraw.
    emit transparentVoidChanged();
    emit stateChanged();
}

MapCameraState MapController::cameraState() const { return mapViewState() ? mapViewState()->cameraState() : MapCameraState{}; }

bool MapController::hasSelection() const { return mapViewState() && !mapViewState()->selection().isEmpty(); }

int MapController::selectionMode() const {
    const auto* map_state = mapViewState();
    return map_state ? static_cast<int>(map_state->selection().mode()) : 0;
}

int MapController::selectionChunkCount() const {
    const auto* map_state = mapViewState();
    return map_state ? map_state->selection().chunkCount() : 0;
}

int MapController::selectionRectCount() const {
    const auto* map_state = mapViewState();
    return map_state ? static_cast<int>(map_state->selection().rectCount()) : 0;
}

int MapController::modifiedChunkCount() const {
    if (!loader_) return 0;
    return loader_->chunkModifyCounts().second;
}

int MapController::deletedChunkCount() const {
    if (!loader_) return 0;
    return loader_->chunkModifyCounts().first;
}

bool MapController::selectionDragging() const {
    const auto* map_state = mapViewState();
    return map_state && map_state->selection().isDragging();
}

QVector<QRect> MapController::selectionRects() const {
    QVector<QRect> result;
    const auto* map_state = mapViewState();
    if (!map_state) return result;
    for (const auto& rect : map_state->selection().region()) result.append(rect);
    return result;
}

QRect MapController::selectionDragRect() const {
    const auto* map_state = mapViewState();
    return map_state && map_state->selection().isDragging() ? map_state->selection().dragRect() : QRect();
}

bool MapController::selectionContainsChunkAt(const QPointF& view_position) const {
    const auto* map_state = mapViewState();
    if (!map_state || map_state->selection().isEmpty()) return false;
    const auto chunk = map_state->chunkPosAt(view_position, map_state->viewportSize());
    return map_state->selection().region().contains(QPoint(chunk.x, chunk.z));
}

QVariantMap MapController::blockInfoAt(const QPointF& view_position) const {
    QVariantMap result;
    const auto* map_state = mapViewState();
    if (!map_state) return result;
    const QPoint block = map_state->blockPosAt(view_position, map_state->viewportSize());
    const int dimension = map_state->dim();
    const auto chunk_x = static_cast<int>(std::floor(block.x() / 16.0));
    const auto chunk_z = static_cast<int>(std::floor(block.y() / 16.0));
    result.insert(QStringLiteral("blockX"), block.x());
    result.insert(QStringLiteral("blockZ"), block.y());
    result.insert(QStringLiteral("chunkX"), chunk_x);
    result.insert(QStringLiteral("chunkZ"), chunk_z);
    result.insert(QStringLiteral("dimension"), dimension);
    if (!loader_ || !loader_->isOpen()) return result;

    const bl::block_pos pos(block.x(), 0, block.y());
    const std::string block_name = loader_->getBlockName(pos, dimension);
    const auto tips = loader_->getBlockTips(pos, dimension);
    result.insert(QStringLiteral("blockName"), QString::fromStdString(block_name));
    result.insert(QStringLiteral("biomeName"), QString::fromStdString(bl::get_biome_name(tips.biome)));
    result.insert(QStringLiteral("biomeId"), static_cast<int>(tips.biome));
    result.insert(QStringLiteral("altitude"), tips.height);
    result.insert(QStringLiteral("tpCommand"), QStringLiteral("tp @s %1 ~ %2").arg(QString::number(block.x()), QString::number(block.y())));
    return result;
}

QVariantMap MapController::voxelViewArea() const {
    QVariantMap result;
    const auto* map_state = mapViewState();
    if (!map_state) return result;

    QRect area = map_state->selection().region().boundingRect();
    if (area.isEmpty()) {
        const auto [min_chunk, max_chunk, unused] = map_state->renderRange();
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
    result.insert(QStringLiteral("dimension"), map_state->dim());
    return result;
}

bool MapController::worldLoaded() const { return loader_ && loader_->isOpen(); }

void MapController::setLevelLoader(AsyncLevelLoader* loader) {
    if (loader_ == loader) return;
    region_import_.cancel();
    if (loader_) disconnect(loader_, nullptr, this, nullptr);
    loader_ = loader;
    if (loader_) {
        connect(loader_, &AsyncLevelLoader::chunkCoordsPreloadFinished, this, &MapController::worldStateChanged);
        connect(loader_, &AsyncLevelLoader::dirtyChanged, this, &MapController::modificationStateChanged);
    }
    emit modificationStateChanged();
    emit transparentVoidChanged();
    emit worldStateChanged();
}

void MapController::refreshRegions(const QRegion& region, int dimension) {
    if (region.isEmpty()) return;
    if (loader_) loader_->invalidateRegionTiles(region, dimension);
    emit mapDataChanged(region, dimension);
}

void MapController::applyZoomLimits() {
    const bool overview =
        loader_ && loader_->chunkOverviewEnabled() && setting::current().preloadAllChunkCoords && setting::current().chunkOverviewEnabled;
    if (auto* map_state = mapViewState()) map_state->applyTerrainZoomLimits(overview);
}

void MapController::setViewportSize(const QSize& size) {
    applyZoomLimits();
    if (auto* map_state = mapViewState()) map_state->setViewportSize(size);
}

void MapController::setDimension(int dimension) {
    if (dimension != this->dimension()) {
        if (operation_running_) return;
        region_import_.cancel();
    }
    if (auto* map_state = mapViewState()) map_state->setDim(dimension);
}

void MapController::setScale(qreal scale_value, const QPointF& anchor) {
    applyZoomLimits();
    if (auto* map_state = mapViewState()) map_state->setScale(scale_value, anchor);
}

void MapController::zoomBy(qreal factor, const QPointF& anchor) {
    applyZoomLimits();
    if (auto* map_state = mapViewState()) map_state->zoomBy(factor, anchor);
}

void MapController::transformGesture(qreal factor, const QPointF& previous, const QPointF& position) {
    if (!std::isfinite(factor) || factor <= 0.0 || !std::isfinite(previous.x()) || !std::isfinite(previous.y()) ||
        !std::isfinite(position.x()) || !std::isfinite(position.y()))
        return;
    auto* map_state = mapViewState();
    if (!map_state) return;
    applyZoomLimits();
    const qreal scale = map_state->scale();
    if (!std::isfinite(scale) || scale <= 0.0) return;
    // Move the old centroid onto the new one before zooming around it. Publish
    // only the combined transform so the renderer cannot observe half a pinch.
    map_state->translate((position - previous) / scale, false);
    map_state->zoomBy(factor, position);
}

void MapController::zoomAdjacent(int direction, const QPointF& anchor) {
    applyZoomLimits();
    if (auto* map_state = mapViewState()) map_state->zoomToAdjacentLevel(direction, anchor);
}

void MapController::pan(qreal dx, qreal dy) {
    if (auto* map_state = mapViewState()) {
        const qreal scale = std::abs(map_state->scale());
        if (scale > 0.0) {
            map_state->translate(QPointF(dx, dy) / scale, false);
            emit cameraMoved();
        }
    }
}

void MapController::gotoBlock(int x, int z) {
    auto* map_state = mapViewState();
    if (!map_state || qFuzzyIsNull(map_state->scaleLevel())) return;
    const QPointF view_pos = map_state->blockPosToViewPos(bl::block_pos(x, 0, z));
    const QPointF delta = (QPointF(map_state->camera().center()) - view_pos) / std::abs(map_state->scaleLevel());
    map_state->translate(delta);
}

void MapController::setLayer(int layer) {
    if (auto* map_state = mapViewState()) map_state->setLayer(static_cast<MapRenderOptions::LayerType>(layer));
}

void MapController::setOther(int other, bool enabled) {
    if (auto* map_state = mapViewState()) map_state->setOther(static_cast<MapRenderOptions::OtherType>(other), enabled);
}

bool MapController::toggleOther(int other) {
    auto* map_state = mapViewState();
    if (!map_state) return false;
    const auto type = static_cast<MapRenderOptions::OtherType>(other);
    const bool enabled = !map_state->options().getOther(type);
    map_state->setOther(type, enabled);
    return enabled;
}

void MapController::setSelectionMode(int mode) {
    if (auto* map_state = mapViewState()) {
        map_state->setSelectionMode(static_cast<ChunkSelection::Mode>(mode));
        emit selectionModeChanged();
    }
}

void MapController::clearSelection() {
    if (auto* map_state = mapViewState()) map_state->clearSelection();
}

void MapController::beginSelectionDragAt(const QPointF& view_position) {
    if (region_import_.active() || region_import_.loading() || operation_running_) return;
    auto* map_state = mapViewState();
    if (!map_state) return;
    map_state->beginSelectionDrag(map_state->chunkPosAt(view_position, map_state->viewportSize()));
}

void MapController::updateSelectionDragAt(const QPointF& view_position) {
    auto* map_state = mapViewState();
    if (!map_state || !map_state->selection().isDragging()) return;
    map_state->updateSelectionDrag(map_state->chunkPosAt(view_position, map_state->viewportSize()));
}

void MapController::finishSelectionDrag() {
    if (auto* map_state = mapViewState(); map_state && map_state->selection().isDragging()) map_state->finishSelectionDrag();
}

void MapController::cancelSelectionDrag() {
    if (auto* map_state = mapViewState(); map_state && map_state->selection().isDragging()) map_state->cancelSelectionDrag();
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
        return OperationResult{
            success, true, success ? QString() : QStringLiteral("The biome could not be applied to every selected chunk."), {}};
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
        return OperationResult{true, false, {}, QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size()))};
    });
}

bool MapController::exportSelectionAsync(const QUrl& destination) {
    if (destination.isEmpty()) return false;
    QUrl target = destination;
    if (target.isLocalFile()) {
        QString path = target.toLocalFile();
        if (QFileInfo(path).suffix().compare(QStringLiteral("bchks"), Qt::CaseInsensitive) != 0) path += QStringLiteral(".bchks");
        target = QUrl::fromLocalFile(path);
    }
    return startSelectionOperation([target](AsyncLevelLoader& loader, const QRegion& region, int dimension) {
        const bool success = ChunkOperator::exportRegion(region, target, loader, dimension);
        return OperationResult{success, false, success ? QString() : QStringLiteral("The selected chunks could not be exported."), {}};
    });
}

bool MapController::beginImportAsync(const QUrl& source, int chunkX, int chunkZ) {
    if (!loader_ || !loader_->isOpen() || operation_running_ || editing_blocked_) return false;
    cancelSelectionDrag();
    return region_import_.loadFile(source, QPoint(chunkX, chunkZ), dimension());
}

bool MapController::beginPasteAsync(int chunkX, int chunkZ) {
    if (!loader_ || !loader_->isOpen() || operation_running_ || editing_blocked_ || region_import_.active() || region_import_.loading())
        return false;
    // Capture the payload on the GUI thread. The worker owns this snapshot, so
    // later clipboard changes cannot affect an in-progress placement.
    QByteArray bytes = context_.binaryClipboard().read(BinaryClipboard::PayloadKind::ChunkRegion);
    if (bytes.isEmpty()) {
        emit operationFailed(tr("The clipboard does not contain any copied chunks."));
        return false;
    }
    cancelSelectionDrag();
    return region_import_.loadBytes(std::move(bytes), tr("Clipboard"), QPoint(chunkX, chunkZ), dimension());
}

bool MapController::beginImportDragAt(const QPointF& position) {
    const auto* map_state = mapViewState();
    if (!map_state || operation_running_ || editing_blocked_) return false;
    return region_import_.beginDrag(map_state->transformForViewport(map_state->viewportSize()).inverted().map(position));
}

void MapController::updateImportDragAt(const QPointF& position) {
    const auto* map_state = mapViewState();
    if (!map_state || operation_running_ || editing_blocked_) return;
    region_import_.updateDrag(map_state->transformForViewport(map_state->viewportSize()).inverted().map(position));
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
    return startRegionOperation(
        [source, offset](AsyncLevelLoader& loader, const QRegion&, int dimension) {
            try {
                // Always relocate a copy. Canceled placement and retries preserve the
                // source coordinates, entity IDs and NBT exactly as loaded.
                ExportedRegion imported = *source;
                for (auto& chunk : imported.chunks()) {
                    const auto pos = chunk.pos();
                    chunk.move_to(bl::chunk_pos(pos.x + offset.x(), pos.z + offset.y(), dimension), &loader.level());
                }
                const bool success = ChunkOperator::importRegion(imported, loader, false);
                return OperationResult{
                    success, true, success ? QString() : MapController::tr("Some imported chunks could not be written."), {}, true};
            } catch (const std::exception& error) {
                return OperationResult{
                    false, true, MapController::tr("The region could not be imported: %1").arg(QString::fromUtf8(error.what())), {}, true};
            } catch (...) {
                return OperationResult{false, true, MapController::tr("The region could not be imported."), {}, true};
            }
        },
        region_import_.region(), region_import_.dimension());
}

QVariantList MapController::biomeOptions() const {
    QVariantList result;
    magic_enum::enum_for_each<bl::biome>([&](auto value) {
        constexpr auto entry = value;
        if constexpr (entry != bl::biome::none) {
            QVariantMap item;
            item.insert(QStringLiteral("value"), static_cast<int>(entry));
            item.insert(QStringLiteral("name"), QString::fromStdString(bl::get_biome_name(entry)));
            const auto color = bl::get_biome_color(entry);
            item.insert(QStringLiteral("color"), QColor(color.r, color.g, color.b, color.a));
            result.append(item);
        }
    });
    return result;
}

QVariantMap MapController::mapFilterOptions() const {
    return loader_ ? ::mapFilterOptions(loader_->filter()) : ::mapFilterOptions(MapFilter{});
}

QVariantMap MapController::defaultMapFilterOptions() const { return ::mapFilterOptions(MapFilter{}); }

bool MapController::applyMapFilterOptions(const QVariantMap& options) {
    if (!loader_ || !loader_->isOpen()) return false;

    MapFilter filter;
    filter.block_black_mode_ = options.value(QStringLiteral("blockBlackMode"), true).toBool();
    filter.blocks_list_ = stringSet(options.value(QStringLiteral("blockList")));
    filter.biome_black_mode_ = options.value(QStringLiteral("biomeBlackMode"), true).toBool();
    filter.biomes_list_ = biomeSet(options.value(QStringLiteral("biomeList")));
    filter.actor_black_mode_ = options.value(QStringLiteral("actorBlackMode"), true).toBool();
    filter.actors_list_ = stringSet(options.value(QStringLiteral("actorList")));
    filter.enable_layer_ = options.value(QStringLiteral("layerEnabled"), false).toBool();
    filter.layer = std::clamp(options.value(QStringLiteral("layer"), 64).toInt(), -64, 320);

    loader_->setFilter(filter);
    loader_->reloadFilteredRegions();
    emit mapFilterChanged();
    return true;
}

bool MapController::startSelectionOperation(SelectionOperation operation) {
    auto* map_state = mapViewState();
    if (!map_state || map_state->selection().isEmpty() || region_import_.active() || region_import_.loading()) return false;

    const QRegion region = map_state->selection().region();
    const int dimension = map_state->dim();
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
        if (!context_.binaryClipboard().write(BinaryClipboard::PayloadKind::ChunkRegion, result.clipboard)) {
            emit operationFailed(tr("The clipboard is not available."));
            return;
        }
    }
    if (result.imported) {
        region_import_.cancel();
        if (auto* map_state = mapViewState()) map_state->setSelectionRegion(completed_region);
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

MapViewState* MapController::mapViewState() const { return map_view_state_; }
