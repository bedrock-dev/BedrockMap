#ifndef BEDROCKMAP_WORLDSCANSERVICE_H
#define BEDROCKMAP_WORLDSCANSERVICE_H

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <vector>

#include "loader/worldsource.h"

struct WorldScanResult {
    std::vector<WorldMetadata> worlds;
    std::vector<WorldMetadata> recentWorlds;
};

class WorldScanService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

   public:
    explicit WorldScanService(QObject* parent = nullptr);
    ~WorldScanService() override;

    [[nodiscard]] bool isLoading() const { return loading_; }
    [[nodiscard]] QString error() const { return error_; }
    [[nodiscard]] const std::vector<WorldMetadata>& worlds() const { return result_.worlds; }
    [[nodiscard]] const std::vector<WorldMetadata>& recentWorlds() const { return result_.recentWorlds; }

    // Starts a scan on a worker thread. Returns false if a scan is already running.
    bool start(const QStringList& recentPaths = {});

   signals:
    void loadingChanged();
    void errorChanged();
    void finished();

   private:
    void setLoading(bool value);
    void setError(const QString& value);
    void onFinished();

    QFutureWatcher<WorldScanResult> watcher_;
    WorldScanResult result_;
    QStringList recent_paths_;
    bool loading_{false};
    bool restart_pending_{false};
    QString error_;
};

#endif  // BEDROCKMAP_WORLDSCANSERVICE_H
