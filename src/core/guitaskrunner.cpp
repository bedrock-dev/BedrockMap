#include "guitaskrunner.h"

#include <QtConcurrent>

GuiTaskRunner::GuiTaskRunner(QObject *parent) : QObject(parent) {}

GuiTaskRunner::~GuiTaskRunner() = default;

bool GuiTaskRunner::start(Worker worker) {
    if (running_.load()) return false;
    running_.store(true);
    cancel_requested_.store(false);
    failed_.store(false);
    {
        std::lock_guard<std::mutex> lk(text_mu_);
        error_.clear();
    }

    emit started();

    // A fresh watcher per run avoids a queued finished() from a previous run
    // racing with a new start() on a reused watcher.
    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        onWorkerFinished();
    });
    auto future = QtConcurrent::run([this, worker] { worker(this); });
    watcher->setFuture(future);
    {
        std::lock_guard<std::mutex> lk(future_mu_);
        current_future_ = future;
    }
    return true;
}

void GuiTaskRunner::cancel() { cancel_requested_.store(true); }

void GuiTaskRunner::waitForFinished() {
    QFuture<void> future;
    {
        std::lock_guard<std::mutex> lk(future_mu_);
        future = current_future_;
    }
    if (future.isValid()) future.waitForFinished();
}

void GuiTaskRunner::reportProgress(int value) { emit progressChanged(value, QString()); }

void GuiTaskRunner::fail(const QString &error) {
    failed_.store(true);
    std::lock_guard<std::mutex> lk(text_mu_);
    error_ = error;
}

bool GuiTaskRunner::isCancelRequested() const { return cancel_requested_.load(); }

bool GuiTaskRunner::isRunning() const { return running_.load(); }

void GuiTaskRunner::onWorkerFinished() {
    if (!running_.load()) return;
    running_.store(false);

    const bool wasCancelled = cancel_requested_.load();
    const bool isFailed = failed_.load();
    QString error;
    {
        std::lock_guard<std::mutex> lk(text_mu_);
        error = error_;
    }

    if (wasCancelled) {
        emit cancelled();
    } else if (isFailed) {
        emit failed(error.isEmpty() ? "Task failed" : error);
    } else {
        emit finished();
    }
}
