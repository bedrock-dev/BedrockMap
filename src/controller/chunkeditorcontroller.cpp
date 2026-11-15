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
#include <utility>
#include <vector>

#include "actor.h"
#include "bedrock_key.h"
#include "chunk.h"
#include "color.h"
#include "io/documentio.h"
#include "loader/asynclevelloader.h"
#include "nbt.h"
#include "raw_chunk.h"
#include "render/voxelmesh.h"

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
        auto parsed = bl::nbt::parse_all(std::span<const byte_t>(data.data(), data.size()));
        if (!parsed) {
            LOG_F(ERROR, "Cannot parse chunk record: %s", bl::nbt::parse_error_to_string(parsed.error_code));
            return;
        }
        for (auto* root : parsed.roots) {
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

    bool supportedHsaType(bl::HSAType type) {
        return type == bl::NetherFortress || type == bl::SwampHut || type == bl::OceanMonument || type == bl::PillagerOutpost;
    }

    QString hsaTypeName(bl::HSAType type) {
        switch (type) {
            case bl::NetherFortress:
                return ChunkEditorController::tr("Nether Fortress");
            case bl::SwampHut:
                return ChunkEditorController::tr("Swamp Hut");
            case bl::OceanMonument:
                return ChunkEditorController::tr("Ocean Monument");
            case bl::PillagerOutpost:
                return ChunkEditorController::tr("Pillager Outpost");
            case bl::Unknown:
                return ChunkEditorController::tr("Unknown");
        }
        return ChunkEditorController::tr("Unknown");
    }

    QColor hsaTypeColor(bl::HSAType type) {
        switch (type) {
            case bl::NetherFortress:
                return QColor(0, 223, 162);
            case bl::SwampHut:
                return QColor(255, 0, 96);
            case bl::OceanMonument:
                return QColor(246, 250, 112);
            case bl::PillagerOutpost:
                return QColor(0, 121, 255);
            case bl::Unknown:
                return QColor(160, 166, 176);
        }
        return QColor(160, 166, 176);
    }

    /// Fill the terrain snapshot and build the voxel scene in one pass, so each
    /// block's color and biome blend are computed once.
    std::shared_ptr<VoxelSceneData> buildVoxelScene(const bl::raw_chunk& raw, const std::function<void(int)>& reportProgress,
                                                    ChunkTerrainSnapshot& terrain) {
        auto chunk = std::make_unique<bl::chunk>(raw.pos());
        if (!chunk->load_from_raw_chunk(raw, bl::chunk_load_policy::Terrain | bl::chunk_load_policy::Others)) return {};
        const auto [worldMinY, worldMaxY] = raw.get_y_range();
        const int layerCount = std::max(0, worldMaxY - worldMinY + 1);
        if (layerCount == 0) return {};

        terrain.minY = worldMinY;
        terrain.maxY = worldMaxY;
        terrain.layers.resize(static_cast<size_t>(layerCount));
        VoxelGrid grid(16, layerCount, 16);
        for (int worldY = worldMinY; worldY <= worldMaxY; ++worldY) {
            const int y = worldY - worldMinY;
            auto& terrainLayer = terrain.layers[static_cast<size_t>(y)];
            terrainLayer.y = worldY;
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    const auto biome = chunk->get_biome(x, worldY, z);
                    auto& cell = terrainLayer.cells[static_cast<size_t>(z * 16 + x)];
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
                        // The 3D preview shows the primary block layer only.
                        if (blockLayer == 0 && visibleBlock) grid.set(x, y, z, qRgba(blended.r, blended.g, blended.b, blended.a));
                    }
                }
            }
            if (y % 32 == 0) reportProgress(35 + y * 25 / layerCount);
        }
        const int firstWorldY = worldMinY + grid.trimEmptyY();
        reportProgress(65);
        const QVector3D origin(raw.pos().x * 16, firstWorldY, raw.pos().z * 16);
        auto scene = VoxelSceneData::fromGrid(std::move(grid), origin);
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
            bl::hardcoded_spawn_area_list hsa_list;
            if (hsa_list.from_raw(raw->get_normal_key(bl::chunk_key::HardCodedSpawnAreas))) {
                snapshot->hsaAreas.reserve(hsa_list.size());
                for (const auto& area : hsa_list) {
                    if (!supportedHsaType(area.type)) continue;
                    snapshot->hsaAreas.push_back(ChunkHsaAreaSnapshot{
                        static_cast<int>(area.type),
                        area.min_pos.x,
                        area.min_pos.y,
                        area.min_pos.z,
                        area.max_pos.x,
                        area.max_pos.y,
                        area.max_pos.z,
                    });
                }
            }
            if (!raw->get_actor_digest().empty())
                appendStatistic(*snapshot->statistics, QStringLiteral("ActorDigest"), raw->get_actor_digest());
            for (const auto& [key, data] : raw->get_entities()) {
                if (key.size() != 8) continue;
                const QString hexKey = QString::fromLatin1(QByteArray::fromRawData(key.data(), static_cast<qsizetype>(key.size())).toHex());
                appendStatistic(*snapshot->statistics, QStringLiteral("Entity[%1]").arg(hexKey), data);
            }
            snapshot->terrain = std::make_shared<ChunkTerrainSnapshot>();
            snapshot->voxelScene = buildVoxelScene(*raw, reportProgress, *snapshot->terrain);
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

