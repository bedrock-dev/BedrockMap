#ifndef BEDROCKMAP_CHUNKCOORDSSERVICE_H
#define BEDROCKMAP_CHUNKCOORDSSERVICE_H

#include <QThreadPool>
#include <atomic>
#include <functional>
#include <optional>

#include "chunkcoords.h"

namespace leveldb {
    class DB;
}

/// Coordinates the two-stage chunk coordinate index lifecycle for one level.
class ChunkCoordsService {
   public:
    ChunkCoordsService();
    ~ChunkCoordsService();

    void start(leveldb::DB *db, bool preloadAll, std::function<void()> ready);
    void close();

    bool ready() const { return ready_.load(std::memory_order_acquire); }
    bool loading(bool preloadAll) const { return preloadAll && !ready(); }

    void enqueueUpdate(const bl::chunk_pos &pos, bool present, std::function<void()> finished = {});

    const ChunkCoordsIndex &index() const { return index_; }
    QImage image(const region_pos &pos) const { return index_.image(pos); }
    std::optional<ChunkCoordsBoundingBox> boundingBox(int dim) const { return index_.boundingBox(dim); }

   private:
    QThreadPool preload_pool_;
    std::atomic_bool stop_{false};
    std::atomic_bool ready_{false};
    ChunkCoordsIndex index_;
};

#endif  // BEDROCKMAP_CHUNKCOORDSSERVICE_H
