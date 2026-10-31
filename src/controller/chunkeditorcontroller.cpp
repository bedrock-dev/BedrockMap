#include "controller/chunkeditorcontroller.h"

#include <QClipboard>
#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QUrl>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "actor.h"
#include "bedrock_key.h"
#include "chunk.h"
#include "color.h"
#include "io/documentio.h"
#include "loader/asynclevelloader.h"
#include "loader/blockvolumeoperator.h"
#include "nbt.h"
#include "raw_chunk.h"
#include "render/voxel_mesh.h"

namespace {
    constexpr qsizetype OVERSIZE_BYTES = 16 * 1024 * 1024;
    using NbtRoot = std::shared_ptr<bl::nbt::compound_tag>;
    using NbtRoots = std::vector<NbtRoot>;

    void appendRecords(NbtRoots& target, std::string_view data, const QString& oversizedMessage) {
        if (data.empty()) return;
        if (static_cast<qsizetype>(data.size()) > OVERSIZE_BYTES) {
            auto root = std::make_shared<bl::nbt::compound_tag>("0");
            root->put(new bl::nbt::string_tag("message", oversizedMessage.toStdString()));
            target.push_back(std::move(root));
            return;
        }
        for (auto* root : bl::nbt::parse_all(data.data(), data.size())) {
            if (root) target.emplace_back(root);
        }
    }

    void appendStatistic(ChunkStatisticsSnapshot& statistics, const QString& name, std::string_view data) {
        const QByteArray bytes(data.data(), static_cast<qsizetype>(data.size()));
        const QByteArray hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
        statistics.rows.push_back(ChunkStatisticRowSnapshot{
            name,
            static_cast<qint64>(bytes.size()),
            QString::fromLatin1(hash.toHex().left(8)),
            bytes,
        });
    }

    QString stringTagValue(const bl::nbt::compound_tag* root, std::initializer_list<std::string_view> keys) {
        if (!root) return {};
        for (const auto key : keys) {
            const auto* tag = root->get(key);
            if (tag && tag->type() == bl::nbt::tag_type::String)
                return QString::fromStdString(static_cast<const bl::nbt::string_tag*>(tag)->value);
        }
        return {};
    }

    QString normalizedNbtName(QString name) {
        name = name.toLower();
        if (name.startsWith(QStringLiteral("minecraft:"))) name.remove(0, 10);
        return name;
    }

    QString nbtIcon(const QString& category, const QString& name) {
        const QString resource_path = QStringLiteral(":/bedrockmap/icons/%1/%2.png").arg(category, name);
        return QFile::exists(resource_path) ? QStringLiteral("qrc:/bedrockmap/icons/%1/%2.png").arg(category, name)
                                            : QStringLiteral("qrc:/bedrockmap/icons/what.png");
    }

    NbtItemPresentation entityPresentation(const bl::nbt::compound_tag* root, int index) {
        const QString identifier = normalizedNbtName(stringTagValue(root, {"identifier", "ActorIdentifier"}));
        const QString label = identifier.isEmpty() ? QString::number(index) : identifier;
        const QString icon = identifier.isEmpty() ? QStringLiteral("qrc:/bedrockmap/icons/what.png") : nbtIcon("entity", identifier);
        return {label, icon};
    }

    NbtItemPresentation blockEntityPresentation(const bl::nbt::compound_tag* root, int index) {
        const QString rawIdentifier = stringTagValue(root, {"id"});
        const QString identifier = normalizedNbtName(rawIdentifier);
        const QString label = rawIdentifier.isEmpty() ? QString::number(index) : rawIdentifier;
        const QString icon = identifier.isEmpty() ? QStringLiteral("qrc:/bedrockmap/icons/what.png") : nbtIcon("block_actor", identifier);
        return {label, icon};
    }

    QColor toQColor(bl::color color) { return QColor(color.r, color.g, color.b); }