void ChunkEditorController::setClipboard(BinaryClipboard* clipboard) {
    entities_document_.setClipboard(clipboard);
    block_entities_document_.setClipboard(clipboard);
    pending_ticks_document_.setClipboard(clipboard);
}

ChunkEditorController::~ChunkEditorController() {
    if (watcher_.isRunning()) watcher_.waitForFinished();
}

void ChunkEditorController::setLoader(AsyncLevelLoader* loader) {
    if (loader_ == loader) return;
    if (watcher_.isRunning()) watcher_.waitForFinished();
    clear();
    loader_ = loader;
}

void ChunkEditorController::openChunk(int chunkX, int chunkZ, int dimension) {
    if (!loader_ || !loader_->isOpen()) return;
    if (watcher_.isRunning()) {
        ++request_generation_;
        pending_open_ = [this, chunkX, chunkZ, dimension] { openChunk(chunkX, chunkZ, dimension); };
        return;
    }
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
    hsa_areas_.clear();
    hsa_dirty_ = false;
    terrain_y_ = 0;
    terrain_layer_ = 0;
    voxel_scene_.reset();
    setLoadProgress(0);
    setLoading(true);
    emit chunkChanged();
    emit terrainChanged();
    emit statisticsChanged();
    emit hsaChanged();
    emit dirtyChanged();
    emit statusChanged();

    const quint64 generation = request_generation_;
    watcher_.setFuture(QtConcurrent::run([loader = loader_, pos = bl::chunk_pos(chunkX, chunkZ, dimension), generation,
                                          reportProgress = progressReporter()] {
        return loadChunkSnapshot(loader, pos, generation, reportProgress);
    }));
}

void ChunkEditorController::clear() {
    ++request_generation_;
    pending_open_ = {};
    setLoading(false);
    has_chunk_ = false;
    y_range_.clear();
    error_message_.clear();
    entities_document_.loadMemory({}, {}, tr("Entities"));
    block_entities_document_.loadMemory({}, {}, tr("Block entities"));
    pending_ticks_document_.loadMemory({}, {}, tr("Pts"));
    terrain_.reset();
    statistics_.reset();
    hsa_areas_.clear();
    hsa_dirty_ = false;
    terrain_y_ = 0;
    terrain_layer_ = 0;
    voxel_scene_.reset();
    setLoadProgress(0);
    emit voxelChanged();
    emit chunkChanged();
    emit terrainChanged();
    emit statisticsChanged();
    emit hsaChanged();
    emit dirtyChanged();
    emit statusChanged();
}

