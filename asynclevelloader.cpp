#include "asynclevelloader.h"

#include <QObject>
#include <chrono>
#include <thread>

#include "qdebug.h"

AsyncLevelLoader::AsyncLevelLoader() {
    this->chunk_cache_ = std::vector<LRUCache<bl::chunk_pos, bl::chunk> *>(
        3, new LRUCache<bl::chunk_pos, bl::chunk>(16384));

    this->pool_.setMaxThreadCount(1);
}

bl::chunk *AsyncLevelLoader::get(const bl::chunk_pos &p) {
    bl::chunk *ch{nullptr};

    if (this->chunk_cache_[p.dim]->safe_get(p, ch)) {
        return ch;
    }

    if (this->chunk_in_queue(p)) {
        return nullptr;
    }

    auto *task = new LoadChunkTask(&this->level_, p);
    connect(task, SIGNAL(finish(int, int, int, bl::chunk *)), this,
            SLOT(handle_task_finished_task(int, int, int, bl::chunk *)));
    this->insert_chunk_to_queue(p);
    this->pool_.start(task);
    return nullptr;
}

bool AsyncLevelLoader::init(const std::string &path) {
    this->level_.set_cache(false);
    return this->level_.open(path);
}

void AsyncLevelLoader::handle_task_finished_task(int x, int z, int dim,
                                                 bl::chunk *chunk) {
    this->chunk_cache_[dim]->safe_put(bl::chunk_pos{x, z, dim}, chunk);
    this->remove_chunk_from_queue(bl::chunk_pos{x, z, dim});
}

AsyncLevelLoader::~AsyncLevelLoader() { this->pool_.waitForDone(); }

void LoadChunkTask::run() {
    auto *chunk = this->level_->get_chunk(this->pos_);
    //    bl::chunk *chunk = nullptr;
    //    qDebug() << "Load chunk" << this->pos_.to_string().c_str();

    emit finish(this->pos_.x, this->pos_.z, this->pos_.dim, chunk);
}

bool AsyncLevelLoader::chunk_in_queue(const bl::chunk_pos &p) {
    bool exist{false};
    {
        std::lock_guard<std::mutex> l(this->m_);
        exist = this->processing_.count(p) > 0;
    }
    return exist;
}

void AsyncLevelLoader::insert_chunk_to_queue(const bl::chunk_pos &pos) {
    std::lock_guard<std::mutex> l(this->m_);
    this->processing_.insert(pos);
}

void AsyncLevelLoader::remove_chunk_from_queue(const bl::chunk_pos &pos) {
    std::lock_guard<std::mutex> l(this->m_);
    this->processing_.erase(pos);
}
