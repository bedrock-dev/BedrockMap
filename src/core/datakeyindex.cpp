#include "datakeyindex.h"

#include <leveldb/db.h>
#include <leveldb/iterator.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include "actor.h"
#include "bedrock_key.h"
#include "bedrock_level.h"
#include "global.h"

namespace {

    [[nodiscard]] std::string printableKey(std::string_view key) {
        bool printable = true;
        for (const unsigned char byte : key) {
            if (byte < 0x20 || byte == 0x7f) {
                printable = false;
                break;
            }
        }
        if (printable) return std::string(key);

        static constexpr char HEX[] = "0123456789abcdef";
        std::string result = "0x";
        result.reserve(2 + key.size() * 2);
        for (const unsigned char byte : key) {
            result.push_back(HEX[byte >> 4]);
            result.push_back(HEX[byte & 0x0f]);
        }
        return result;
    }

    [[nodiscard]] std::string hexKey(std::string_view key) {
        static constexpr char HEX[] = "0123456789abcdef";
        std::string result = "0x";
        result.reserve(2 + key.size() * 2);
        for (const unsigned char byte : key) {
            result.push_back(HEX[byte >> 4]);
            result.push_back(HEX[byte & 0x0f]);
        }
        return result;
    }

    [[nodiscard]] std::string actorUidLabel(std::string_view key) {
        const auto actor = bl::actor_key::parse(key);
        if (!actor.valid()) return hexKey(key);
        std::ostringstream stream;
        stream << "0x" << std::hex << static_cast<std::uint64_t>(actor.actor_uid);
        return stream.str();
    }

    [[nodiscard]] bool hasPrefix(std::string_view value, std::string_view prefix) noexcept {
        return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
    }

    [[nodiscard]] std::string identifierHashLabel(std::uint64_t hash) {
        std::ostringstream stream;
        stream << "[0x" << std::hex << std::setw(16) << std::setfill('0') << hash << "]";
        return stream.str();
    }

}  // namespace

bool DataKeyIndex::chunkGroupHasMainKey(std::int32_t dimension, std::int32_t x, std::int32_t z) const {
    return std::binary_search(chunk_groups_with_main_keys_.begin(), chunk_groups_with_main_keys_.end(), bl::chunk_pos{x, z, dimension});
}

