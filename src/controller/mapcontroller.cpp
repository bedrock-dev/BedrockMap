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
#include "model/chunkarea.h"

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

MapController::MapController(MapViewState& map_view_state, AppContext& context, QObject* parent)
    : QObject(parent), map_view_state_(map_view_state), context_(context), region_import_(this) {
    connect(&operation_watcher_, &QFutureWatcher<SelectionOperationResult>::finished, this, &MapController::finishSelectionOperation);
    connect(&region_import_, &RegionImportController::failed, this, &MapController::operationFailed);
    connect(&map_view_state_, &MapViewState::dimChanged, this, [this] { emit dimensionChanged(); });
    connect(&map_view_state_, &MapViewState::viewChanged, this, &MapController::stateChanged);
    connect(&map_view_state_, &MapViewState::selectionChanged, this, &MapController::selectionChanged);
}

MapController::~MapController() { waitForOperation(); }

int MapController::dimension() const { return map_view_state_.dim(); }

int MapController::layer() const { return static_cast<int>(map_view_state_.options().layer); }

bool MapController::transparentVoid() const { return loader_ && loader_->transparentVoid(); }

void MapController::setTransparentVoid(bool enabled) {
    if (!loader_ || loader_->transparentVoid() == enabled) return;
    loader_->setTransparentVoid(enabled);
    // Quick terrain tiles retain void heights, so switching only needs a redraw.
    emit transparentVoidChanged();
    emit stateChanged();
}

MapCameraState MapController::cameraState() const { return map_view_state_.cameraState(); }

bool MapController::hasSelection() const { return !map_view_state_.selection().isEmpty(); }

int MapController::selectionMode() const { return static_cast<int>(map_view_state_.selection().mode()); }

int MapController::selectionChunkCount() const { return map_view_state_.selection().chunkCount(); }

int MapController::selectionRectCount() const { return static_cast<int>(map_view_state_.selection().rectCount()); }

int MapController::modifiedChunkCount() const {
    if (!loader_) return 0;
    return loader_->chunkModifyCounts().second;
}

int MapController::deletedChunkCount() const {
    if (!loader_) return 0;
    return loader_->chunkModifyCounts().first;
}

bool MapController::deleteOutsideAvailable() const { return loader_ && loader_->preloadAllChunkCoords() && loader_->chunkCoordsReady(); }

bool MapController::selectionDragging() const { return map_view_state_.selection().isDragging(); }

QVector<QRect> MapController::selectionRects() const {
    QVector<QRect> result;
    for (const auto& rect : map_view_state_.selection().region()) result.append(rect);
    return result;
}

QRect MapController::selectionDragRect() const {
    return map_view_state_.selection().isDragging() ? map_view_state_.selection().dragRect() : QRect();
}

bool MapController::selectionContainsChunkAt(const QPointF& view_position) const {
    if (map_view_state_.selection().isEmpty()) return false;
    const auto chunk = map_view_state_.chunkPosAt(view_position, map_view_state_.viewportSize());
    return map_view_state_.selection().region().contains(QPoint(chunk.x, chunk.z));
}

QVariantMap MapController::blockCoordinatesAt(const QPointF& view_position) const {
    QVariantMap result;
    const QPoint block = map_view_state_.blockPosAt(view_position, map_view_state_.viewportSize());
    const int dimension = map_view_state_.dim();
    const auto chunk_x = static_cast<int>(std::floor(block.x() / 16.0));
    const auto chunk_z = static_cast<int>(std::floor(block.y() / 16.0));
    result.insert(QStringLiteral("blockX"), block.x());
    result.insert(QStringLiteral("blockZ"), block.y());
    result.insert(QStringLiteral("chunkX"), chunk_x);
    result.insert(QStringLiteral("chunkZ"), chunk_z);
    result.insert(QStringLiteral("dimension"), dimension);
    return result;
}

