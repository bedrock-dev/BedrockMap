#ifndef BEDROCKMAP_MAPFILTER_H
#define BEDROCKMAP_MAPFILTER_H

#include <string>
#include <unordered_set>

#include "loguru/loguru.hpp"

/// Filters which biomes/blocks/actors get loaded for a region.
struct MapFilter {
    std::unordered_set<int> biomes_list_{};
    std::unordered_set<std::string> blocks_list_{"minecraft:air", "minecraft:unknown"};
    std::unordered_set<std::string> actors_list_{"item"};
    int layer{64};
    bool enable_layer_{false};
    bool biome_black_mode_{true};
    bool block_black_mode_{true};
    bool actor_black_mode_{true};

    // Block filter is the default: only excludes air and unknown in blacklist mode.
    [[nodiscard]] bool isBlockFilterDefault() const {
        return block_black_mode_ && !enable_layer_ && blocks_list_.size() == 2 && blocks_list_.count("minecraft:air") &&
               blocks_list_.count("minecraft:unknown");
    }

    void print() {
        LOG_F(INFO, "BlockList (%d)", block_black_mode_);
        for (const auto& block : blocks_list_) {
            LOG_F(INFO, " - %s", block.c_str());
        }
        LOG_F(INFO, "ActorList (%d)", actor_black_mode_);
        for (const auto& actor : actors_list_) {
            LOG_F(INFO, " - %s", actor.c_str());
        }
    }
};

#endif  // BEDROCKMAP_MAPFILTER_H