bool ChunkEditorController::dirty() const {
    return hsa_dirty_ || entities_document_.dirty() || block_entities_document_.dirty() || pending_ticks_document_.dirty();
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

QVariantList ChunkEditorController::hsaAreas() const {
    QVariantList result;
    result.reserve(static_cast<qsizetype>(hsa_areas_.size()));
    for (size_t index = 0; index < hsa_areas_.size(); ++index) {
        const auto& area = hsa_areas_[index];
        const auto type = static_cast<bl::HSAType>(area.type);
        QVariantMap value;
        value.insert(QStringLiteral("index"), static_cast<int>(index));
        value.insert(QStringLiteral("type"), area.type);
        value.insert(QStringLiteral("typeName"), hsaTypeName(type));
        value.insert(QStringLiteral("color"), hsaTypeColor(type));
        value.insert(QStringLiteral("minX"), area.minX);
        value.insert(QStringLiteral("minY"), area.minY);
        value.insert(QStringLiteral("minZ"), area.minZ);
        value.insert(QStringLiteral("maxX"), area.maxX);
        value.insert(QStringLiteral("maxY"), area.maxY);
        value.insert(QStringLiteral("maxZ"), area.maxZ);
        result.push_back(value);
    }
    return result;
}

QVariantList ChunkEditorController::hsaTypes() const {
    QVariantList result;
    for (const auto type : {bl::NetherFortress, bl::SwampHut, bl::OceanMonument, bl::PillagerOutpost}) {
        QVariantMap value;
        value.insert(QStringLiteral("value"), static_cast<int>(type));
        value.insert(QStringLiteral("name"), hsaTypeName(type));
        result.push_back(value);
    }
    return result;
}

int ChunkEditorController::addHsa(int type, int minX, int minY, int minZ, int maxX, int maxY, int maxZ) {
    if (!has_chunk_ || !supportedHsaType(static_cast<bl::HSAType>(type)) || minX > maxX || minY > maxY || minZ > maxZ) return -1;
    hsa_areas_.push_back(ChunkHsaAreaSnapshot{type, minX, minY, minZ, maxX, maxY, maxZ});
    hsa_dirty_ = true;
    emit hsaChanged();
    emit dirtyChanged();
    return static_cast<int>(hsa_areas_.size()) - 1;
}

bool ChunkEditorController::setHsaBounds(int index, int minX, int minY, int minZ, int maxX, int maxY, int maxZ) {
    if (!has_chunk_ || index < 0 || index >= static_cast<int>(hsa_areas_.size()) || minX > maxX || minY > maxY || minZ > maxZ)
        return false;
    auto& area = hsa_areas_[static_cast<size_t>(index)];
    if (area.minX == minX && area.minY == minY && area.minZ == minZ && area.maxX == maxX && area.maxY == maxY && area.maxZ == maxZ)
        return true;
    area.minX = minX;
    area.minY = minY;
    area.minZ = minZ;
    area.maxX = maxX;
    area.maxY = maxY;
    area.maxZ = maxZ;
    hsa_dirty_ = true;
    emit hsaChanged();
    emit dirtyChanged();
    return true;
}

bool ChunkEditorController::setHsaType(int index, int type) {
    if (!has_chunk_ || index < 0 || index >= static_cast<int>(hsa_areas_.size())) return false;
    if (!supportedHsaType(static_cast<bl::HSAType>(type))) return false;
    auto& area = hsa_areas_[static_cast<size_t>(index)];
    if (area.type == type) return true;
    area.type = type;
    hsa_dirty_ = true;
    emit hsaChanged();
    emit dirtyChanged();
    return true;
}

bool ChunkEditorController::removeHsa(int index) {
    if (!has_chunk_ || index < 0 || index >= static_cast<int>(hsa_areas_.size())) return false;
    hsa_areas_.erase(hsa_areas_.begin() + index);
    hsa_dirty_ = true;
    emit hsaChanged();
    emit dirtyChanged();
    return true;
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
        for (int index = 0; index < document.rootCount(); ++index) {
            auto root = document.itemCopy(index);
            if (root) payload += root->serialize();
        }
        return payload;
    };

    if (hsa_dirty_) {
        bl::hardcoded_spawn_area_list list;
        for (const auto& area : hsa_areas_) {
            list.add(bl::hardcoded_spawn_area{
                static_cast<bl::HSAType>(area.type),
                bl::block_pos{area.minX, area.minY, area.minZ},
                bl::block_pos{area.maxX, area.maxY, area.maxZ},
            });
        }
        const std::string hsa_payload = hsa_areas_.empty() ? std::string{} : list.to_raw();
        raw->set_normal(bl::chunk_key::HardCodedSpawnAreas, hsa_payload);
    }
    if (pending_ticks_document_.dirty()) raw->set_normal(bl::chunk_key::PendingTicks, documentPayload(pending_ticks_document_));
    if (block_entities_document_.dirty()) raw->set_normal(bl::chunk_key::BlockEntity, documentPayload(block_entities_document_));
    if (entities_document_.dirty()) {
        std::vector<std::unique_ptr<bl::actor>> actor_storage;
        std::vector<bl::actor*> actors;
        actor_storage.reserve(static_cast<size_t>(entities_document_.rootCount()));
        actors.reserve(static_cast<size_t>(entities_document_.rootCount()));
        for (int index = 0; index < entities_document_.rootCount(); ++index) {
            auto root = entities_document_.itemCopy(index);
            auto entity = std::make_unique<bl::actor>();
            if (!root || !entity->load_from_nbt(root.get())) return false;
            actors.push_back(entity.get());
            actor_storage.push_back(std::move(entity));
        }
        raw->set_entities(actors);
    }

    if (!loader_->putRawChunk(*raw)) return false;
    hsa_dirty_ = false;
    entities_document_.markPersisted();
    block_entities_document_.markPersisted();
    pending_ticks_document_.markPersisted();
    emit dirtyChanged();
    emit chunkSavedToCache(chunk_x_, chunk_z_, dimension_);
    return true;
}

