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

class AsyncLevelLoader;

class RenderFilter;

struct BlockTipsInfo {
    std::string block_name;
    bl::biome biome;
    int height;
};

struct chunk_region {
    ~chunk_region();

    std::array<std::array<BlockTipsInfo, cfg::RW << 4>, cfg::RW << 4> tips_info_{};
    QImage *terrain_bake_image_{nullptr};
    QImage *biome_bake_image_{nullptr};
    bool valid{false};
    std::unordered_map<std::string, std::vector<bl::vec3>> actors_;
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
    explicit LoadRegionTask(bl::bedrock_level *level, const bl::chunk_pos &pos, const RenderFilter *filter)
            : QRunnable(), level_(level), pos_(pos), filter_(filter) {
    }

    void run() override;

signals:

    void finish(int x, int z, int dim, chunk_region *group); //NOLINT

private:
    bl::bedrock_level *level_;
    region_pos pos_;
    const RenderFilter *filter_;
};


class AsyncLevelLoader : public QObject {
Q_OBJECT

public:
    AsyncLevelLoader();

    chunk_region *getRegion(const region_pos &p, bool &empty, const RenderFilter *filter);

    void clear_all_cache();

    bool open(const std::string &path);

    void close();

    bl::bedrock_level &level() { return this->level_; }

public:
    bl::chunk *getChunkDirect(const bl::chunk_pos &p);

    QFuture<bool> dropChunk(const bl::chunk_pos &min, const ::bl::chunk_pos &max);

    bool modifyLeveldat(bl::palette::compound_tag *nbt);

    bool
    modifyPlayerList(const std::unordered_map<std::string, bl::palette::compound_tag *> &player_list);

    bool modifyVillageList(
            const std::unordered_map<std::string, std::array<bl::palette::compound_tag *, 4>> &village_list);

    bool modifyChunkBlockEntities(const bl::chunk_pos &cp, const std::string &raw);

    bool modifyChunkPendingTicks(const bl::chunk_pos &cp, const std::string &raw);

    //TODO: 修改实体并持久化
    bool
    modifyChunkActors(const bl::chunk_pos &cp,
                      const std::unordered_map<std::string, bl::palette::compound_tag *> &actors);

public slots:

    void handle_task_finished_task(int x, int z, int dim, chunk_region *chunk);

public:
    ~AsyncLevelLoader() override;

    std::vector<QString> debug_info();

private:
    std::atomic_bool loaded_{false};

    bl::bedrock_level level_;
    std::mutex m_;

//    std::unordered_set<region_pos> processing_;
    TaskBuffer<region_pos> processing_;
    std::vector<QCache<region_pos, chunk_region> *> region_cache_;
    std::vector<QCache<region_pos, char> *> invalid_cache_;
    QThreadPool pool_;
};

inline std::vector<QString> AsyncLevelLoader::debug_info() {
    std::vector<QString> res;

    res.emplace_back("Chunk data cache:");

    for (int i = 0; i < 3; i++) {
        res.push_back(QString(" - [%1]: %2/%3")
                              .arg(QString::number(i), QString::number(this->region_cache_[i]->totalCost()),
                                   QString::number(this->region_cache_[i]->maxCost())));
    }

    res.emplace_back("Null chunk cache:");
    for (int i = 0; i < 3; i++) {
        res.push_back(QString(" - [%1]: %2/%3")
                              .arg(QString::number(i), QString::number(this->invalid_cache_[i]->totalCost()),
                                   QString::number(this->invalid_cache_[i]->maxCost())));
    }
    res.emplace_back("Background thread pool:");
    res.push_back(QString(" - Read threads: %1").arg(QString::number(4)));
    res.push_back(QString(" - Task queue size %1").arg(QString::number(this->processing_.size())));

    return res;
}

#endif // ASYNCLEVELLOADER_H
