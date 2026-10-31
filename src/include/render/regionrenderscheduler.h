#ifndef BEDROCKMAP_REGIONRENDERSCHEDULER_H
#define BEDROCKMAP_REGIONRENDERSCHEDULER_H

#include <QObject>
#include <QThread>
#include <QThreadPool>
#include <atomic>
#include <cstdint>
#include <optional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "loader/chunkregion.h"

class AsyncLevelLoader;
class ResourceManager;

/// Owns region render tasks and schedules pending work according to the most
/// recently reported viewport. This object is UI-thread owned; task completion
/// is delivered back through Qt's queued signal connection.
class RegionRenderScheduler : public QObject {
    Q_OBJECT

   public:
    explicit RegionRenderScheduler(AsyncLevelLoader* loader, ResourceManager* resources, QObject* parent = nullptr);

    void setViewport(const region_pos& minRegion, const region_pos& maxRegion);
    void request(const region_pos& pos, const MapFilter& filter, const TerrainTileRequest& tile_request = {});

    bool contains(const region_pos& pos) const;
    /// Stop dispatching and drop queued work without waiting for running bakes.
    /// Running tasks finish on their pool threads and their results are discarded.
    void pause();
    /// Resume dispatch for the retained viewport and any work requested later.
    void resume();
    /// Drop queued work and ignore running results without waiting for them.
    void reset();
    void clear();
    void waitForDone();
    int pendingCount() const;
    int activeCount() const;

   signals:
    void regionFinished(RegionRenderResult result);

   private:
    void assertOwnerThread() const;

    struct PendingTask {
        region_pos pos;
        MapFilter filter;
        TerrainTileRequest tile_request;
        std::uint64_t version{0};
    };

    struct QueueEntry {
        region_pos pos;
        bool inViewport{false};
        std::int64_t distance{0};
        std::uint64_t version{0};
    };

    struct QueueCompare {
        bool operator()(const QueueEntry& lhs, const QueueEntry& rhs) const {
            if (lhs.inViewport != rhs.inViewport) return !lhs.inViewport;
            if (lhs.distance != rhs.distance) return lhs.distance > rhs.distance;
            return lhs.version > rhs.version;
        }
    };

    bool inViewport(const region_pos& pos) const;
    QueueEntry makeQueueEntry(const PendingTask& task) const;
    void maybeRebuildQueue();
    void discardPendingOutsideViewport();
    void rebuildQueue();
    std::optional<PendingTask> takeBestPending();
    void dispatch();
    void onTaskFinished(RegionRenderResult result);

    AsyncLevelLoader* loader_;
    ResourceManager* resources_;
    QThreadPool pool_;
    std::unordered_map<region_pos, PendingTask> pending_;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, QueueCompare> queue_;
    std::unordered_set<region_pos> active_;
    region_pos viewport_min_{};
    region_pos viewport_max_{};
    bool viewport_valid_{false};
    std::atomic_bool accepting_{true};
    std::uint64_t next_version_{0};
    std::uint64_t task_generation_{0};
    QThread* owner_thread_{nullptr};
};

#endif  // BEDROCKMAP_REGIONRENDERSCHEDULER_H