    std::shared_ptr<VoxelSceneData> buildVoxelScene(const bl::raw_chunk& raw, const std::function<void(int)>& reportProgress,
                                                    ChunkTerrainSnapshot* terrain = nullptr) {
        auto chunk = std::make_unique<bl::chunk>(raw.pos());
        if (!chunk->load_from_raw_chunk(raw, bl::chunk_load_policy::Terrain | bl::chunk_load_policy::Others)) return {};
        const auto [worldMinY, worldMaxY] = raw.get_y_range();
        const int layerCount = std::max(0, worldMaxY - worldMinY + 1);
        if (layerCount == 0) return {};

        if (terrain) {
            terrain->minY = worldMinY;
            terrain->maxY = worldMaxY;
            terrain->layers.resize(static_cast<size_t>(layerCount));
            for (int index = 0; index < layerCount; ++index) terrain->layers[static_cast<size_t>(index)].y = worldMinY + index;
        }

        for (int worldY = worldMinY; worldY <= worldMaxY; ++worldY) {
            ChunkTerrainLayerSnapshot* terrainLayer = terrain ? &terrain->layers[static_cast<size_t>(worldY - worldMinY)] : nullptr;
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    const auto biome = chunk->get_biome(x, worldY, z);
                    if (terrainLayer) {
                        auto& cell = terrainLayer->cells[static_cast<size_t>(z * 16 + x)];
                        cell.biome = static_cast<int>(biome);
                        cell.biomeName = QString::fromStdString(bl::get_biome_name(biome));
                        cell.biomeColor = toQColor(bl::get_biome_color(biome));
                        for (int blockLayer = 0;; ++blockLayer) {
                            auto* rawBlock = chunk->get_block_raw(x, worldY, z, blockLayer);
                            if (!rawBlock) break;
                            const auto blockInfo = chunk->get_block_with_color(x, worldY, z, blockLayer);
                            const auto blended = bl::blend_color_with_biome(blockInfo.name, blockInfo.color, biome);
                            const bool visibleBlock = blockInfo.name != "minecraft:air" && blockInfo.name != "minecraft:unknown";
                            cell.blocks.push_back(ChunkTerrainBlockSnapshot{
                                QString::fromStdString(blockInfo.name),
                                QString::fromStdString(rawBlock->to_readable_string()),
                                visibleBlock ? toQColor(blended) : QColor(0, 0, 0, 0),
                            });
                        }
                    }
                }
            }
            if ((worldY - worldMinY) % 32 == 0) reportProgress(35 + (worldY - worldMinY) * 25 / std::max(1, layerCount));
        }
        int firstWorldY = 0;
        auto grid = voxel_mesh::createVoxelDataFromChunks({{chunk.get()}}, {}, &firstWorldY);
        const auto bounds = voxel_mesh::fullBounds(grid);
        if (!bounds) return {};
        reportProgress(65);

        auto scene = std::make_shared<VoxelSceneData>();
        scene->origin = QVector3D(raw.pos().x * 16, firstWorldY, raw.pos().z * 16);
        scene->modelSize = QVector3D(16.0f, static_cast<float>(grid.size()), 16.0f);
        auto mesh = voxel_mesh::build(grid, *bounds);
        scene->opaqueVertices = std::move(mesh.opaqueVertices);
        scene->opaqueIndices = std::move(mesh.opaqueIndices);
        scene->transparentVertices = std::move(mesh.transparentVertices);
        scene->transparentIndices = std::move(mesh.transparentIndices);
        scene->voxels = std::make_shared<const VoxelGrid>(std::move(grid));
        reportProgress(98);
        return scene;
    }

    std::shared_ptr<ChunkNbtSnapshot> loadChunkSnapshot(AsyncLevelLoader* loader, bl::chunk_pos pos, quint64 generation,
                                                        const std::function<void(int)>& reportProgress) {
        auto snapshot = std::make_shared<ChunkNbtSnapshot>();
        snapshot->generation = generation;
        snapshot->chunkX = pos.x;
        snapshot->chunkZ = pos.z;
        snapshot->dimension = pos.dim;
        try {
            if (!loader || !loader->isOpen()) {
                snapshot->error = ChunkEditorController::tr("The world is not open.");
                return snapshot;
            }
            auto raw = loader->getRawChunk(pos);
            if (!raw) {
                snapshot->error = ChunkEditorController::tr("No chunk data was found at this position.");
                return snapshot;
            }

            const auto [min_y, max_y] = raw->get_y_range();
            snapshot->yRange = ChunkEditorController::tr("%1 to %2").arg(min_y).arg(max_y);
            reportProgress(20);
            appendRecords(snapshot->entities, raw->get_normal_key(bl::chunk_key::Entity),
                          ChunkEditorController::tr("Entity data is too large to preview."));
            for (const auto& [uid, data] : raw->get_entities()) {
                Q_UNUSED(uid);
                appendRecords(snapshot->entities, data, ChunkEditorController::tr("Entity data is too large to preview."));
            }
            appendRecords(snapshot->blockEntities, raw->get_normal_key(bl::chunk_key::BlockEntity),
                          ChunkEditorController::tr("Block entity data is too large to preview."));
            appendRecords(snapshot->pendingTicks, raw->get_normal_key(bl::chunk_key::PendingTicks),
                          ChunkEditorController::tr("Pending tick data is too large to preview."));
            reportProgress(32);
            snapshot->statistics = std::make_shared<ChunkStatisticsSnapshot>();
            for (const auto& [key, data] : raw->get_normal_data())
                appendStatistic(*snapshot->statistics, QString::fromStdString(bl::chunk_key::chunk_key_to_str(key)), data);
            for (const auto& [identifierHash, data] : raw->get_jigsaw_data()) {
                const QString hashText = QString::number(static_cast<qulonglong>(identifierHash), 16).rightJustified(16, QLatin1Char('0'));
                appendStatistic(*snapshot->statistics, QStringLiteral("JigsawStructureBlueprint[0x%1]").arg(hashText), data);
            }
            for (const auto& [y, data] : raw->get_sub_chunks()) {
                if (!data.empty())
                    appendStatistic(*snapshot->statistics, QStringLiteral("SubChunk[%1 ~ %2]").arg(y * 16).arg((y + 1) * 16 - 1), data);
            }
            if (!raw->get_actor_digest().empty())
                appendStatistic(*snapshot->statistics, QStringLiteral("ActorDigest"), raw->get_actor_digest());
            for (const auto& [key, data] : raw->get_entities()) {
                if (key.size() != 8) continue;
                const QString hexKey = QString::fromLatin1(QByteArray::fromRawData(key.data(), static_cast<qsizetype>(key.size())).toHex());
                appendStatistic(*snapshot->statistics, QStringLiteral("Entity[%1]").arg(hexKey), data);
            }
            snapshot->terrain = std::make_shared<ChunkTerrainSnapshot>();
            snapshot->voxelScene = buildVoxelScene(*raw, reportProgress, snapshot->terrain.get());
            snapshot->ok = true;
        } catch (const std::exception& error) {
            snapshot->error = QString::fromUtf8(error.what());
        } catch (...) {
            snapshot->error = ChunkEditorController::tr("Could not read this chunk.");
        }
        return snapshot;
    }

    std::vector<std::unique_ptr<bl::nbt::compound_tag>> cloneRoots(const NbtRoots& roots) {
        std::vector<std::unique_ptr<bl::nbt::compound_tag>> result;
        result.reserve(roots.size());
        for (const auto& root : roots) {
            if (!root) continue;
            result.emplace_back(static_cast<bl::nbt::compound_tag*>(root->copy()));
        }
        return result;
    }
}  // namespace

