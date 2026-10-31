#ifndef BEDROCKMAP_LEVELDBXOR_H
#define BEDROCKMAP_LEVELDBXOR_H

class QString;
namespace platform {
    class WorldFileSource;
}

namespace leveldb_xor {

    /// Detect the NetEase XOR marker in the world's LevelDB files.
    [[nodiscard]] bool isEncrypted(platform::WorldFileSource& files, const QString& worldRoot);
    [[nodiscard]] bool isEncrypted(const QString& worldRoot);

}  // namespace leveldb_xor

#endif  // BEDROCKMAP_LEVELDBXOR_H
