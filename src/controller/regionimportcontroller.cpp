#include "controller/regionimportcontroller.h"

#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <vector>

#include "io/documentio.h"

namespace {
    // raw_chunk::move_to uses signed block coordinates and a signed block offset.
    constexpr int min_chunk = std::numeric_limits<int>::min() / 16;
    constexpr int max_chunk = (std::numeric_limits<int>::max() - 15) / 16;
}  // namespace

RegionImportController::RegionImportController(QObject* parent) : QObject(parent) {
    connect(&watcher_, &QFutureWatcher<LoadResult>::finished, this, &RegionImportController::finishLoad);
}

RegionImportController::~RegionImportController() { waitForLoad(); }

bool RegionImportController::loadFile(const QUrl& url, const QPoint& target, int dimension) {
    if (loading_ || active()) return false;
    if (url.isEmpty()) return false;
    beginLoad(io::DocumentIo::displayName(url), target, dimension);
    watcher_.setFuture(QtConcurrent::run([url] {
        try {
            const auto read = io::DocumentIo::readAll(url);
            if (!read.ok()) return LoadResult{{}, {}, RegionImportController::tr("The region file could not be read: %1").arg(read.error)};
            return decodeRegion(read.data);
        } catch (const std::exception& error) {
            return LoadResult{
                {}, {}, RegionImportController::tr("The region file could not be loaded: %1").arg(QString::fromUtf8(error.what()))};
        } catch (...) {
            return LoadResult{{}, {}, RegionImportController::tr("The region file could not be loaded.")};
        }
    }));
    return true;
}

bool RegionImportController::loadBytes(QByteArray bytes, const QString& sourceName, const QPoint& target, int dimension) {
    if (loading_ || active()) return false;
    beginLoad(sourceName, target, dimension);
    watcher_.setFuture(QtConcurrent::run([bytes = std::move(bytes)] { return decodeRegion(bytes); }));
    return true;
}

void RegionImportController::beginLoad(const QString& sourceName, const QPoint& target, int dimension) {
    target_ = target;
    dimension_ = dimension;
    source_name_ = sourceName;
    canceled_ = false;
    loading_ = true;
    emit loadingChanged();
}

RegionImportController::LoadResult RegionImportController::decodeRegion(const QByteArray& bytes) {
    try {
        auto source = std::make_shared<ExportedRegion>(ExportedRegion::deserialize(bytes.constData(), bytes.size()));
        if (source->isEmpty() || source->chunkCount() > static_cast<size_t>(std::numeric_limits<int>::max()))
            return LoadResult{{}, {}, RegionImportController::tr("The region data is empty or invalid.")};

        // Merge each row into runs before unioning it, preserving holes and
        // keeping large rectangular imports inexpensive to draw and hit-test.
        std::map<int, std::vector<int>> rows;
        const int source_dimension = source->chunks().front().pos().dim;
        for (const auto& chunk : source->chunks()) {
            const auto pos = chunk.pos();
            if (pos.dim != source_dimension || pos.x < min_chunk || pos.x > max_chunk || pos.z < min_chunk || pos.z > max_chunk)
                return LoadResult{
                    {}, {}, RegionImportController::tr("The region contains invalid chunk coordinates or multiple dimensions.")};
            rows[pos.z].push_back(pos.x);
        }
        QRegion region;
        for (auto& [z, columns] : rows) {
            std::sort(columns.begin(), columns.end());
            int begin = columns.front();
            int end = begin;
            for (size_t i = 1; i < columns.size(); ++i) {
                const int x = columns[i];
                if (x == end) return LoadResult{{}, {}, RegionImportController::tr("The region contains duplicate chunks.")};
                if (x == end + 1) {
                    end = x;
                } else {
                    region += QRect(begin, z, end - begin + 1, 1);
                    begin = end = x;
                }
            }
            region += QRect(begin, z, end - begin + 1, 1);
        }
        return LoadResult{std::move(source), std::move(region), {}};
    } catch (const std::exception& error) {
        return LoadResult{{}, {}, tr("The region data could not be loaded: %1").arg(QString::fromUtf8(error.what()))};
    } catch (...) {
        return LoadResult{{}, {}, tr("The region data could not be loaded.")};
    }
}

void RegionImportController::finishLoad() {
    // Consume the result so canceling a large preview also releases its data.
    auto future = watcher_.future();
    const auto result = future.takeResult();
    loading_ = false;
    if (!canceled_ && result.source) {
        source_ = result.source;
        source_region_ = result.region;
        target_ = boundedTarget(target_);
        // Loading only establishes an initial world position; hover never moves it.
        emit activeChanged();
        emit previewChanged();
    }
    emit loadingChanged();
    if (!canceled_ && !result.error.isEmpty()) emit failed(result.error);
}

QPoint RegionImportController::boundedTarget(const QPoint& value) const {
    const QRect bounds = source_region_.boundingRect();
    if (bounds.isEmpty()) return value;
    const auto bound = [](int value, int origin, int extent) {
        const qint64 low = std::max<qint64>(min_chunk, qint64(origin) + min_chunk);
        const qint64 high = std::min<qint64>(qint64(max_chunk) - extent + 1, qint64(origin) + max_chunk);
        return static_cast<int>(std::clamp<qint64>(value, low, high));
    };
    return {bound(value.x(), bounds.x(), bounds.width()), bound(value.y(), bounds.y(), bounds.height())};
}

void RegionImportController::setTarget(const QPoint& value) {
    if (!active()) return;
    const QPoint bounded = boundedTarget(value);
    if (target_ == bounded) return;
    target_ = bounded;
    emit previewChanged();
}

void RegionImportController::setTargetX(int value) { setTarget({value, target_.y()}); }
void RegionImportController::setTargetZ(int value) { setTarget({target_.x(), value}); }

bool RegionImportController::beginDrag(const QPointF& chunkPosition) {
    dragging_ = false;
    if (!active() || !std::isfinite(chunkPosition.x()) || !std::isfinite(chunkPosition.y())) return false;
    const QPointF local = chunkPosition - QPointF(offset());
    const QRect bounds = source_region_.boundingRect();
    if (local.x() < bounds.left() || local.x() >= qreal(bounds.right()) + 1 || local.y() < bounds.top() ||
        local.y() >= qreal(bounds.bottom()) + 1 ||
        !source_region_.contains(QPoint(static_cast<int>(std::floor(local.x())), static_cast<int>(std::floor(local.y())))))
        return false;
    dragging_ = true;
    drag_start_ = chunkPosition;
    drag_target_ = target_;
    return true;
}

void RegionImportController::updateDrag(const QPointF& chunkPosition) {
    if (!dragging_ || !std::isfinite(chunkPosition.x()) || !std::isfinite(chunkPosition.y())) return;
    const QPointF delta = chunkPosition - drag_start_;
    // Clamp before converting to int, including gestures at the overview zoom floor.
    const auto coordinate = [](qreal value) { return static_cast<int>(std::round(std::clamp(value, qreal(min_chunk), qreal(max_chunk)))); };
    setTarget({coordinate(drag_target_.x() + delta.x()), coordinate(drag_target_.y() + delta.y())});
}

void RegionImportController::cancel() {
    canceled_ = true;
    dragging_ = false;
    const bool was_active = active();
    source_.reset();
    source_region_ = {};
    if (was_active) {
        emit activeChanged();
        emit previewChanged();
    }
}

void RegionImportController::waitForLoad() {
    if (watcher_.isRunning()) watcher_.waitForFinished();
}