ChunkEditorController::ChunkEditorController(QObject* parent) : VoxelSceneController(parent) {
    entities_document_.setParent(this);
    block_entities_document_.setParent(this);
    pending_ticks_document_.setParent(this);
    connect(&watcher_, &QFutureWatcher<std::shared_ptr<ChunkNbtSnapshot>>::finished, this, &ChunkEditorController::finishLoad);
    connect(&entities_document_, &NbtDocumentController::dirtyChanged, this, &ChunkEditorController::dirtyChanged);
    connect(&block_entities_document_, &NbtDocumentController::dirtyChanged, this, &ChunkEditorController::dirtyChanged);
    connect(&pending_ticks_document_, &NbtDocumentController::dirtyChanged, this, &ChunkEditorController::dirtyChanged);
}

ChunkEditorController::~ChunkEditorController() {
    waitForExport();
    if (watcher_.isRunning()) watcher_.waitForFinished();
}

void ChunkEditorController::setLoader(AsyncLevelLoader* loader) {
    if (loader_ == loader) return;
    waitForExport();
    if (watcher_.isRunning()) watcher_.waitForFinished();
    clear();
    loader_ = loader;
}

void ChunkEditorController::openChunk(int chunkX, int chunkZ, int dimension) {
    if (!loader_ || !loader_->isOpen() || watcher_.isRunning()) return;
    ++request_generation_;
    chunk_x_ = chunkX;
    chunk_z_ = chunkZ;
    dimension_ = dimension;
    y_range_.clear();
    error_message_.clear();
    has_chunk_ = false;
    entities_document_.loadMemory({}, {}, tr("Entities"));
    block_entities_document_.loadMemory({}, {}, tr("Block entities"));
    pending_ticks_document_.loadMemory({}, {}, tr("Pts"));
    terrain_.reset();
    statistics_.reset();
    terrain_y_ = 0;
    terrain_layer_ = 0;
    voxel_scene_.reset();
    setVoxelProgress(0);
    setLoading(true);
    emit chunkChanged();
    emit dataChanged();
    emit terrainChanged();
    emit statisticsChanged();
    emit statusChanged();

    const quint64 generation = request_generation_;
    watcher_.setFuture(QtConcurrent::run([this, loader = loader_, pos = bl::chunk_pos(chunkX, chunkZ, dimension), generation] {
        const auto reportProgress = [this](int progress) {
            QMetaObject::invokeMethod(this, [this, progress] { setVoxelProgress(progress); }, Qt::QueuedConnection);
        };
        return loadChunkSnapshot(loader, pos, generation, reportProgress);
    }));
}

