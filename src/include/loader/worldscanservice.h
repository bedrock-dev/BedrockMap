#ifndef BEDROCKMAP_WORLDSCANSERVICE_H
#define BEDROCKMAP_WORLDSCANSERVICE_H

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <vector>

#include "model/worldmetadata.h"

class WorldScanService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

   public:
    explicit WorldScanService(QObject* parent = nullptr);
    ~WorldScanService() override;

    [[nodiscard]] bool isLoading() const { return loading_; }
    [[nodiscard]] int progress() const { return progress_; }
    [[nodiscard]] QString error() const { return error_; }
    [[nodiscard]] const std::vector<WorldMetadata>& worlds() const { return worlds_; }

    // Starts a scan on a worker thread. Returns false if a scan is already running.
    bool start();
    void reportProgress(int value);

   signals:
    void loadingChanged();
    void progressChanged();
    void errorChanged();
    void finished();

   private:
    void setLoading(bool value);
    void setProgress(int value);
    void setError(const QString& value);
    void onFinished();

    QFutureWatcher<std::vector<WorldMetadata>> watcher_;
    std::vector<WorldMetadata> worlds_;
    bool loading_{false};
    bool restart_pending_{false};
    int progress_{0};
    QString error_;
};

#endif  // BEDROCKMAP_WORLDSCANSERVICE_H
