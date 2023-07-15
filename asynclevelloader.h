#ifndef ASYNCLEVELLOADER_H
#define ASYNCLEVELLOADER_H

#include <QCache>
#include <QFuture>
#include <QRunnable>
#include <QThreadPool>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <array>
#include "bedrock_level.h"
#include "config.h"
#include <unordered_set>
#include <vector>
#include "palette.h"
#include "renderfilterdialog.h"

class AsyncLevelLoader;
namespace bl {

    inline uint qHash(const bl::chunk_pos &key, uint seed) {
        uint hash = 3241;
        hash = 3457689L * hash + key.x;
        hash = 8734625L * hash + key.z;
        hash = 2873465L * hash + key.dim;
        return hash;
    }
}  // namespace bl


struct BlockTipsInfo {
    std::string block_name{"unknown"};
    bl::biome biome{bl::none};
    int height{-128};
};

struct chunk_region {
    ~chunk_region();

    std::array<std::array<BlockTipsInfo, cfg::RW << 4>, cfg::RW << 4> tips_info_{};
    std::array<std::array<bool, cfg::RW>, cfg::RW> chunk_bit_map_;
    QImage *terrain_bake_image_{nullptr};
    QImage *biome_bake_image_{nullptr};
    bool valid{false};
    std::unordered_map<QImage *, std::vector<bl::vec3>> actors_;
    std::vector<bl::hardcoded_spawn_area> HSAs_;
};


template<typename T>
class TaskBuffer {
public:
    bool contains(const T &t) {
        bool exist{false};
        {
            std::lock_guard<std::mutex> lk(this->mu_);
            exist = this->buffer_.count(t) > 0;
        }
        return exist;
    }

    size_t size() {
        size_t sz = 0;
        {
            std::lock_guard<std::mutex> kl(this->mu_);
            sz = this->buffer_.size();
        }
        return sz;
    }

    void clear() {
        std::lock_guard<std::mutex> kl(this->mu_);
        this->buffer_.clear();
    }

    void add(const T &t) {
        std::lock_guard<std::mutex> kl(this->mu_);
        this->buffer_.insert(t);
    }

    void remove(const T &t) {
        std::lock_guard<std::mutex> kl(this->mu_);
        this->buffer_.erase(t);

    }

    bl::bedrock_level *level() { return this->level_; }

    std::mutex mu_;
    std::unordered_set<T> buffer_;
};

class LoadRegionTask : public QObject, public QRunnable {
Q_OBJECT

public:
    explicit LoadRegionTask(bl::bedrock_level *level, const bl::chunk_pos &pos, const MapFilter *filter)
            : QRunnable(), level_(level), pos_(pos), filter_(filter) {
    }

    void run() override;

signals:

    void finish(int x, int z, int dim, chunk_region *group); //NOLINT

private:
    bl::bedrock_level *level_;
    region_pos pos_;
    const MapFilter *filter_;
};


class AsyncLevelLoader : public QObject {
Q_OBJECT

public:
    AsyncLevelLoader();

    void clearAllCache();

    bool open(const std::string &path);

    void close();

    bl::bedrock_level &level() { return this->level_; }

    inline bool isOpen() const { return this->loaded_; }

    void setFilter(const MapFilter &f) { this->map_filter_ = f; }

public:

    /*region cache*/
    QImage *bakedBiomeImage(const region_pos &rp);

    QImage *bakedTerrainImage(const region_pos &rp);

    QImage *bakedHeightImage(const region_pos &rp);

    QImage *bakedSlimeChunkImage(const region_pos &rp);

    BlockTipsInfo getBlockTips(const bl::block_pos &p, int dim);

    std::unordered_map<QImage *, std::vector<bl::vec3>> getActorList(const region_pos &rp);

    std::vector<bl::hardcoded_spawn_area> getHSAs(const region_pos &rp);


    /*Modify*/
    bl::chunk *getChunkDirect(const bl::chunk_pos &p);

    //modify
    QFuture<bool> dropChunk(const bl::chunk_pos &min, const ::bl::chunk_pos &max);

    bool modifyLeveldat(bl::palette::compound_tag *nbt);

    bool
    modifyPlayerList(const std::unordered_map<std::string, bl::palette::compound_tag *> &player_list);

    bool modifyVillageList(
            const std::unordered_map<std::string, std::array<bl::palette::compound_tag *, 4>> &village_list);

    bool modifyChunkBlockEntities(const bl::chunk_pos &cp, const std::string &raw);

    bool modifyChunkPendingTicks(const bl::chunk_pos &cp, const std::string &raw);

    bool modifyChunkActors(bl::chunk *ch, const std::vector<bl::actor *> &actors);

public:
    ~AsyncLevelLoader() override;

    std::vector<QString> debugInfo();

private:

    chunk_region *tryGetRegion(const region_pos &p, bool &empty);

private:
    std::atomic_bool loaded_{false};
    bl::bedrock_level level_{};
    TaskBuffer<region_pos> processing_;
    std::vector<QCache<region_pos, chunk_region> *> region_cache_;
    std::vector<QCache<region_pos, char> *> invalid_cache_;
    //主要是缓存图像，计算不是重点
    QCache<region_pos, QImage> *slime_chunk_cache_;
    QThreadPool pool_;
    MapFilter map_filter_;
};


#endif // ASYNCLEVELLOADER_H