void ChunkEditorController::finishLoad() {
    if (pending_open_) {
        const auto open = std::exchange(pending_open_, {});
        open();
        return;
    }
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
    hsa_areas_ = snapshot->hsaAreas;
    hsa_dirty_ = false;
    if (terrain_ && !terrain_->layers.empty()) {
        terrain_y_ = std::clamp(0, terrain_->minY, terrain_->maxY);
        terrain_layer_ = std::clamp(terrain_layer_, 0, terrainMaxLayer());
    } else {
        terrain_y_ = 0;
        terrain_layer_ = 0;
    }
    voxel_scene_ = snapshot->voxelScene;
    setLoadProgress(100);

    entities_document_.loadMemory(
        cloneRoots(snapshot->entities), {}, tr("Entities"),
        [](const bl::nbt::compound_tag* root, int index, const QString&) { return entityPresentation(root, index); });
    block_entities_document_.loadMemory(
        cloneRoots(snapshot->blockEntities), {}, tr("Block entities"),
        [](const bl::nbt::compound_tag* root, int index, const QString&) { return blockEntityPresentation(root, index); });
    pending_ticks_document_.loadMemory(cloneRoots(snapshot->pendingTicks), {}, tr("Pts"));
    emit chunkChanged();
    emit terrainChanged();
    emit statisticsChanged();
    emit hsaChanged();
    emit dirtyChanged();
    emit statusChanged();
    emit voxelChanged();
}

void ChunkEditorController::setLoading(bool loading) {
    if (loading_ == loading) return;
    loading_ = loading;
    emit loadingChanged();
}
