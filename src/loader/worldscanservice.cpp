#include "loader/worldscanservice.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QtConcurrent>
#include <exception>
#include <utility>

#include "base/config.h"
#include "base/platform.h"
#include "loader/worldsource.h"

namespace {

    WorldScanResult collectWorlds(const QStringList& customPaths, bool scanLeviPath, const QStringList& recentPaths) {
        QElapsedTimer timer;
        timer.start();
        const auto sources = makeWorldSources(customPaths, scanLeviPath);
        qInfo() << "[WorldScan] Scan started: sources=" << sources.size() << "customPaths=" << customPaths.size()
                << "recentPaths=" << recentPaths.size();
#ifdef Q_OS_ANDROID
        qInfo().noquote() << "[WorldScan] Permissions: allFiles=" << platform::hasAllFilesPermission()
                          << "shizuku=" << platform::shizukuPermission() << "root=" << platform::rootPermission();
#endif

        WorldScanResult result;
        QHash<QString, std::size_t> seen;
        for (const auto& source : sources) {
            for (auto& info : scanWorldSource(source)) {
                const auto path = platform::normalizeLocalPath(QString::fromStdString(info.path));
                if (path.isEmpty() || seen.contains(path)) continue;
                seen.insert(path, result.worlds.size());
                info.path = path.toStdString();
                result.worlds.push_back(std::move(info));
            }
        }
        QSet<QString> recent_seen;
        for (const auto& path : recentPaths) {
            const QString normalized = platform::normalizeLocalPath(path);
            if (!normalized.isEmpty() && !recent_seen.contains(normalized)) {
                recent_seen.insert(normalized);
                const auto found = seen.constFind(normalized);
                auto info = found != seen.cend() ? result.worlds[found.value()] : readWorldMetadata(normalized);
                if (info.isValid) result.recentWorlds.push_back(std::move(info));
            }
        }
        qInfo() << "[WorldScan] Scan complete: worlds=" << result.worlds.size() << "recentWorlds=" << result.recentWorlds.size()
                << "elapsedMs=" << timer.elapsed();
        return result;
    }

}  // namespace

WorldScanService::WorldScanService(QObject* parent) : QObject(parent) {
    connect(&watcher_, &QFutureWatcher<WorldScanResult>::finished, this, &WorldScanService::onFinished);
}

WorldScanService::~WorldScanService() {
    if (watcher_.isRunning()) watcher_.waitForFinished();
}

bool WorldScanService::start(const QStringList& recentPaths) {
    recent_paths_ = recentPaths;
    if (loading_) {
        // A settings/permission change arrived while the current scan was
        // running; run once more as soon as it finishes.
        restart_pending_ = true;
        qInfo() << "[WorldScan] Refresh queued while scan is running";
        return false;
    }
    setError({});
    result_ = {};
    setLoading(true);
    const QStringList custom_paths = setting::current().customWorldPaths;
    const bool scan_levi = setting::current().scanLeviPath;
    watcher_.setFuture(QtConcurrent::run(
        [custom_paths, scan_levi, recent_paths = recent_paths_] { return collectWorlds(custom_paths, scan_levi, recent_paths); }));
    return true;
}

void WorldScanService::setLoading(bool value) {
    if (loading_ == value) return;
    loading_ = value;
    emit loadingChanged();
}

void WorldScanService::setError(const QString& value) {
    if (error_ == value) return;
    error_ = value;
    emit errorChanged();
}

void WorldScanService::onFinished() {
    try {
        result_ = watcher_.result();
    } catch (const std::exception& ex) {
        qWarning() << "[WorldScan] Scan failed:" << ex.what();
        setError(QString::fromUtf8(ex.what()));
    } catch (...) {
        qWarning() << "[WorldScan] Scan failed with an unknown exception";
        setError(QStringLiteral("World scan failed."));
    }
    setLoading(false);
    emit finished();
    if (restart_pending_) {
        restart_pending_ = false;
        start(recent_paths_);
    }
}
