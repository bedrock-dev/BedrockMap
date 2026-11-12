#ifndef BEDROCKMAP_CHUNKEDITORCONTROLLER_H
#define BEDROCKMAP_CHUNKEDITORCONTROLLER_H

#include <QByteArray>
#include <QColor>
#include <QFutureWatcher>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <array>
#include <memory>
#include <vector>

#include "controller/nbtdocumentcontroller.h"
#include "controller/voxelscenecontroller.h"

class AsyncLevelLoader;
namespace bl::nbt {
    class compound_tag;
}

struct ChunkTerrainBlockSnapshot {
    QString name;
    QString palette;
    QColor color;
};

struct ChunkTerrainCellSnapshot {
    QVector<ChunkTerrainBlockSnapshot> blocks;
    int biome{0};
    QString biomeName;
    QColor biomeColor;
};

struct ChunkTerrainLayerSnapshot {
    int y{0};
    std::array<ChunkTerrainCellSnapshot, 256> cells;
};

struct ChunkTerrainSnapshot {
    int minY{0};
    int maxY{-1};
    std::vector<ChunkTerrainLayerSnapshot> layers;
};

struct ChunkStatisticRowSnapshot {
    QString name;
    qint64 size{0};
    QString hash;
    QByteArray data;
};

struct ChunkStatisticsSnapshot {
    std::vector<ChunkStatisticRowSnapshot> rows;
};

struct ChunkNbtSnapshot {
    quint64 generation{0};
    int chunkX{0};
    int chunkZ{0};
    int dimension{0};
    QString yRange;
    QString error;
    std::vector<std::shared_ptr<bl::nbt::compound_tag>> entities;
    std::vector<std::shared_ptr<bl::nbt::compound_tag>> blockEntities;
    std::vector<std::shared_ptr<bl::nbt::compound_tag>> pendingTicks;
    std::shared_ptr<ChunkTerrainSnapshot> terrain;
    std::shared_ptr<ChunkStatisticsSnapshot> statistics;
    std::shared_ptr<VoxelSceneData> voxelScene;
    bool ok{false};
};

/// Chunk editor data for the QML frontend. Parsing runs on a worker so opening
/// a chunk never blocks the map or its render thread.
class ChunkEditorController : public VoxelSceneController {
    Q_OBJECT
    Q_PROPERTY(bool hasChunk READ hasChunk NOTIFY chunkChanged)
    Q_PROPERTY(int chunkX READ chunkX NOTIFY chunkChanged)
    Q_PROPERTY(int chunkZ READ chunkZ NOTIFY chunkChanged)
    Q_PROPERTY(int dimension READ dimension NOTIFY chunkChanged)
    Q_PROPERTY(QString yRange READ yRange NOTIFY chunkChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(int terrainMinY READ terrainMinY NOTIFY terrainChanged)
    Q_PROPERTY(int terrainMaxY READ terrainMaxY NOTIFY terrainChanged)
    Q_PROPERTY(int terrainY READ terrainY WRITE setTerrainY NOTIFY terrainChanged)
    Q_PROPERTY(int terrainLayer READ terrainLayer WRITE setTerrainLayer NOTIFY terrainChanged)
    Q_PROPERTY(int terrainMaxLayer READ terrainMaxLayer NOTIFY terrainChanged)
    Q_PROPERTY(QVariantList terrainCells READ terrainCells NOTIFY terrainChanged)
    Q_PROPERTY(QVariantList statistics READ statistics NOTIFY statisticsChanged)
    Q_PROPERTY(int voxelRevision READ voxelRevision NOTIFY voxelChanged)
    Q_PROPERTY(NbtDocumentController* entitiesDocument READ entitiesDocument CONSTANT)
    Q_PROPERTY(NbtDocumentController* blockEntitiesDocument READ blockEntitiesDocument CONSTANT)
    Q_PROPERTY(NbtDocumentController* pendingTicksDocument READ pendingTicksDocument CONSTANT)

   public:
    explicit ChunkEditorController(QObject* parent);
    ~ChunkEditorController() override;

    [[nodiscard]] bool loading() const override { return loading_; }
    [[nodiscard]] bool hasChunk() const { return has_chunk_; }
    [[nodiscard]] int chunkX() const { return chunk_x_; }
    [[nodiscard]] int chunkZ() const { return chunk_z_; }
    [[nodiscard]] int dimension() const { return dimension_; }
    [[nodiscard]] QString yRange() const { return y_range_; }
    [[nodiscard]] QString errorMessage() const override { return error_message_; }
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] int terrainMinY() const;
    [[nodiscard]] int terrainMaxY() const;
    [[nodiscard]] int terrainY() const { return terrain_y_; }
    [[nodiscard]] int terrainLayer() const { return terrain_layer_; }
    [[nodiscard]] int terrainMaxLayer() const;
    [[nodiscard]] QVariantList terrainCells() const;
    [[nodiscard]] QVariantList statistics() const;
    [[nodiscard]] int voxelProgress() const override { return voxel_progress_; }
    [[nodiscard]] int voxelRevision() const { return voxel_revision_; }
    [[nodiscard]] std::shared_ptr<const VoxelSceneData> voxelScene() const override { return voxel_scene_; }
    [[nodiscard]] NbtDocumentController* entitiesDocument() { return &entities_document_; }
    [[nodiscard]] NbtDocumentController* blockEntitiesDocument() { return &block_entities_document_; }
    [[nodiscard]] NbtDocumentController* pendingTicksDocument() { return &pending_ticks_document_; }
    void setClipboard(BinaryClipboard* clipboard);

    Q_INVOKABLE void openChunk(int chunkX, int chunkZ, int dimension);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void setTerrainY(int y);
    Q_INVOKABLE void setTerrainLayer(int layer);
    Q_INVOKABLE void copyText(const QString& text) const;
    Q_INVOKABLE [[nodiscard]] bool exportStatistic(int index, const QUrl& url) const;
    /// Write the current editor documents into the loader's raw chunk cache.
    /// The world save remains responsible for committing that cache to disk.
    Q_INVOKABLE [[nodiscard]] bool saveToCache();
    void setLoader(AsyncLevelLoader* loader);

   signals:
    void chunkChanged();
    void dataChanged();
    void dirtyChanged();
    void terrainChanged();
    void statisticsChanged();

   protected:
    [[nodiscard]] StructureExportTask structureExportTask(const bl::block_box& localBounds, bool exportEntities,
                                                          bool useNewFormat) const override;

   private:
    void finishLoad();
    void setLoading(bool loading);
    void setVoxelProgress(int progress);
    [[nodiscard]] const ChunkTerrainLayerSnapshot* terrainLayerAt(int y) const;

    AsyncLevelLoader* loader_{nullptr};
    QFutureWatcher<std::shared_ptr<ChunkNbtSnapshot>> watcher_;
    quint64 request_generation_{0};
    bool loading_{false};
    bool has_chunk_{false};
    int chunk_x_{0};
    int chunk_z_{0};
    int dimension_{0};
    QString y_range_;
    QString error_message_;
    std::shared_ptr<const ChunkTerrainSnapshot> terrain_;
    int terrain_y_{0};
    int terrain_layer_{0};
    std::shared_ptr<const ChunkStatisticsSnapshot> statistics_;
    int voxel_progress_{0};
    int voxel_revision_{0};
    std::shared_ptr<const VoxelSceneData> voxel_scene_;
    NbtDocumentController entities_document_;
    NbtDocumentController block_entities_document_;
    NbtDocumentController pending_ticks_document_;
};

#endif  // BEDROCKMAP_CHUNKEDITORCONTROLLER_H
