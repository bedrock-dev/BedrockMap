#ifndef BEDROCKMAP_LEVELDBXOR_H
#define BEDROCKMAP_LEVELDBXOR_H

#include <string>

namespace leveldb_xor {

    /// Detect the NetEase XOR marker in the world's LevelDB files.
    [[nodiscard]] bool isEncrypted(const std::string& world_root);

    /// Resolve the configured GUI mode to the key passed to bedrock-level.
    /// An empty result means that XOR decoding is disabled.
    [[nodiscard]] std::string keyForLevel(const std::string& world_root);

}  // namespace leveldb_xor

#endif  // BEDROCKMAP_LEVELDBXOR_H
