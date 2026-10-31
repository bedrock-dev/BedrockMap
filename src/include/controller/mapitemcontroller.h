#ifndef BEDROCKMAP_MAPITEMCONTROLLER_H
#define BEDROCKMAP_MAPITEMCONTROLLER_H

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class NbtDocumentController;

namespace bl::nbt {
    class compound_tag;
}

/// Image operations and lock state for the selected map-item NBT record.
class MapItemController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString imageSource READ imageSource NOTIFY imageSourceChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imageSourceChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY lockedChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

   public:
    explicit MapItemController(NbtDocumentController* document, QObject* parent = nullptr);

    void reload();
    [[nodiscard]] QString imageSource() const { return image_source_; }
    [[nodiscard]] bool hasImage() const { return !image_.isNull(); }
    [[nodiscard]] bool locked() const { return locked_; }
    [[nodiscard]] QString lastError() const { return last_error_; }
    Q_INVOKABLE bool saveImage(const QUrl& url);
    Q_INVOKABLE bool replaceImage(const QUrl& url);
    Q_INVOKABLE bool lockMap();

   signals:
    void imageSourceChanged();
    void lockedChanged();
    void lastErrorChanged();

   private:
    [[nodiscard]] bl::nbt::compound_tag* editableRoot();
    void setError(const QString& error);

    QPointer<NbtDocumentController> document_;
    QImage image_;
    QString image_source_;
    QString last_error_;
    bool locked_{false};
};

#endif  // BEDROCKMAP_MAPITEMCONTROLLER_H
