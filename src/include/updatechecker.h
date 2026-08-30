#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>

// Asynchronously queries the GitHub latest-release API and compares the result
// against the current build. Emits exactly one of updateAvailable / upToDate /
// checkFailed per checkForUpdates() call.
class UpdateChecker : public QObject {
    Q_OBJECT

   public:
    explicit UpdateChecker(QObject *parent = nullptr);

    // Start an async check. Any in-flight request is cancelled first.
    void checkForUpdates();

    [[nodiscard]] static QString apiUrl();

   signals:
    void updateAvailable(const QString &newVersion, const QString &releaseNotes, const QString &htmlUrl);
    void upToDate();
    void checkFailed(const QString &message);

   private:
    void handleReply(QNetworkReply *reply);

    QNetworkAccessManager network_;
    QPointer<QNetworkReply> active_reply_;
};
