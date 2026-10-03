#include "datakeyindex.h"

#include <leveldb/db.h>
#include <leveldb/iterator.h>

#include <algorithm>
#include <string_view>
#include <unordered_set>

#include "actor.h"
#include "bedrock_key.h"
#include "bedrock_level.h"

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

    [[nodiscard]] bool hasPrefix(std::string_view value, std::string_view prefix) noexcept {
        return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
    }

}  // namespace

bool DataKeyIndex::load(bl::bedrock_level& level, const std::atomic_bool& stop, ProgressCallback progress) {
    clear();
    auto* db = level.db();
    if (!db) return false;

    // A digp value is an actor digest list: every 8-byte item is the suffix
    // used by the corresponding actorprefix key. Read these first so actor
    // records can be separated from unrelated global keys in the second pass.
    std::unordered_set<std::string> actor_keys;
    auto* digest_iterator = db->NewIterator(level.bulk_read_options());
    for (digest_iterator->Seek("digp"); digest_iterator->Valid(); digest_iterator->Next()) {
        const std::string key = digest_iterator->key().ToString();
        if (key.rfind("digp", 0) != 0) break;
        if (stop.load(std::memory_order_acquire)) {
            delete digest_iterator;
            return false;
        }
        bl::actor_digest_list digest;
        const std::string value = digest_iterator->value().ToString();
        if (!digest.load(value)) continue;
        for (const auto& uid : digest.actor_digests_) actor_keys.insert("actorprefix" + uid);
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
        entry.key = key;
        entry.label = printableKey(key_view);

        const auto chunk_key = bl::chunk_key::parse(key_view);
        if (chunk_key.valid()) {
            entry.category = DataKeyCategory::Chunks;
            entry.has_dimension = true;
            entry.dimension = chunk_key.cp.dim;
            entry.group = std::to_string(chunk_key.cp.x) + "," + std::to_string(chunk_key.cp.z);
            entry.label = bl::chunk_key::chunk_key_to_str(chunk_key.type);
            if (chunk_key.type == bl::chunk_key::SubChunkTerrain) {
                entry.label += "[" + std::to_string(chunk_key.y_index) + "]";
            }
        } else {
            const auto village_key = bl::village_key::parse(key);
            if (village_key.valid()) {
                entry.category = DataKeyCategory::Villages;
                entry.has_dimension = true;
                entry.dimension = village_key.dim;
                entry.group = village_key.uuid;
                entry.label = bl::village_key::village_key_type_to_str(village_key.type);
            } else if (actor_keys.contains(key)) {
                entry.category = DataKeyCategory::Actors;
                const auto actor = bl::actor_key::parse(key_view);
                entry.label = actor.valid() ? "actor " + actor.to_string() : printableKey(key_view);
            } else if (key_view.rfind("digp", 0) == 0) {
                entry.category = DataKeyCategory::Digp;
                const auto digest_key = bl::actor_digest_key::parse(key);
                entry.label = digest_key.valid() ? "digp " + digest_key.to_string() : printableKey(key_view);
            } else if (key == "~local_player" || key_view.find("player") != std::string_view::npos) {
                entry.category = DataKeyCategory::Players;
            } else if (hasPrefix(key_view, "map")) {
                entry.category = DataKeyCategory::MapItems;
            }
        }

        ++category_counts_[static_cast<std::size_t>(entry.category)];
        entries_.push_back(std::move(entry));
        if (progress && (scanned_keys % 4096 == 0)) progress(scanned_keys, entries_.size());
    }

    const auto status = iterator->status();
    delete iterator;
    if (!status.ok()) {
        clear();
        return false;
    }
    std::sort(entries_.begin(), entries_.end(), [](const DataKeyEntry& lhs, const DataKeyEntry& rhs) {
        if (lhs.category != rhs.category) return lhs.category < rhs.category;
        if (lhs.has_dimension != rhs.has_dimension) return lhs.has_dimension < rhs.has_dimension;
        if (lhs.dimension != rhs.dimension) return lhs.dimension < rhs.dimension;
        if (lhs.group != rhs.group) return lhs.group < rhs.group;
        return lhs.key < rhs.key;
    });
    for (const auto& entry : entries_) {
        if (!entry.has_dimension) continue;
        auto& dimensions = category_dimensions_[static_cast<std::size_t>(entry.category)];
        if (dimensions.empty() || dimensions.back() != entry.dimension) dimensions.push_back(entry.dimension);
    }
    if (progress) progress(scanned_keys, entries_.size());
    return true;
}