QVariantMap MapController::blockInfoAt(const QPointF& view_position) const {
    QVariantMap result = blockCoordinatesAt(view_position);
    if (!loader_ || !loader_->isOpen()) return result;

    const int block_x = result.value(QStringLiteral("blockX")).toInt();
    const int block_z = result.value(QStringLiteral("blockZ")).toInt();
    const int dimension = result.value(QStringLiteral("dimension")).toInt();
    const bl::block_pos pos(block_x, 0, block_z);
    const std::string block_name = loader_->getBlockName(pos, dimension);
    const auto tips = loader_->getBlockTips(pos, dimension);
    result.insert(QStringLiteral("blockName"), QString::fromStdString(block_name));
    result.insert(QStringLiteral("biomeName"), QString::fromStdString(bl::get_biome_name(tips.biome)));
    result.insert(QStringLiteral("biomeId"), static_cast<int>(tips.biome));
    result.insert(QStringLiteral("altitude"), tips.height);
    result.insert(QStringLiteral("tpCommand"), QStringLiteral("tp @s %1 ~ %2").arg(QString::number(block_x), QString::number(block_z)));
    return result;
}

QVariantMap MapController::voxelViewArea() const {
    QVariantMap result;
    QRect area = map_view_state_.selection().region().boundingRect();
    if (area.isEmpty()) {
        const auto [min_chunk, max_chunk, unused] = map_view_state_.renderRange();
        Q_UNUSED(unused);
        area = QRect(QPoint(min_chunk.x, min_chunk.z), QPoint(max_chunk.x, max_chunk.z));
    }
    if (area.isEmpty()) return result;

    const ChunkArea chunks{area.left(), area.top(), area.right(), area.bottom(), map_view_state_.dim()};
    if (!chunks.valid()) return result;
    result.insert(QStringLiteral("minX"), chunks.minChunkX);
    result.insert(QStringLiteral("minZ"), chunks.minChunkZ);
    result.insert(QStringLiteral("maxX"), chunks.maxChunkX);
    result.insert(QStringLiteral("maxZ"), chunks.maxChunkZ);
    result.insert(QStringLiteral("dimension"), chunks.dimension);
    return result;
}

QVariantList MapController::customDimensionOptions() const {
    QVariantList result;
    if (!loader_ || !loader_->isOpen()) return result;

    std::vector<std::pair<std::string, int>> dimensions;
    for (const auto& [name, dimension] : loader_->level().custom_dimension_table()) dimensions.emplace_back(name, dimension);
    std::sort(dimensions.begin(), dimensions.end(), [](const auto& lhs, const auto& rhs) {
        return QString::localeAwareCompare(QString::fromStdString(lhs.first), QString::fromStdString(rhs.first)) < 0;
    });

    for (const auto& [name, dimension] : dimensions) {
        QVariantMap option;
        option.insert(QStringLiteral("name"), QString::fromStdString(name));
        option.insert(QStringLiteral("id"), dimension);
        result.append(option);
    }
    return result;
}

void MapController::setLevelLoader(AsyncLevelLoader* loader) {
    if (loader_ == loader) return;
    region_import_.cancel();
    if (loader_) disconnect(loader_, nullptr, this, nullptr);
    loader_ = loader;
    initial_position_applied_ = false;
    if (loader_) {
        connect(loader_, &AsyncLevelLoader::chunkCoordsPreloadFinished, this, [this] {
            emit deleteOutsideAvailableChanged();
            emit worldStateChanged();
        });
        connect(loader_, &AsyncLevelLoader::dirtyChanged, this, &MapController::modificationStateChanged);
    }
    emit modificationStateChanged();
    emit deleteOutsideAvailableChanged();
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
    map_view_state_.applyTerrainZoomLimits(overview);
}

void MapController::setViewportSize(const QSize& size) {
    applyZoomLimits();
    map_view_state_.setViewportSize(size);
}

void MapController::setDimension(int dimension) {
    if (dimension != this->dimension()) {
        if (operation_running_) return;
        region_import_.cancel();
    }
    map_view_state_.setDim(dimension);
}

void MapController::transformGesture(qreal factor, const QPointF& previous, const QPointF& position) {
    if (!std::isfinite(factor) || factor <= 0.0 || !std::isfinite(previous.x()) || !std::isfinite(previous.y()) ||
        !std::isfinite(position.x()) || !std::isfinite(position.y()))
        return;
    applyZoomLimits();
    const qreal scale = map_view_state_.scale();
    if (!std::isfinite(scale) || scale <= 0.0) return;
    // Move the old centroid onto the new one before zooming around it. Publish
    // only the combined transform so the renderer cannot observe half a pinch.
    map_view_state_.translate((position - previous) / scale, false);
    map_view_state_.zoomBy(factor, position);
}

void MapController::zoomAdjacent(int direction, const QPointF& anchor) {
    applyZoomLimits();
    map_view_state_.zoomToAdjacentLevel(direction, anchor);
}

