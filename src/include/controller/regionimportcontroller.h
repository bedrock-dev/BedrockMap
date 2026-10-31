#ifndef BEDROCKMAP_REGIONIMPORTCONTROLLER_H
#define BEDROCKMAP_REGIONIMPORTCONTROLLER_H

#include <QByteArray>
#include <QFutureWatcher>
#include <QObject>
#include <QPointF>
#include <QRegion>
#include <QUrl>
#include <memory>

#include "loader/exportedregion.h"

/// A read-only source and a chunk-aligned placement, independent of any UI.
class RegionImportController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int targetX READ targetX WRITE setTargetX NOTIFY previewChanged)
    Q_PROPERTY(int targetZ READ targetZ WRITE setTargetZ NOTIFY previewChanged)
    Q_PROPERTY(int width READ width NOTIFY activeChanged)
    Q_PROPERTY(int height READ height NOTIFY activeChanged)
    Q_PROPERTY(int chunkCount READ chunkCount NOTIFY activeChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY activeChanged)

   public:
    explicit RegionImportController(QObject* parent);
    ~RegionImportController() override;
    [[nodiscard]] bool active() const { return bool(source_); }
    [[nodiscard]] bool loading() const { return loading_; }
    [[nodiscard]] int targetX() const { return target_.x(); }
    [[nodiscard]] int targetZ() const { return target_.y(); }
    [[nodiscard]] int width() const { return source_region_.boundingRect().width(); }
    [[nodiscard]] int height() const { return source_region_.boundingRect().height(); }
    [[nodiscard]] int chunkCount() const { return source_ ? static_cast<int>(source_->chunkCount()) : 0; }
    [[nodiscard]] QString sourceName() const { return source_name_; }
    [[nodiscard]] int dimension() const { return dimension_; }
    [[nodiscard]] QPoint offset() const { return target_ - source_region_.boundingRect().topLeft(); }
    [[nodiscard]] QRegion region() const { return active() ? source_region_.translated(offset()) : QRegion{}; }
    [[nodiscard]] std::shared_ptr<const ExportedRegion> source() const { return source_; }
    bool loadFile(const QUrl& url, const QPoint& target, int dimension);
    bool loadBytes(QByteArray bytes, const QString& sourceName, const QPoint& target, int dimension);
    void setTargetX(int value);
    void setTargetZ(int value);
    bool beginDrag(const QPointF& chunkPosition);
    void updateDrag(const QPointF& chunkPosition);
    void endDrag() { dragging_ = false; }
    Q_INVOKABLE void cancel();
    void waitForLoad();

   signals:
    void activeChanged();
    void loadingChanged();
    void previewChanged();
    void failed(const QString& message);

   private:
    struct LoadResult {
        std::shared_ptr<const ExportedRegion> source;
        QRegion region;
        QString error;
    };
    static LoadResult decodeRegion(const QByteArray& bytes);
    void beginLoad(const QString& sourceName, const QPoint& target, int dimension);
    void finishLoad();
    void setTarget(const QPoint& value);
    [[nodiscard]] QPoint boundedTarget(const QPoint& value) const;

    QFutureWatcher<LoadResult> watcher_;
    std::shared_ptr<const ExportedRegion> source_;
    QRegion source_region_;
    QString source_name_;
    QPoint target_;
    QPoint drag_target_;
    QPointF drag_start_;
    int dimension_{0};
    bool loading_{false};
    bool canceled_{false};
    bool dragging_{false};
};

#endif  // BEDROCKMAP_REGIONIMPORTCONTROLLER_H
