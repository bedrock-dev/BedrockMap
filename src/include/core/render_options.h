#ifndef BEDROCKMAP_RENDER_OPTIONS_H
#define BEDROCKMAP_RENDER_OPTIONS_H

#include <algorithm>
#include <array>
#include <string>
#include <unordered_set>

#include "loguru/loguru.hpp"

namespace bl {
    class chunk;
}

struct ChunkRegion;

/// View state (dimension/layer/overlays) for the 2D map. A plain value type so
/// it can be shared by the map widget and any render code without pulling in UI
/// headers; it is intentionally not nested inside a widget class.
struct RenderOption {
    enum LayerType { Terrain = 0, Biome = 1 };
    enum OtherType { Grid = 0, Coords = 1, SlimeChunk = 2, Actors = 3, Village = 4, HSA = 5, OtherLen = 6 };

    static constexpr int OverWorld = 0;
    static constexpr int Nether = 1;
    static constexpr int TheEnd = 2;

    RenderOption() { reset(); }

    int dim{OverWorld};
    LayerType layer{Terrain};
    std::array<bool, OtherType::OtherLen> others{};
    void reset() {
        dim = OverWorld;
        layer = Terrain;
        std::fill(others.begin(), others.end(), false);
        setOther(Grid, true);
    }

    void setDim(int nDim) { dim = nDim; }
    void setLayer(LayerType nLayer) { layer = nLayer; }

    bool toggleOther(OtherType type) {
        if (type < 0 || type >= OtherType::OtherLen) return true;
        others[type] = !others[type];
        return others[type];
    }

    void setOther(OtherType type, bool value) {
        if (type < 0 || type >= OtherType::OtherLen) return;
        others[type] = value;
    }
    bool getOther(OtherType type) {
        if (type < 0 || type >= OtherType::OtherLen) return false;
        return others[type];
    }
};

/// Filters which biomes/blocks/actors get rendered for a region. Read by the
/// region render workers (chunk_task/maptile), so it lives in the shared layer.
struct MapFilter {
    std::unordered_set<int> biomes_list_{};
    std::unordered_set<std::string> blocks_list_{"minecraft:air", "minecraft:unknown"};
    std::unordered_set<std::string> actors_list_{"item"};
    int layer{64};
    bool enable_layer_{false};
    bool biome_black_mode_{true};
    bool block_black_mode_{true};
    bool actor_black_mode_{true};

    // block filter is the default: only excludes air + unknown in blacklist mode
    [[nodiscard]] bool isBlockFilterDefault() const {
        return block_black_mode_ && !enable_layer_ && blocks_list_.size() == 2 && blocks_list_.count("minecraft:air") &&
               blocks_list_.count("minecraft:unknown");
    }

    void renderImages(bl::chunk *ch, int rw, int rh, ChunkRegion *region) const;

    void print() {
        LOG_F(INFO, "BlockList (%d)", block_black_mode_);
        for (const auto &block : blocks_list_) {
            LOG_F(INFO, " - %s", block.c_str());
        }
        LOG_F(INFO, "ActorList (%d)", actor_black_mode_);
        for (const auto &actor : actors_list_) {
            LOG_F(INFO, " - %s", actor.c_str());
        }
    }
};

#endif  // BEDROCKMAP_RENDER_OPTIONS_H
