#ifndef ASYNCLEVELLOADER_H
#define ASYNCLEVELLOADER_H
#include <QRunnable>
#include <QThreadPool>
#include <mutex>
#include <unordered_set>

#include "bedrock_level.h"
#include "lrucache.h"

class AsyncLevelLoader;

class LoadChunkTask : public QObject, public QRunnable {
    Q_OBJECT

   public:
    explicit LoadChunkTask(bl::bedrock_level* level, const bl::chunk_pos& pos)
        : QRunnable(), level_(level), pos_(pos) {}
    void run() override;
   signals:
    void finish(int x, int z, int dim, bl::chunk* chunk);

   private:
    bl::bedrock_level* level_;
    bl::chunk_pos pos_;
};

class AsyncLevelLoader : public QObject {
    Q_OBJECT

   public:
    AsyncLevelLoader();

    bl::chunk* get(const bl::chunk_pos& p);
    bool init(const std::string& path);

   public slots:
    void handle_task_finished_task(int x, int z, int dim, bl::chunk* chunk);

   public:
    ~AsyncLevelLoader();

   private:
    // processing
    bool chunk_in_queue(const bl::chunk_pos& p);

    void insert_chunk_to_queue(const bl::chunk_pos& pos);

    void remove_chunk_from_queue(const bl::chunk_pos& pos);

   private:
    bl::bedrock_level level_;
    std::mutex m_;

    std::unordered_set<bl::chunk_pos> processing_;
    std::array<LRUCache<bl::chunk_pos, bl::chunk>, 3> chunk_cache_;
    QThreadPool pool_;
};

#endif // ASYNCLEVELLOADER_H
