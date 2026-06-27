#ifndef CHUNK_TASK_H
#define CHUNK_TASK_H
#include <QImage>
#include <QObject>
#include <QRunnable>
#include <bitset>
#include <deque>
#include <mutex>
#include <unordered_set>

#include "bedrock_key.h"
#include "chunk.h"
#include "config.h"
#include "data_3d.h"
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
    std::string block_name{"?"};
    std::string solid_block_name{"?"};
    bl::biome biome{bl::none};
    int16_t height{-128};
    int16_t solid_height{-128};
    uint32_t water_surface_color{0};  // QRgb packed, 0 = no water overlay
    uint8_t water_depth{0};           // 0 = no water
};

struct ChunkRegion {
    ~ChunkRegion();
    struct ActorCount {
        bl::vec3 pos{0, 0, 0};
        int count{0};
    };

    std::array<std::array<BlockTipsInfo, cfg::RW << 4>, cfg::RW << 4> tips_info_{};
    std::bitset<cfg::RW * cfg::RW> chunk_bit_map_;
    QImage terrain_bake_image_;
    QImage biome_bake_image_;
    QImage height_bake_image_;
    bool valid{false};
    std::unordered_map<QImage *, std::vector<bl::vec3>> actors_;             // for render mode 0
    std::map<bl::chunk_pos, std::map<QImage *, ActorCount>> actors_counts_;  // for render mode 1
    std::vector<bl::hardcoded_spawn_area> HSAs_;
};

struct RegionTimer {
    std::deque<int64_t> values;

    [[nodiscard]] int64_t mean() const;

    void push(int64_t value);
};

template <typename T>
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

    std::mutex mu_;
    std::unordered_set<T> buffer_;
};

class LoadRegionTask : public QObject, public QRunnable {
    Q_OBJECT

   public:
    LoadRegionTask(AsyncLevelLoader *loader, const bl::chunk_pos &pos, const MapFilter *filter)
        : QRunnable(), loader_(loader), pos_(pos), filter_(filter) {}
    void run() override;

   public:
   signals:

    void finish(int x, int z, int dim, ChunkRegion *region, long long load_time, long long render_time, bl::chunk **chunks);

   private:
    AsyncLevelLoader *loader_;
    region_pos pos_;
    const MapFilter *filter_;
};

class LoadThumbnailTask : public QObject, public QRunnable {
    Q_OBJECT

   public:
    LoadThumbnailTask(AsyncLevelLoader *loader, const bl::chunk_pos &pos) : QRunnable(), loader_(loader), pos_(pos) {}
    void run() override;

   signals:

    void finish(int x, int z, int dim, QImage *thumbnail);

   private:
    AsyncLevelLoader *loader_;
    region_pos pos_;
};

#endif