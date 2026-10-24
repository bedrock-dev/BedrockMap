#ifndef BEDROCKMAP_WORLDMETADATA_H
#define BEDROCKMAP_WORLDMETADATA_H

#include <cstdint>
#include <string>

enum class WorldSourceKind {
    MinecraftDefault,
    AppInternal,
    UserFolder,
    MojangExternal,
};

// UI-independent metadata describing a Bedrock world on disk.
struct WorldMetadata {
    std::string path;
    std::string levelName;
    std::string version;
    std::int64_t lastModified{0};
    std::int64_t sizeBytes{0};
    bool isValid{false};
    bool modern{false};
    bool preview{false};
    bool netease{false};
    bool xorEncrypted{false};
    WorldSourceKind sourceKind{WorldSourceKind::MinecraftDefault};
    std::string sourceLabel;
};

// Compatibility name retained by the world repository API.
using LevelPathInfo = WorldMetadata;

#endif  // BEDROCKMAP_WORLDMETADATA_H