void ChunkEditorController::clear() {
    ++request_generation_;
    setLoading(false);
    has_chunk_ = false;
    y_range_.clear();
    error_message_.clear();
    entities_document_.loadMemory({}, {}, tr("Entities"));
    block_entities_document_.loadMemory({}, {}, tr("Block entities"));
    pending_ticks_document_.loadMemory({}, {}, tr("Pts"));
    terrain_.reset();
    statistics_.reset();
    terrain_y_ = 0;
    terrain_layer_ = 0;
    voxel_scene_.reset();
    setVoxelProgress(0);
    emit voxelChanged();
    emit chunkChanged();
    emit dataChanged();
    emit terrainChanged();
    emit statisticsChanged();
    emit statusChanged();
}

bool ChunkEditorController::dirty() const {
    return entities_document_.dirty() || block_entities_document_.dirty() || pending_ticks_document_.dirty();
}

int ChunkEditorController::terrainMinY() const { return terrain_ ? terrain_->minY : 0; }

int ChunkEditorController::terrainMaxY() const { return terrain_ ? terrain_->maxY : -1; }

const ChunkTerrainLayerSnapshot* ChunkEditorController::terrainLayerAt(int y) const {
    if (!terrain_ || terrain_->layers.empty() || y < terrain_->minY || y > terrain_->maxY) return nullptr;
    const auto index = static_cast<size_t>(y - terrain_->minY);
    if (index >= terrain_->layers.size()) return nullptr;
    const auto* layer = &terrain_->layers[index];
    return layer->y == y ? layer : nullptr;
}

int ChunkEditorController::terrainMaxLayer() const {
    const auto* layer = terrainLayerAt(terrain_y_);
    if (!layer) return 0;
    qsizetype count = 0;
    for (const auto& cell : layer->cells) count = std::max(count, cell.blocks.size());
    return count == 0 ? 0 : static_cast<int>(count - 1);
}

