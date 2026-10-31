#ifndef BEDROCKMAP_FILEOPENHANDLER_H
#define BEDROCKMAP_FILEOPENHANDLER_H

#include <QList>
#include <QObject>
#include <QUrl>

class QGuiApplication;

/// Receives platform file-open requests and holds them until the UI is ready.
class FileOpenHandler final : public QObject {
    Q_OBJECT

   public:
    explicit FileOpenHandler(QGuiApplication& application);
    ~FileOpenHandler() override;

    void start();

   public slots:
    void openUrl(const QUrl& url);

   signals:
    void fileOpenRequested(const QUrl& url);

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    QGuiApplication& application_;
    QList<QUrl> pending_;
    bool ready_{false};
};

#endif  // BEDROCKMAP_FILEOPENHANDLER_H
