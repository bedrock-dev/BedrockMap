#ifndef ASYNCLEVELLOADER_H
#define ASYNCLEVELLOADER_H

#include <qcache.h>
#include <qimage.h>

#include <QCache>
#include <QFuture>
#include <QRunnable>
#include <QThreadPool>
#include <atomic>
#include <vector>

#include "bedrock_key.h"
#include "bedrock_level.h"
#include "chunk_task.h"
#include "config.h"
#include "palette.h"

class AsyncLevelLoader;

struct GlobalNBTLoadResult {
    bl::village_data villageData;
    bl::general_kv_nbts playerData;
    bl::general_kv_nbts mapData;
    bl::general_kv_nbts otherData;
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

    void loadGlobalData(GlobalNBTLoadResult &result, std::atomic_bool &stop);

   public:
    /*region cache*/
    QImage *bakedBiomeImage(const region_pos &rp);

    QImage *bakedTerrainImage(const region_pos &rp);

    QImage *bakedHeightImage(const region_pos &rp);

    QImage *bakeThumbnailImage(const region_pos &rp);

    QImage *bakedSlimeChunkImage(const region_pos &rp);

    BlockTipsInfo getBlockTips(const bl::block_pos &p, int dim);

    std::unordered_map<QImage *, std::vector<bl::vec3>> getActorList(const region_pos &rp);

    std::map<bl::chunk_pos, std::map<QImage *, ChunkRegion::ActorCount>> getActorCountList(const region_pos &rp);

    std::vector<bl::hardcoded_spawn_area> getHSAs(const region_pos &rp);

    /*Modify*/
    bl::chunk *getChunkDirect(const bl::chunk_pos &p);

    // modify
    QFuture<bool> dropChunk(const bl::chunk_pos &min, const ::bl::chunk_pos &max);

    /**
     * 批量修改数据库
     * 对于 @modifies中的没一个key 和value
     * 如果value是空的，就往db中删除key
     * 如果value不是空的，就更新key
     * 注意不会更新内存中的bedrock_level对象
     * @param modifies
     * @return
     */
    bool modifyDBGlobal(const std::unordered_map<std::string, std::string> &modifies);

    bool modifyLeveldat(bl::palette::compound_tag *nbt);

    bool modifyChunkBlockEntities(const bl::chunk_pos &cp, const std::string &raw);

    bool modifyChunkPendingTicks(const bl::chunk_pos &cp, const std::string &raw);

    bool modifyChunkActors(const bl::chunk_pos &cp, bl::ChunkVersion v, const std::vector<bl::actor *> &actors);

   public:
    ~AsyncLevelLoader() override;

    std::vector<QString> debugInfo();

   private:
    ChunkRegion *tryGetRegion(const region_pos &p, bool &empty);

    QImage *tryGetThumbnail(const region_pos &p);

   private:
    std::atomic_bool loaded_{false};
    bl::bedrock_level level_{};
    // map region cache
    TaskBuffer<region_pos> processing_;
    std::vector<QCache<region_pos, ChunkRegion> *> region_cache_;
    std::vector<QCache<region_pos, char> *> invalid_cache_;
    // map region thumbnails cache
    TaskBuffer<region_pos> thumbbail_processing_;
    std::vector<QCache<region_pos, QImage> *> thumbnails_cache_;

    // 主要是缓存图像，计算不是重点
    QCache<region_pos, QImage> *slime_chunk_cache_;
    QThreadPool pool_;
    MapFilter map_filter_;
    RegionTimer region_load_timer_;
    RegionTimer region_render_timer_;
};

#endif  // ASYNCLEVELLOADER_H
