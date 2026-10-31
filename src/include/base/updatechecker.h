#ifndef BEDROCKMAP_UPDATECHECKER_H
#define BEDROCKMAP_UPDATECHECKER_H
#include <QByteArray>
#include <QObject>
#include <QString>
#ifdef Q_OS_ANDROID
#include <QFutureWatcher>
#else
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#endif

// Asynchronously queries GitHub releases and compares them against the build.
// Completed checks emit updateAvailable, upToDate or checkFailed.
class UpdateChecker : public QObject {
    Q_OBJECT

   public:
    explicit UpdateChecker(QObject* parent = nullptr);

    // Start an async check unless one is already in flight.
    void checkForUpdates();

    [[nodiscard]] static QString apiUrl();

   signals:
    void updateAvailable(const QString& newVersion, const QString& releaseNotes, const QString& htmlUrl);
    void upToDate();
    void checkFailed(const QString& message);

   private:
    void handleResponse(const QByteArray& data);
#ifdef Q_OS_ANDROID
    QFutureWatcher<QByteArray> fetch_watcher_;
#else
    void handleReply(QNetworkReply* reply);

    QNetworkAccessManager network_;
    QPointer<QNetworkReply> active_reply_;
#endif
};
#endif  // BEDROCKMAP_UPDATECHECKER_H
