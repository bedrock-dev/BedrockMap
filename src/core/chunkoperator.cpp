#include "chunkoperator.h"

#include <qfile.h>
#include <qlogging.h>

#include <QFile>
#include <functional>

#include "asynclevelloader.h"
#include "chunkio.h"
#include "loguru/loguru.hpp"

namespace {

    void forEachChunkInRegion(const QRegion &chunkRegion, const std::function<void(const bl::chunk_pos &)> &fn) {
        for (const auto &r : chunkRegion) {
            int x1 = r.x();
            int z1 = r.y();
            int x2 = r.x() + r.width() - 1;
            int z2 = r.y() + r.height() - 1;
            for (int x = x1; x <= x2; x++) {
                for (int z = z1; z <= z2; z++) {
                    fn(bl::chunk_pos(x, z, 0));
                }
            }
        }
    }

}  // namespace

void ChunkOperator::exportRegion(const QRegion &chunkRegion, const QString &filePath, AsyncLevelLoader &loader) {
    ExportedRegion region;
    forEachChunkInRegion(chunkRegion, [&](const bl::chunk_pos &cp) {
        auto *ref = loader.getChunk(cp);
        if (!ref) return;
        bl::raw_chunk raw(cp);
        if (!raw.read(loader.level())) return;
        region.addChunk(raw);
        delete ref;
    });

    auto data = region.serialize();
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_F(WARNING, "ChunkExporter: cannot write to %s", filePath.toStdString().c_str());
        return;
    }
    file.write(data.data(), data.size());
    file.close();
    LOG_F(INFO, "ChunkOperator: exported %d chunks to %s", static_cast<int>(region.chunkCount()), filePath.toStdString().c_str());
}

void ChunkOperator::importRegion(const ExportedRegion &region, AsyncLevelLoader &loader) {
    for (const auto &chunk : region.chunks()) {
        loader.putRawChunk(chunk);
    }
    LOG_F(INFO, "ChunkOperator: imported %d chunks", static_cast<int>(region.chunkCount()));
}

void ChunkOperator::deleteRegion(const QRegion &chunkRegion, AsyncLevelLoader &loader) {
    forEachChunkInRegion(chunkRegion, [&](const bl::chunk_pos &cp) { loader.deleteChunk(cp); });
}

void ChunkOperator::createVoid(const QRegion &chunkRegion, AsyncLevelLoader &loader) {
    LOG_F(INFO, "Create void!");
    forEachChunkInRegion(chunkRegion, [&](const bl::chunk_pos &cp) { loader.createVoid(cp); });
}

void ChunkOperator::setRegionBiome(const QRegion &chunkRegion, AsyncLevelLoader &loader, bl::biome biome) {
    forEachChunkInRegion(chunkRegion, [&](const bl::chunk_pos &cp) { loader.setRawChunkBiome(cp, biome); });
}
