#include "asynclevelloader.h"

#include <QObject>
#include <chrono>
#include <thread>

#include "qdebug.h"
#include "world.h"

AsyncLevelLoader::AsyncLevelLoader() {
    this->pool_.setMaxThreadCount(1);
    for (int i = 0; i < 3; i++) {
        this->chunk_cache_.push_back(new QCache<bl::chunk_pos, bl ::chunk>(4096));
        this->invalid_cache_.push_back(new QCache<bl::chunk_pos, char>(16384));
    }
}

bl::chunk *AsyncLevelLoader::get(const bl::chunk_pos &p, bool &empty) {
    empty = false;
    if (!this->loadered_) return nullptr;

    // empty cunk(no data in level)
    auto *invald = this->invalid_cache_[p.dim]->operator[](p);
    if (invald) {
        // hit but null chunk
        empty = true;
        return nullptr;
    }

    // chunk cahce
    auto *chunk = this->chunk_cache_[p.dim]->operator[](p);
    if (chunk) {
        return chunk;
    }

    // not in cache but in queue
    if (this->chunk_in_queue(p)) {
        return nullptr;
    }

    auto *task = new LoadChunkTask(&this->level_, p);
    connect(task, SIGNAL(finish(int, int, int, bl::chunk *)), this, SLOT(handle_task_finished_task(int, int, int, bl::chunk *)));
    this->insert_chunk_to_queue(p);
    this->pool_.start(task);
    return nullptr;
}

bool AsyncLevelLoader::init(const std::string &path) {
    qDebug() << "open level" << path.c_str();
    this->level_.set_cache(false);
    auto res = this->level_.open(path);
    if (res) {
        this->loadered_ = true;
        qDebug() << "open level" << path.c_str() << "Success";
    } else {
        qDebug() << "open level" << path.c_str() << "Failure";
    }
    return res;
}

void AsyncLevelLoader::handle_task_finished_task(int x, int z, int dim, bl::chunk *chunk) {
    if (chunk) {
        this->chunk_cache_[dim]->insert(bl::chunk_pos{x, z, dim}, chunk);
    } else {
        this->invalid_cache_[dim]->insert(bl::chunk_pos{x, z, dim}, new char(0));
    }
    this->remove_chunk_from_queue(bl::chunk_pos{x, z, dim});
}

AsyncLevelLoader::~AsyncLevelLoader() { this->close(); }

void LoadChunkTask::run() {
    auto *chunk = this->level_->get_chunk(this->pos_);
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

void AsyncLevelLoader::close() {
    if (!this->loadered_) return;
    this->loadered_ = false;    //阻止UI层请求数据
    this->processing_.clear();  //队列清除
    this->pool_.clear();        //清除所有任务
    this->pool_.waitForDone();  //等待当前任务完成
    qDebug() << "clear thread pool!";
    this->level_.close();  //关闭存档
    for (auto &c : this->chunk_cache_) c->clear();
    qDebug() << "close level";
}
