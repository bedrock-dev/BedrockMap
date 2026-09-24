#include "chunkcoordsservice.h"

#include <QRunnable>

ChunkCoordsService::ChunkCoordsService() { preload_pool_.setMaxThreadCount(1); }

ChunkCoordsService::~ChunkCoordsService() { close(); }

void ChunkCoordsService::start(bl::bedrock_level& level, bool preloadAll, std::function<void()> ready) {
    close();
    stop_.store(false, std::memory_order_release);
    if (!preloadAll) {
        index_.finishScan();
        return;
    }

    preload_pool_.start(QRunnable::create([this, level_ptr = &level, ready = std::move(ready)]() mutable {
        const auto progress = [this](std::uint64_t scannedKeys, std::uint64_t chunks) {
            emit preloadProgress(static_cast<qulonglong>(scannedKeys), static_cast<qulonglong>(chunks));
        };
        if (index_.load(*level_ptr, stop_, progress) && !stop_.load(std::memory_order_acquire)) {
            ready_.store(true, std::memory_order_release);
            emit preloadFinished();
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

void ChunkCoordsService::update(const bl::chunk_pos& pos, bool present) { index_.updateChunk(pos, present); }
