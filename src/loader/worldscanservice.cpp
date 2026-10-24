#include "loader/worldscanservice.h"

#include <QMetaObject>
#include <QSet>
#include <QtConcurrent>
#include <algorithm>
#include <exception>
#include <utility>

#include "base/config.h"
#include "base/platform.h"
#include "loader/worldsource.h"
#include "model/worldpath.h"

namespace {

    void reportProgress(QObject* receiver, int value) {
        QMetaObject::invokeMethod(
            receiver,
            [receiver, value] {
                if (auto* service = qobject_cast<WorldScanService*>(receiver)) service->reportProgress(value);
            },
            Qt::QueuedConnection);
    }

    std::vector<WorldMetadata> collectWorlds(QObject* receiver, const QStringList& customPaths, bool scanLeviPath,
                                             bool allFilesAccess) {
        const auto sources = makeWorldSources(customPaths, scanLeviPath, allFilesAccess);
        int availableCount = 0;
        for (const auto& source : sources)
            if (source.availability == WorldSourceAvailability::Available) ++availableCount;

        std::vector<WorldMetadata> result;
        QSet<QString> seen;
        int processed = 0;
        for (const auto& source : sources) {
            if (source.availability != WorldSourceAvailability::Available) continue;

            for (auto& info : scanWorldSource(source)) {
                const auto path = world_path::normalize(QString::fromStdString(info.path));
                const auto key = world_path::identityKey(path);
                if (key.isEmpty() || seen.contains(key)) continue;
                seen.insert(key);
                info.path = path.toStdString();
                result.push_back(std::move(info));
            }

            ++processed;
            reportProgress(receiver, availableCount == 0 ? 100 : processed * 100 / availableCount);
        }
        return result;
    }

}  // namespace

WorldScanService::WorldScanService(QObject* parent) : QObject(parent) {
    connect(&watcher_, &QFutureWatcher<std::vector<WorldMetadata>>::finished, this, &WorldScanService::onFinished);
}

WorldScanService::~WorldScanService() {
    if (watcher_.isRunning()) watcher_.waitForFinished();
}

bool WorldScanService::start() {
    if (loading_) {
        // A settings/permission change arrived while the current scan was
        // running; run once more as soon as it finishes.
        restart_pending_ = true;
        return false;
    }
    setError({});
    worlds_.clear();
    setProgress(0);
    setLoading(true);
    const QStringList custom_paths = setting::current().customWorldPaths;
    const bool scan_levi = setting::current().scanLeviPath;
    const bool all_files_access = platform::hasAllFilesAccess();
    watcher_.setFuture(QtConcurrent::run([this, custom_paths, scan_levi, all_files_access] {
        return collectWorlds(this, custom_paths, scan_levi, all_files_access);
    }));
    return true;
}

void WorldScanService::setLoading(bool value) {
    if (loading_ == value) return;
    loading_ = value;
    emit loadingChanged();
}

void WorldScanService::setProgress(int value) {
    value = std::clamp(value, 0, 100);
    if (progress_ == value) return;
    progress_ = value;
    emit progressChanged();
}

void WorldScanService::reportProgress(int value) { setProgress(value); }

void WorldScanService::setError(const QString& value) {
    if (error_ == value) return;
    error_ = value;
    emit errorChanged();
}

void WorldScanService::onFinished() {
    try {
        worlds_ = watcher_.result();
        setProgress(100);
    } catch (const std::exception& ex) {
        setError(QString::fromUtf8(ex.what()));
    } catch (...) {
        setError(QStringLiteral("World scan failed."));
    }
    setLoading(false);
    emit finished();
    if (restart_pending_) {
        restart_pending_ = false;
        start();
    }
}