void MapController::pan(qreal dx, qreal dy) {
    const qreal scale = std::abs(map_view_state_.scale());
    if (scale > 0.0) {
        map_view_state_.translate(QPointF(dx, dy) / scale, false);
        emit cameraMoved();
    }
}

void MapController::gotoBlock(int x, int z) { (void)centerOnBlockPosition(x, z); }

bool MapController::centerOnInitialMapPosition() {
    if (initial_position_applied_) return true;
    if (!loader_ || !loader_->isOpen()) return false;

    QPointF position{0.0, 0.0};
    const QString mode = setting::current().initialMapPosition;
    if (mode == QStringLiteral("world_spawn")) {
        if (const auto spawn = loader_->worldSpawnPosition()) position = QPointF(spawn->x, spawn->z);
    } else if (mode == QStringLiteral("player")) {
        if (const auto player = loader_->localPlayerPosition()) position = QPointF(player->x, player->z);
    }

    if (!centerOnBlockPosition(position.x(), position.y())) return false;
    initial_position_applied_ = true;
    return true;
}

bool MapController::centerOnBlockPosition(qreal x, qreal z) {
    const qreal scale = std::abs(map_view_state_.scaleLevel());
    const QSize size = map_view_state_.viewportSize();
    if (size.isEmpty() || !std::isfinite(scale) || scale <= 0.0) return false;

    const QPointF viewPosition = map_view_state_.worldToView().map(QPointF(x / 16.0, z / 16.0));
    const QPointF viewCenter(size.width() / 2.0, size.height() / 2.0);
    map_view_state_.translate((viewCenter - viewPosition) / scale);
    return true;
}

void MapController::setLayer(int layer) { map_view_state_.setLayer(static_cast<MapRenderOptions::LayerType>(layer)); }

QVariantMap MapController::overlays() const {
    QVariantMap result;
    for (const auto type : magic_enum::enum_values<MapRenderOptions::OtherType>()) {
        const auto name = magic_enum::enum_name(type);
        QVariantMap state;
        state.insert(QStringLiteral("type"), static_cast<int>(type));
        state.insert(QStringLiteral("enabled"), map_view_state_.options().getOther(type));
        result.insert(QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size())), state);
    }
    return result;
}

void MapController::setOverlay(int type, bool enabled) {
    const auto overlay = magic_enum::enum_cast<MapRenderOptions::OtherType>(type);
    if (!overlay || map_view_state_.options().getOther(*overlay) == enabled) return;
    map_view_state_.setOther(*overlay, enabled);
    emit overlaysChanged();
}

void MapController::toggleOverlay(int type) {
    const auto overlay = magic_enum::enum_cast<MapRenderOptions::OtherType>(type);
    if (!overlay) return;
    map_view_state_.setOther(*overlay, !map_view_state_.options().getOther(*overlay));
    emit overlaysChanged();
}

void MapController::setSelectionMode(int mode) {
    map_view_state_.setSelectionMode(static_cast<ChunkSelection::Mode>(mode));
    emit selectionModeChanged();
}

void MapController::clearSelection() { map_view_state_.clearSelection(); }

void MapController::beginSelectionDragAt(const QPointF& view_position) {
    if (region_import_.active() || region_import_.loading() || operation_running_) return;
    map_view_state_.beginSelectionDrag(map_view_state_.chunkPosAt(view_position, map_view_state_.viewportSize()));
}

void MapController::updateSelectionDragAt(const QPointF& view_position) {
    if (!map_view_state_.selection().isDragging()) return;
    map_view_state_.updateSelectionDrag(map_view_state_.chunkPosAt(view_position, map_view_state_.viewportSize()));
}

void MapController::finishSelectionDrag() {
    if (map_view_state_.selection().isDragging()) map_view_state_.finishSelectionDrag();
}

void MapController::cancelSelectionDrag() {
    if (map_view_state_.selection().isDragging()) map_view_state_.cancelSelectionDrag();
}

bool MapController::deleteSelectionAsync() {
    return startSelectionOperation([](AsyncLevelLoader& loader, const QRegion& region, int dimension) {
        const bool success = ChunkOperator::deleteRegion(region, loader, dimension, false);
        return SelectionOperationResult::refreshSelection(
            success, success ? QString() : QStringLiteral("Some selected chunks could not be deleted."));
    });
}