QVariantList ChunkEditorController::terrainCells() const {
    QVariantList result;
    const auto* layer = terrainLayerAt(terrain_y_);
    if (!layer) return result;

    result.reserve(256);
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            const auto& cell = layer->cells[static_cast<size_t>(z * 16 + x)];
            const bool hasBlock = terrain_layer_ >= 0 && terrain_layer_ < cell.blocks.size();
            const auto& block = hasBlock ? cell.blocks[static_cast<size_t>(terrain_layer_)]
                                         : ChunkTerrainBlockSnapshot{QStringLiteral("minecraft:air"), {}, QColor(0, 0, 0, 0)};
            QVariantMap value;
            value.insert(QStringLiteral("x"), x);
            value.insert(QStringLiteral("y"), terrain_y_);
            value.insert(QStringLiteral("z"), z);
            value.insert(QStringLiteral("terrainColor"), block.color);
            value.insert(QStringLiteral("blockName"), block.name);
            value.insert(QStringLiteral("blockPalette"), block.palette);
            value.insert(QStringLiteral("biomeId"), cell.biome);
            value.insert(QStringLiteral("biomeName"), cell.biomeName);
            value.insert(QStringLiteral("biomeLabel"), QStringLiteral("%1 (%2)").arg(cell.biome).arg(cell.biomeName));
            value.insert(QStringLiteral("biomeColor"), cell.biomeColor);
            result.push_back(value);
        }
    }
    return result;
}

QVariantList ChunkEditorController::statistics() const {
    QVariantList result;
    if (!statistics_) return result;
    result.reserve(static_cast<qsizetype>(statistics_->rows.size()));
    for (size_t index = 0; index < statistics_->rows.size(); ++index) {
        const auto& row = statistics_->rows[index];
        QVariantMap value;
        value.insert(QStringLiteral("index"), static_cast<int>(index));
        value.insert(QStringLiteral("name"), row.name);
        value.insert(QStringLiteral("size"), row.size);
        value.insert(QStringLiteral("hash"), row.hash);
        result.push_back(value);
    }
    return result;
}

void ChunkEditorController::setTerrainY(int y) {
    if (!terrain_) {
        if (terrain_y_ == 0) return;
        terrain_y_ = 0;
        terrain_layer_ = 0;
        emit terrainChanged();
        return;
    }
    y = std::clamp(y, terrain_->minY, terrain_->maxY);
    if (terrain_y_ == y) return;
    terrain_y_ = y;
    terrain_layer_ = std::clamp(terrain_layer_, 0, terrainMaxLayer());
    emit terrainChanged();
}

void ChunkEditorController::setTerrainLayer(int layer) {
    layer = std::clamp(layer, 0, terrainMaxLayer());
    if (terrain_layer_ == layer) return;
    terrain_layer_ = layer;
    emit terrainChanged();
}

void ChunkEditorController::copyText(const QString& text) const {
    if (auto* clipboard = QGuiApplication::clipboard()) clipboard->setText(text);
}

bool ChunkEditorController::exportStatistic(int index, const QUrl& url) const {
    if (!statistics_ || index < 0 || index >= static_cast<int>(statistics_->rows.size()) || url.isEmpty()) return false;
    const auto& data = statistics_->rows[static_cast<size_t>(index)].data;
    return io::DocumentIo::writeBytes(url, data).isEmpty();
}

