#include "chunkcoordsservice.h"

#include <leveldb/db.h>

#include <QRunnable>

ChunkCoordsService::ChunkCoordsService() { preload_pool_.setMaxThreadCount(1); }

ChunkCoordsService::~ChunkCoordsService() { close(); }

void ChunkCoordsService::start(leveldb::DB *db, bool preloadAll, std::function<void()> ready) {
    close();
    stop_.store(false, std::memory_order_release);
    if (!preloadAll) {
        index_.beginInteractivePhase();
        return;
    }

    preload_pool_.start(QRunnable::create([this, db, ready = std::move(ready)]() mutable {
        if (index_.load(db, stop_) && !stop_.load(std::memory_order_acquire)) {
            ready_.store(true, std::memory_order_release);
            if (ready) ready();
        }
    }));
}

void ChunkCoordsService::close() {
    stop_.store(true, std::memory_order_release);
    preload_pool_.clear();
    preload_pool_.waitForDone();
    ready_.store(false, std::memory_order_release);
    index_.clear();
}

void ChunkCoordsService::enqueueUpdate(const bl::chunk_pos &pos, bool present, std::function<void()> finished) {
    if (!ready()) return;
    index_.enqueueUpdate(pos, present, std::move(finished));
}
