#ifndef BEDROCKMAP_CHUNKCOORDSSERVICE_H
#define BEDROCKMAP_CHUNKCOORDSSERVICE_H

#include <QObject>
#include <QThreadPool>
#include <atomic>
#include <functional>
#include <optional>

#include "loader/chunkcoords.h"

namespace bl {
    class bedrock_level;
}

/// Coordinates the two-stage chunk coordinate index lifecycle for one level.
class ChunkCoordsService : public QObject {
    Q_OBJECT

   public:
    ChunkCoordsService();
    ~ChunkCoordsService();

    void start(bl::bedrock_level& level, std::function<void()> ready, bool overview = false);
    void close();

    bool ready() const { return ready_.load(std::memory_order_acquire); }

    /// Mirror one chunk write into the index. Called on the thread performing the
    /// edit, before the edit is reported as complete, so the index never trails
    /// the storage cache. Writes made during the initial scan are replayed when
    /// it finishes.
    void update(const bl::chunk_pos& pos, bool present);
    void updateOverview(const bl::raw_chunk& raw) {
        if (overviewEnabled()) index_.updateOverview(raw);
    }
    bool overviewEnabled() const { return overview_enabled_.load(std::memory_order_acquire); }

    const ChunkCoordsIndex& index() const { return index_; }
    std::optional<ChunkCoordsBoundingBox> boundingBox(int dim) const { return index_.boundingBox(dim); }

   signals:
    /// Progress from the initial full-database scan.
    void preloadProgress(qulonglong scannedKeys, qulonglong chunks);
    /// Emitted after the scan switches to the interactive update phase.
    void preloadFinished();

   private:
    QThreadPool preload_pool_;
    std::atomic_bool stop_{false};
    std::atomic_bool ready_{false};
    std::atomic_bool overview_enabled_{false};
    ChunkCoordsIndex index_;
};

#endif  // BEDROCKMAP_CHUNKCOORDSSERVICE_H