bool ChunkEditorController::saveToCache() {
    if (!dirty()) return true;
    if (!loader_ || !loader_->isOpen() || !has_chunk_) return false;

    auto raw = loader_->getRawChunk(bl::chunk_pos(chunk_x_, chunk_z_, dimension_));
    if (!raw) return false;

    const auto documentPayload = [](const NbtDocumentController& document) {
        std::string payload;
        for (int index = 0; index < document.itemCount(); ++index) {
            auto root = document.itemCopy(index);
            if (root) payload += root->serialize();
        }
        return payload;
    };

    if (pending_ticks_document_.dirty()) raw->set_normal(bl::chunk_key::PendingTicks, documentPayload(pending_ticks_document_));
    if (block_entities_document_.dirty()) raw->set_normal(bl::chunk_key::BlockEntity, documentPayload(block_entities_document_));
    if (entities_document_.dirty()) {
        std::vector<std::unique_ptr<bl::actor>> actor_storage;
        std::vector<bl::actor*> actors;
        actor_storage.reserve(static_cast<size_t>(entities_document_.itemCount()));
        actors.reserve(static_cast<size_t>(entities_document_.itemCount()));
        for (int index = 0; index < entities_document_.itemCount(); ++index) {
            auto root = entities_document_.itemCopy(index);
            auto entity = std::make_unique<bl::actor>();
            if (!root || !entity->load_from_nbt(root.get())) return false;
            actors.push_back(entity.get());
            actor_storage.push_back(std::move(entity));
        }
        raw->set_entities(actors);
    }

    if (!loader_->putRawChunk(*raw)) return false;
    entities_document_.markPersisted();
    block_entities_document_.markPersisted();
    pending_ticks_document_.markPersisted();
    emit dirtyChanged();
    return true;
}

void ChunkEditorController::finishLoad() {
    const auto snapshot = watcher_.result();
    if (!snapshot || snapshot->generation != request_generation_) return;
    setLoading(false);

    error_message_ = snapshot->error;
    chunk_x_ = snapshot->chunkX;
    chunk_z_ = snapshot->chunkZ;
    dimension_ = snapshot->dimension;
    y_range_ = snapshot->yRange;
    has_chunk_ = snapshot->ok;
    terrain_ = snapshot->terrain;
    statistics_ = snapshot->statistics;
    if (terrain_ && !terrain_->layers.empty()) {
        terrain_y_ = std::clamp(0, terrain_->minY, terrain_->maxY);
        terrain_layer_ = std::clamp(terrain_layer_, 0, terrainMaxLayer());
    } else {
        terrain_y_ = 0;
        terrain_layer_ = 0;
    }
    ++voxel_revision_;
    if (snapshot->voxelScene) snapshot->voxelScene->revision = static_cast<std::uint64_t>(voxel_revision_);
    voxel_scene_ = snapshot->voxelScene;
    setVoxelProgress(100);

    entities_document_.loadMemory(
        cloneRoots(snapshot->entities), {}, tr("Entities"),
        [](const bl::nbt::compound_tag* root, int index, const QString&) { return entityPresentation(root, index); });
    block_entities_document_.loadMemory(
        cloneRoots(snapshot->blockEntities), {}, tr("Block entities"),
        [](const bl::nbt::compound_tag* root, int index, const QString&) { return blockEntityPresentation(root, index); });
    pending_ticks_document_.loadMemory(cloneRoots(snapshot->pendingTicks), {}, tr("Pts"));
    emit chunkChanged();
    emit dataChanged();
    emit terrainChanged();
    emit statisticsChanged();
    emit statusChanged();
    emit voxelChanged();
}

void ChunkEditorController::setLoading(bool loading) {
    if (loading_ == loading) return;
    loading_ = loading;
    emit loadingChanged();
}

void ChunkEditorController::setVoxelProgress(int progress) {
    progress = std::clamp(progress, 0, 100);
    if (voxel_progress_ == progress) return;
    voxel_progress_ = progress;
    emit voxelProgressChanged();
}

VoxelSceneController::StructureExportTask ChunkEditorController::structureExportTask(const bl::block_box& localBounds, bool exportEntities,
                                                                                     bool useNewFormat) const {
    if (!loader_ || !loader_->isOpen() || !voxel_scene_) return {};
    const auto origin = voxel_scene_->origin;
    const auto bounds = localBounds.translated({qRound(origin.x()), qRound(origin.y()), qRound(origin.z())});
    return [loader = loader_, dimension = dimension_, bounds, exportEntities, useNewFormat] {
        const auto raw =
            BlockVolumeOperator::exportWorldMcstructureData(*loader, dimension, bounds, false, useNewFormat ? 2 : 1, exportEntities);
        return QByteArray(raw.data(), static_cast<qsizetype>(raw.size()));
    };
}
