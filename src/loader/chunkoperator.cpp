#include "loader/chunkoperator.h"

#include <qlogging.h>

#include <QUrl>
#include <functional>

#include "io/documentio.h"
#include "loader/asynclevelloader.h"
#include "loader/exportedregion.h"
#include "loguru/loguru.hpp"

namespace {

    void forEachChunkInRegion(const QRegion& chunkRegion, int dim, const std::function<void(const bl::chunk_pos&)>& fn) {
        for (const auto& r : chunkRegion) {
            int x1 = r.x();
            int z1 = r.y();
            int x2 = r.x() + r.width() - 1;
            int z2 = r.y() + r.height() - 1;
            for (int x = x1; x <= x2; x++) {
                for (int z = z1; z <= z2; z++) {
                    fn(bl::chunk_pos(x, z, dim));
                }
            }
        }
    }

}  // namespace

bool ChunkOperator::exportRegion(const QRegion& chunkRegion, const QUrl& url, AsyncLevelLoader& loader, int dim) {
    ExportedRegion region;
    forEachChunkInRegion(chunkRegion, dim, [&](const bl::chunk_pos& cp) {
        auto* ref = loader.getChunk(cp);
        if (!ref) return;
        bl::raw_chunk raw(cp);
        if (!raw.read(loader.level())) return;
        region.addChunk(raw);
        delete ref;
    });

    auto data = region.serialize();
    if (region.isEmpty()) return false;
    const QByteArray bytes(data.data(), static_cast<qsizetype>(data.size()));
    const QString error = io::DocumentIo::writeBytes(url, bytes);
    if (!error.isEmpty()) {
        LOG_F(WARNING, "ChunkExporter: cannot write to %s", url.toString(QUrl::FullyEncoded).toStdString().c_str());
        return false;
    }
    LOG_F(INFO, "ChunkOperator: exported %d chunks to %s", static_cast<int>(region.chunkCount()),
          url.toString(QUrl::FullyEncoded).toStdString().c_str());
    return true;
}

bool ChunkOperator::importRegion(const ExportedRegion& region, AsyncLevelLoader& loader, bool invalidate_tiles) {
    std::vector<bl::chunk_pos> edited;
    edited.reserve(region.chunks().size());
    bool success = true;
    for (const auto& chunk : region.chunks()) {
        success = loader.putRawChunk(chunk) && success;
        edited.push_back(chunk.pos());
    }
    if (invalidate_tiles) loader.invalidateRegionTiles(edited);
    LOG_F(INFO, "ChunkOperator: imported %d chunks", static_cast<int>(region.chunkCount()));
    return success;
}

bool ChunkOperator::deleteRegion(const QRegion& chunkRegion, AsyncLevelLoader& loader, int dim, bool invalidate_tiles) {
    bool success = true;
    forEachChunkInRegion(chunkRegion, dim, [&](const bl::chunk_pos& cp) { success = loader.deleteChunk(cp) && success; });
    if (invalidate_tiles) loader.invalidateRegionTiles(chunkRegion, dim);
    return success;
}

bool ChunkOperator::createVoid(const QRegion& chunkRegion, AsyncLevelLoader& loader, int dim, bool invalidate_tiles) {
    LOG_F(INFO, "Create void!");
    bool success = true;
    forEachChunkInRegion(chunkRegion, dim, [&](const bl::chunk_pos& cp) { success = loader.createVoid(cp) && success; });
    if (invalidate_tiles) loader.invalidateRegionTiles(chunkRegion, dim);
    return success;
}

bool ChunkOperator::setRegionBiome(const QRegion& chunkRegion, AsyncLevelLoader& loader, bl::biome biome, int dim, bool invalidate_tiles) {
    bool success = true;
    forEachChunkInRegion(chunkRegion, dim, [&](const bl::chunk_pos& cp) { success = loader.setRawChunkBiome(cp, biome) && success; });
    if (invalidate_tiles) loader.invalidateRegionTiles(chunkRegion, dim);
    return success;
}
