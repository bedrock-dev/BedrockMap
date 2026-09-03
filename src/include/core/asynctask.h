#ifndef BEDROCKMAP_ASYNCTASK_H
#define BEDROCKMAP_ASYNCTASK_H

#include <QFuture>
#include <QObject>
#include <QString>
#include <atomic>
#include <functional>
#include <mutex>

class AsyncTaskRunner : public QObject {
    Q_OBJECT

   public:
    using Worker = std::function<void(AsyncTaskRunner *)>;

    explicit AsyncTaskRunner(QObject *parent = nullptr);
    ~AsyncTaskRunner() override;

    bool start(Worker worker);
    void cancel();
    void fail(const QString &error);
    void waitForFinished();
    void reportProgress(int value);

    // --- thread-safe, callable from the worker thread ---
    [[nodiscard]] bool isCancelRequested() const;
    [[nodiscard]] bool isRunning() const;

   signals:
    void started();
    void progressChanged(int value, const QString &text);
    void finished();
    void failed(const QString &error);
    void cancelled();

   private:
    void onWorkerFinished();

    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_requested_{false};
    std::atomic<bool> failed_{false};
    std::mutex text_mu_;  // guards error_
    QString error_;
    std::mutex future_mu_;  // guards current_future_
    QFuture<void> current_future_;
};

#endif  // BEDROCKMAP_ASYNCTASK_H
