#ifndef CHUNKOPERATOR_H
#define CHUNKOPERATOR_H

#include <qregion.h>

#include <QRegion>
#include <QString>

#include "chunkio.h"

class AsyncLevelLoader;

/// Helper that operates on selected chunk regions (export, import, delete, etc.).
class ChunkOperator {
   public:
    ChunkOperator() = delete;

    /// Export all chunks inside the given QRegion to a binary file.
    static void exportRegion(const QRegion &chunkRegion, const QString &filePath, AsyncLevelLoader &loader);

    /// Import all chunks from an ExportedRegion into the level.
    static void importRegion(const ExportedRegion &region, AsyncLevelLoader &loader);

    static void deleteRegion(const QRegion &chunkRegion, AsyncLevelLoader &loader);

    static void createVoid(const QRegion &chunkRegion, AsyncLevelLoader &loader);
};

#endif  // CHUNKOPERATOR_H
