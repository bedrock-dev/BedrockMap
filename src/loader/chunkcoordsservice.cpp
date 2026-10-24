#include "loader/chunkcoordsservice.h"

#include <QRunnable>

ChunkCoordsService::ChunkCoordsService() { preload_pool_.setMaxThreadCount(1); }

ChunkCoordsService::~ChunkCoordsService() { close(); }

void ChunkCoordsService::start(bl::bedrock_level& level, std::function<void()> ready, bool overview) {
    close();
    stop_.store(false, std::memory_order_release);
    overview_enabled_.store(overview, std::memory_order_release);
    preload_pool_.start(QRunnable::create([this, level_ptr = &level, ready = std::move(ready), overview]() mutable {
        const auto progress = [this](std::uint64_t scannedKeys, std::uint64_t chunks) {
            emit preloadProgress(static_cast<qulonglong>(scannedKeys), static_cast<qulonglong>(chunks));
        };
        ChunkCoordsLoadOptions options;
        options.build_overview = overview;
        if (index_.load(*level_ptr, stop_, progress, nullptr, options) && !stop_.load(std::memory_order_acquire)) {
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
    overview_enabled_.store(false, std::memory_order_release);
    index_.clear();
}

void ChunkCoordsService::update(const bl::chunk_pos& pos, bool present) {
    index_.updateChunk(pos, present);
    if (!present && overviewEnabled()) index_.updateOverview(pos, 0);
}
