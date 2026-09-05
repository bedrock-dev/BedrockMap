#include "regionrenderscheduler.h"

#include <QMetaObject>
#include <algorithm>
#include <cstdint>

RegionRenderScheduler::RegionRenderScheduler(AsyncLevelLoader *loader, QObject *parent)
    : QObject(parent), loader_(loader), owner_thread_(QThread::currentThread()) {
    pool_.setMaxThreadCount(setting::current().THREAD_NUM);
}

void RegionRenderScheduler::assertOwnerThread() const {
    Assert(QThread::currentThread() == owner_thread_, "RegionRenderScheduler", "scheduler state must be accessed from the owning thread");
}

bool RegionRenderScheduler::inViewport(const region_pos &pos) const {
    return viewport_valid_ && pos.dim == viewport_min_.dim && pos.x >= viewport_min_.x && pos.x <= viewport_max_.x &&
           pos.z >= viewport_min_.z && pos.z <= viewport_max_.z;
}

void RegionRenderScheduler::setViewport(const region_pos &minRegion, const region_pos &maxRegion) {
    assertOwnerThread();
    if (viewport_valid_ && viewport_min_ == minRegion && viewport_max_ == maxRegion) return;
    viewport_min_ = minRegion;
    viewport_max_ = maxRegion;
    viewport_valid_ = true;

    // Requests are generated from the visible map area. Drop queued work that
    // is no longer relevant after a pan or zoom; running tasks are allowed to
    // finish because QThreadPool cannot safely interrupt their LevelDB reads.
    discardPendingOutsideViewport();
    rebuildQueue();
    dispatch();
}

void RegionRenderScheduler::request(const region_pos &pos, const MapFilter &filter) {
    assertOwnerThread();
    if (!accepting_.load(std::memory_order_acquire) || active_.count(pos) != 0) return;
    const auto version = ++next_version_;
    pending_.insert_or_assign(pos, PendingTask{pos, filter, version});
    queue_.push(makeQueueEntry(pending_.at(pos)));
    maybeRebuildQueue();
    dispatch();
}

bool RegionRenderScheduler::contains(const region_pos &pos) const {
    assertOwnerThread();
    return active_.count(pos) != 0 || pending_.count(pos) != 0;
}

RegionRenderScheduler::QueueEntry RegionRenderScheduler::makeQueueEntry(const PendingTask &task) const {
    const auto centerX = static_cast<int64_t>(viewport_min_.x) + (static_cast<int64_t>(viewport_max_.x) - viewport_min_.x) / 2;
    const auto centerZ = static_cast<int64_t>(viewport_min_.z) + (static_cast<int64_t>(viewport_max_.z) - viewport_min_.z) / 2;
    const auto dx = static_cast<int64_t>(task.pos.x) - centerX;
    const auto dz = static_cast<int64_t>(task.pos.z) - centerZ;
    return QueueEntry{task.pos, inViewport(task.pos), dx * dx + dz * dz, task.version};
}

void RegionRenderScheduler::maybeRebuildQueue() {
    // Every request adds a new ordering entry. Replacing an existing pending
    // task leaves its old entry behind, and dispatched tasks do the same until
    // their stale entries reach the top. Periodically compact the heap so the
    // amount of bookkeeping remains proportional to live pending work.
    constexpr std::size_t kQueueSlack = 64;
    const auto pendingSize = pending_.size();
    const auto rebuildLimit = pendingSize * 2 + kQueueSlack;
    if (queue_.size() > rebuildLimit) rebuildQueue();
}

void RegionRenderScheduler::discardPendingOutsideViewport() {
    if (!viewport_valid_) return;
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (!inViewport(it->first)) {
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}

void RegionRenderScheduler::rebuildQueue() {
    queue_ = {};
    for (const auto &[pos, task] : pending_) {
        (void)pos;
        queue_.push(makeQueueEntry(task));
    }
}

std::optional<RegionRenderScheduler::PendingTask> RegionRenderScheduler::takeBestPending() {
    for (;;) {
        while (!queue_.empty()) {
            const auto entry = queue_.top();
            queue_.pop();
            const auto it = pending_.find(entry.pos);
            if (it != pending_.end() && it->second.version == entry.version) return std::move(it->second);
        }

        // A queue rebuild is normally triggered by maybeRebuildQueue(), but
        // this fallback keeps the scheduler correct if all heap entries were
        // invalidated between two dispatches.
        if (pending_.empty()) return std::nullopt;
        rebuildQueue();
    }
}

void RegionRenderScheduler::dispatch() {
    while (accepting_.load(std::memory_order_acquire) && active_.size() < static_cast<size_t>(pool_.maxThreadCount()) &&
           !pending_.empty()) {
        maybeRebuildQueue();
        auto next = takeBestPending();
        if (!next) return;
        const auto pos = next->pos;
        auto filter = std::move(next->filter);
        pending_.erase(pos);
        active_.insert(pos);

        auto *task = new LoadRegionTask(loader_, pos, std::move(filter));
        connect(
            task, &LoadRegionTask::finish, this,
            [this, pos](int, int, int, ChunkRegion *region, long long loadTime, long long renderTime, bl::chunk **) {
                onTaskFinished(pos, region, loadTime, renderTime);
            },
            Qt::QueuedConnection);
        pool_.start(task);
    }
}

void RegionRenderScheduler::onTaskFinished(const region_pos &pos, ChunkRegion *region, long long loadTime, long long renderTime) {
    assertOwnerThread();
    active_.erase(pos);
    if (!accepting_.load(std::memory_order_acquire)) {
        delete region;
        return;
    }
    emit regionFinished(pos, region, loadTime, renderTime);
    dispatch();
}

void RegionRenderScheduler::clear() {
    // Stopping the pool is safe from the close worker. The scheduler maps are
    // UI-thread owned and are cleared there after the worker pool has stopped.
    accepting_.store(false, std::memory_order_release);
    pool_.clear();
    pool_.waitForDone();
    if (QThread::currentThread() == owner_thread_) {
        pending_.clear();
        queue_ = {};
        active_.clear();
    } else {
        // Finish on the owning thread after all queued task-completion events
        // already posted by the worker have been delivered. This is a small
        // bookkeeping barrier, not a synchronous wait for rendering work.
        QMetaObject::invokeMethod(
            this,
            [this]() {
                pending_.clear();
                queue_ = {};
                active_.clear();
            },
            Qt::BlockingQueuedConnection);
    }
}

void RegionRenderScheduler::waitForDone() {
    assertOwnerThread();
    pool_.waitForDone();
}

int RegionRenderScheduler::pendingCount() const {
    assertOwnerThread();
    return static_cast<int>(pending_.size());
}

int RegionRenderScheduler::activeCount() const {
    assertOwnerThread();
    return static_cast<int>(active_.size());
}
