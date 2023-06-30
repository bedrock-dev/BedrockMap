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

#include "bedrock_level.h"
#include "config.h"

class AsyncLevelLoader;

struct chunk_region {
    std::array<std::array<bl::chunk *, cfg::RW>, cfg::RW> chunks_;

    ~chunk_region();

    bool valid() const;
};

class LoadChunkTask : public QObject, public QRunnable {
Q_OBJECT

public:
    explicit LoadChunkTask(bl::bedrock_level *level, const bl::chunk_pos &pos)
            : QRunnable(), level_(level), pos_(pos) {}

    void run() override;

signals:

    void finish(int x, int z, int dim, chunk_region *group); //NOLINT

private:
    bl::bedrock_level *level_;
    region_pos pos_;
};

class AsyncLevelLoader : public QObject {
Q_OBJECT

public:
    AsyncLevelLoader();

    chunk_region *getRegion(const region_pos &p, bool &empty);

    bool init(const std::string &path);

    void close();

public:
    QFuture<bl::chunk *> getChunkDirect(const bl::chunk_pos &p);

public slots:

    void handle_task_finished_task(int x, int z, int dim, chunk_region *chunk);

public:
    ~AsyncLevelLoader() override;

    std::vector<QString> debug_info();

private:
    // processing
    bool region_in_queue(const bl::chunk_pos &p);

    void insert_region_to_queue(const bl::chunk_pos &pos);

    void remove_region_from_queue(const bl::chunk_pos &pos);

private:
    std::atomic_bool loaded_{false};

    bl::bedrock_level level_;
    std::mutex m_;

    std::unordered_set<region_pos> processing_;

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