bool DataKeyIndex::load(bl::bedrock_level& level, const std::atomic_bool& stop, ProgressCallback progress) {
    clear();
    auto* db = level.db();
    if (!db) return false;

    // A digp value is an actor digest list: every 8-byte item is the suffix
    // used by the corresponding actorprefix key. Read these first so actor
    // records can be separated from unrelated global keys in the second pass.
    std::unordered_map<std::uint64_t, bl::chunk_pos> actor_locations;
    auto* digest_iterator = db->NewIterator(level.bulk_read_options());
    for (digest_iterator->Seek("digp"); digest_iterator->Valid(); digest_iterator->Next()) {
        const std::string key = digest_iterator->key().ToString();
        if (key.rfind("digp", 0) != 0) break;
        if (stop.load(std::memory_order_acquire)) {
            delete digest_iterator;
            return false;
        }
        const auto digest_key = bl::actor_digest_key::parse(key);
        if (!digest_key.valid()) continue;
        bl::actor_digest_list digest;
        const std::string value = digest_iterator->value().ToString();
        if (!digest.load(value)) continue;
        for (const auto& uid : digest.actor_digests_) {
            if (uid.size() != sizeof(std::uint64_t)) continue;
            std::uint64_t actor_uid = 0;
            std::memcpy(&actor_uid, uid.data(), sizeof(actor_uid));
            actor_locations[actor_uid] = digest_key.cp;
        }
    }
    const auto digest_status = digest_iterator->status();
    delete digest_iterator;
    if (!digest_status.ok()) return false;

    std::uint64_t scanned_keys = 0;
    auto* iterator = db->NewIterator(level.bulk_read_options());
    for (iterator->SeekToFirst(); iterator->Valid(); iterator->Next()) {
        if (stop.load(std::memory_order_acquire)) {
            delete iterator;
            clear();
            return false;
        }

        ++scanned_keys;
        const std::string key = iterator->key().ToString();
        const std::string_view key_view(key);

        DataKeyEntry entry;
        entry.key_offset = key_storage_.size();
        entry.key_size = static_cast<std::uint32_t>(key.size());
        key_storage_.append(key);
        entry.value_empty = iterator->value().empty();

        const auto chunk_key = bl::chunk_key::parse(key_view);
        if (chunk_key.valid()) {
            entry.category = DataKeyCategory::Chunks;
            entry.has_dimension = true;
            entry.dimension = chunk_key.cp.dim;
            entry.group_x = chunk_key.cp.x;
            entry.group_z = chunk_key.cp.z;
            entry.nbt_value = chunk_key.type == bl::chunk_key::BlockEntity || chunk_key.type == bl::chunk_key::Entity ||
                              chunk_key.type == bl::chunk_key::PendingTicks;
            if (chunk_key.type == bl::chunk_key::JigsawStructureBlueprint) {
                entry.raw_value = true;
            }
            if (chunk_key.type == bl::chunk_key::LegacyTerrain || chunk_key.type == bl::chunk_key::VersionOld ||
                chunk_key.type == bl::chunk_key::VersionNew) {
                chunk_groups_with_main_keys_.push_back(bl::chunk_pos{entry.group_x, entry.group_z, entry.dimension});
            }
        } else {
            const auto village_key = bl::village_key::parse(key);
            if (village_key.valid()) {
                entry.category = DataKeyCategory::Villages;
                entry.has_dimension = true;
                entry.dimension = village_key.dim;
            } else if (const auto actor_key = bl::actor_key::parse(key_view);
                       actor_key.valid() &&
                       actor_locations.find(static_cast<std::uint64_t>(actor_key.actor_uid)) != actor_locations.end()) {
                // Individual actor records are children of the digp entry for
                // the chunk whose digest contains their storage key.
                const auto actor_location = actor_locations.find(static_cast<std::uint64_t>(actor_key.actor_uid));
                entry.category = DataKeyCategory::Actors;
                entry.has_dimension = true;
                entry.dimension = actor_location->second.dim;
                entry.group_x = actor_location->second.x;
                entry.group_z = actor_location->second.z;
                entry.nbt_value = true;
            } else if (hasPrefix(key_view, "actorprefix")) {
                // Keep malformed/orphaned actor records visible even when no
                // digp record points at them.
                entry.category = DataKeyCategory::Actors;
                entry.nbt_value = true;
            } else if (key_view.rfind("digp", 0) == 0) {
                entry.category = DataKeyCategory::Digp;
                entry.nbt_value = false;
                const auto digest_key = bl::actor_digest_key::parse(key);
                if (digest_key.valid()) {
                    entry.has_dimension = true;
                    entry.dimension = digest_key.cp.dim;
                    entry.group_x = digest_key.cp.x;
                    entry.group_z = digest_key.cp.z;
                }
            } else if (hasPrefix(key_view, "structuretemplate_")) {
                entry.category = DataKeyCategory::Structures;
            } else if (hasPrefix(key_view, "RealmsStoriesData_")) {
                entry.category = DataKeyCategory::RealmsStoriesData;
            } else if (hasPrefix(key_view, "tickingarea_")) {
                entry.category = DataKeyCategory::TickingAreas;
            } else if (key == "~local_player" || key_view.find("player") != std::string_view::npos) {
                entry.category = DataKeyCategory::Players;
            } else if (hasPrefix(key_view, "map")) {
                entry.category = DataKeyCategory::MapItems;
            } else if (bl::global_key::is_other_key(key_view)) {
                entry.category = DataKeyCategory::Others;
            }
        }

        entries_.push_back(std::move(entry));
        if (progress && (scanned_keys % 4096 == 0)) progress(scanned_keys, entries_.size());
    }

    const auto status = iterator->status();
    delete iterator;
    if (!status.ok()) {
        clear();
        return false;
    }
    const auto key_view_for = [this](const DataKeyEntry& entry) {
        return std::string_view(key_storage_.data() + entry.key_offset, entry.key_size);
    };
    const auto tree_category = [](DataKeyCategory category) {
        // digp is displayed inside the chunks tree, so keep both record types
        // adjacent and ordered by the same chunk coordinates.
        return category == DataKeyCategory::Digp ? DataKeyCategory::Chunks : category;
    };
    std::sort(entries_.begin(), entries_.end(), [&key_view_for, &tree_category](const DataKeyEntry& lhs, const DataKeyEntry& rhs) {
        if (tree_category(lhs.category) != tree_category(rhs.category)) return tree_category(lhs.category) < tree_category(rhs.category);
        if (lhs.has_dimension != rhs.has_dimension) return lhs.has_dimension < rhs.has_dimension;
        if (lhs.dimension != rhs.dimension) return lhs.dimension < rhs.dimension;
        if (tree_category(lhs.category) == DataKeyCategory::Chunks) {
            if (lhs.group_x != rhs.group_x) return lhs.group_x < rhs.group_x;
            if (lhs.group_z != rhs.group_z) return lhs.group_z < rhs.group_z;
        }
        return key_view_for(lhs) < key_view_for(rhs);
    });

    std::unordered_map<bl::chunk_pos, std::size_t> digp_indices;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].category == DataKeyCategory::Digp && entries_[i].has_dimension) {
            digp_indices.emplace(bl::chunk_pos{entries_[i].group_x, entries_[i].group_z, entries_[i].dimension}, i);
        }
    }
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].category != DataKeyCategory::Actors || !entries_[i].has_dimension) continue;
        const auto digp_it = digp_indices.find(bl::chunk_pos{entries_[i].group_x, entries_[i].group_z, entries_[i].dimension});
        if (digp_it != digp_indices.end()) {
            entries_[i].parent_index = digp_it->second;
        }
    }
    std::sort(chunk_groups_with_main_keys_.begin(), chunk_groups_with_main_keys_.end());
    chunk_groups_with_main_keys_.erase(std::unique(chunk_groups_with_main_keys_.begin(), chunk_groups_with_main_keys_.end()),
                                       chunk_groups_with_main_keys_.end());
    category_counts_.fill(0);
    for (const auto& entry : entries_) {
        if (!entry.hasParent()) ++category_counts_[static_cast<std::size_t>(entry.category)];
    }
    for (const auto& entry : entries_) {
        if (!entry.has_dimension) continue;
        auto& dimensions = category_dimensions_[static_cast<std::size_t>(entry.category)];
        if (std::find(dimensions.begin(), dimensions.end(), entry.dimension) == dimensions.end()) dimensions.push_back(entry.dimension);
        if (entry.category == DataKeyCategory::Digp) {
            auto& chunk_dimensions = category_dimensions_[static_cast<std::size_t>(DataKeyCategory::Chunks)];
            if (std::find(chunk_dimensions.begin(), chunk_dimensions.end(), entry.dimension) == chunk_dimensions.end()) {
                chunk_dimensions.push_back(entry.dimension);
            }
        }
    }
    for (auto& dimensions : category_dimensions_) {
        std::sort(dimensions.begin(), dimensions.end());
    }
    if (progress) progress(scanned_keys, entries_.size());
    return true;
}

std::string DataKeyIndex::labelForEntry(std::size_t entry_index) const {
    if (entry_index >= entries_.size()) return {};
    const auto& entry = entries_[entry_index];
    const auto key = keyForEntry(entry_index);
    if (entry.category == DataKeyCategory::Chunks) {
        const auto chunk_key = bl::chunk_key::parse(key);
        if (chunk_key.valid()) {
            std::string label = bl::chunk_key::chunk_key_to_str(chunk_key.type);
            if (chunk_key.type == bl::chunk_key::SubChunkTerrain) {
                label += "[" + std::to_string(chunk_key.y_index) + "]";
            } else if (chunk_key.type == bl::chunk_key::JigsawStructureBlueprint) {
                label += identifierHashLabel(chunk_key.identifier_hash);
            }
            return label;
        }
    } else if (entry.category == DataKeyCategory::Villages) {
        const auto village_key = bl::village_key::parse(key);
        if (village_key.valid()) return bl::village_key::village_key_type_to_str(village_key.type);
    } else if (entry.category == DataKeyCategory::Actors) {
        return actorUidLabel(key);
    } else if (entry.category == DataKeyCategory::Digp) {
        return "actorDigest";
    }
    return printableKey(key);
}
