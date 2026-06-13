#ifndef CHUNKIO_H
#define CHUNKIO_H

#include <QString>
#include <string>
#include <vector>

#include "bedrock_key.h"
#include "chunk.h"

class QImage;

namespace bl {
    class bedrock_level;
}  // namespace bl

/// Holds exported chunk data for serialization.
class ExportedRegion {
   public:
    ExportedRegion() = default;

    void addChunk(const bl::raw_chunk &chunk);
    void removeChunk(const bl::chunk_pos &pos);
    void clear();
    bool isEmpty() const;

    const std::vector<bl::raw_chunk> &chunks() const { return chunks_; }
    std::vector<bl::raw_chunk> &chunks() { return chunks_; }

    /// Count of unique (dim, x, z) entries.
    size_t chunkCount() const { return chunks_.size(); }

    // --- Serialization ---

    /// Serialize to binary + zstd compress.
    std::vector<char> serialize();

    /// Decompress + deserialize. Returns empty region on failure.
    static ExportedRegion deserialize(const char *data, size_t size);

    /// Embed raw binary payload after the PNG IEND chunk. Returns complete PNG bytes.
    static std::vector<char> embedInPng(const QImage &image, const std::vector<char> &payload);

    /// Extract raw payload from a PNG-with-embedded-data file.
    /// Returns empty vector if no payload is found.
    static std::vector<char> extractFromPng(const char *data, size_t size);

   private:
    std::vector<bl::raw_chunk> chunks_;
    int32_t version_{0};
};

#endif  // CHUNKIO_H