bool MapController::deleteOutsideSelectionAsync() {
    if (!deleteOutsideAvailable()) return false;
    return startSelectionOperation([](AsyncLevelLoader& loader, const QRegion& region, int dimension) {
        std::vector<bl::chunk_pos> affected_regions;
        const bool success = ChunkOperator::deleteOutsideRegion(region, loader, dimension, &affected_regions, false);
        const QString error = success ? QString() : MapController::tr("Some chunks outside the selection could not be deleted.");
        if (affected_regions.empty()) return SelectionOperationResult::noRefresh(success, error);
        return SelectionOperationResult::refreshRegions(success, std::move(affected_regions), error);
    });
}

bool MapController::createVoidSelectionAsync() {
    return startSelectionOperation([](AsyncLevelLoader& loader, const QRegion& region, int dimension) {
        const bool success = ChunkOperator::createVoid(region, loader, dimension, false);
        return SelectionOperationResult::refreshSelection(
            success, success ? QString() : QStringLiteral("Some selected chunks could not be cleared."));
    });
}

bool MapController::setSelectionBiomeAsync(int biome) {
    if (!magic_enum::enum_contains<bl::biome>(biome) || biome == static_cast<int>(bl::biome::none)) return false;
    const auto selected_biome = static_cast<bl::biome>(biome);
    return startSelectionOperation([selected_biome](AsyncLevelLoader& loader, const QRegion& region, int dimension) {
        const bool success = ChunkOperator::setRegionBiome(region, loader, selected_biome, dimension, false);
        return SelectionOperationResult::refreshSelection(
            success, success ? QString() : QStringLiteral("The biome could not be applied to every selected chunk."));
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
        if (exported.isEmpty())
            return SelectionOperationResult::noRefresh(false, MapController::tr("No chunks were found in the selection."));
        const auto bytes = exported.serialize();
        return SelectionOperationResult::withClipboard(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())));
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
        return SelectionOperationResult::noRefresh(success,
                                                   success ? QString() : QStringLiteral("The selected chunks could not be exported."));
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
    if (operation_running_ || editing_blocked_) return false;
    return region_import_.beginDrag(map_view_state_.transformForViewport(map_view_state_.viewportSize()).inverted().map(position));
}

void MapController::updateImportDragAt(const QPointF& position) {
    if (operation_running_ || editing_blocked_) return;
    region_import_.updateDrag(map_view_state_.transformForViewport(map_view_state_.viewportSize()).inverted().map(position));
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
                return SelectionOperationResult::importResult(
                    success, success ? QString() : MapController::tr("Some imported chunks could not be written."));
            } catch (const std::exception& error) {
                return SelectionOperationResult::importResult(
                    false, MapController::tr("The region could not be imported: %1").arg(QString::fromUtf8(error.what())));
            } catch (...) {
                return SelectionOperationResult::importResult(false, MapController::tr("The region could not be imported."));
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
    if (map_view_state_.selection().isEmpty() || region_import_.active() || region_import_.loading()) return false;

    const QRegion region = map_view_state_.selection().region();
    const int dimension = map_view_state_.dim();
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
            return SelectionOperationResult::noRefresh(false, QString::fromUtf8(error.what()));
        } catch (...) {
            return SelectionOperationResult::noRefresh(false, QStringLiteral("The map operation failed."));
        }
    }));
    return true;
}

void MapController::finishSelectionOperation() {
    const auto result = operation_watcher_.result();
    operation_running_ = false;
    emit operationChanged();

    QRegion refresh_region;
    if (result.refresh_target == SelectionOperationResult::RefreshTarget::Selection) {
        refresh_region = operation_region_;
    } else if (result.refresh_target == SelectionOperationResult::RefreshTarget::Regions) {
        for (const auto& region : result.refresh_regions) refresh_region += QRect(region.x, region.z, constant::RW, constant::RW);
    }
    if (loader_ && !refresh_region.isEmpty()) {
        // The edit ran on a worker. Drop the old region tiles here, before the
        // provider starts refilling, or cached centre tiles would be delivered
        // again and only uncached surrounding regions would show the edit.
        if (result.refresh_target == SelectionOperationResult::RefreshTarget::Regions)
            loader_->invalidateTilesForRegions(result.refresh_regions);
        else
            loader_->invalidateRegionTiles(refresh_region, operation_dimension_);
        emit mapDataChanged(refresh_region, operation_dimension_);
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
        map_view_state_.setSelectionRegion(completed_region);
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
