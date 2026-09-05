#include "chunkio.h"

#include <cstring>

#include "loguru/loguru.hpp"

static constexpr char MAGIC[] = "BCHKS";
static constexpr int8_t VERSION = 1;

bool ExportedRegion::isEmpty() const { return chunks_.empty(); }

void ExportedRegion::addChunk(const bl::raw_chunk &chunk) { chunks_.push_back(chunk); }

void ExportedRegion::removeChunk(const bl::chunk_pos &pos) {
    chunks_.erase(std::remove_if(chunks_.begin(), chunks_.end(), [&pos](const bl::raw_chunk &c) { return c.pos() == pos; }), chunks_.end());
}

void ExportedRegion::clear() { chunks_.clear(); }

std::vector<char> ExportedRegion::serialize() {
    // Compute total size
    size_t total = 6;  // magic(5) + version(1)
    total += 4;        // chunk count
    for (auto &chunk : chunks_) {
        auto raw = chunk.to_raw();
        total += 4 + raw.size();  // size prefix + data
    }

    std::vector<char> buf(total);
    char *p = buf.data();

    // magic + version
    std::memcpy(p, MAGIC, 5);
    p += 5;
    *p++ = VERSION;

    // chunk count
    int32_t n = static_cast<int32_t>(chunks_.size());
    std::memcpy(p, &n, 4);
    p += 4;

    for (auto &chunk : chunks_) {
        auto raw = chunk.to_raw();
        int32_t size = static_cast<int32_t>(raw.size());
        std::memcpy(p, &size, 4);
        p += 4;
        std::memcpy(p, raw.data(), size);
        p += size;
    }

    return buf;
}

ExportedRegion ExportedRegion::deserialize(const char *data, size_t size) {
    ExportedRegion reg;
    if (!data || size < 10) return reg;

    const char *p = data;
    if (std::memcmp(p, MAGIC, 5) != 0) return reg;
    p += 5;
    if (*p++ != VERSION) return reg;

    int32_t chunkCount;
    std::memcpy(&chunkCount, p, 4);
    p += 4;
    if (chunkCount < 0 || static_cast<size_t>(chunkCount) > (size - 10) / 4) return {};

    for (int32_t i = 0; i < chunkCount; i++) {
        const size_t offset = static_cast<size_t>(p - data);
        if (offset > size || size - offset < 4) return {};
        int32_t chunkSize;
        std::memcpy(&chunkSize, p, 4);
        p += 4;
        if (chunkSize < 0) return {};

        const size_t payloadOffset = static_cast<size_t>(p - data);
        const size_t payloadSize = static_cast<size_t>(chunkSize);
        if (payloadOffset > size || payloadSize > size - payloadOffset) return {};

        bl::raw_chunk chunk;
        std::vector<byte_t> raw(p, p + chunkSize);
        if (!chunk.from_raw(raw)) return {};
        p += chunkSize;

        reg.chunks_.push_back(std::move(chunk));
    }

    return reg;
}

std::vector<char> ExportedRegion::embedInPng(const QImage &image, const std::vector<char> &payload) { return {}; }

std::vector<char> ExportedRegion::extractFromPng(const char *data, size_t size) { return {}; }
