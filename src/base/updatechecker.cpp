#include "base/updatechecker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QtConcurrent>
#else
#include <QNetworkRequest>
#include <QUrl>
#endif

#include "base/config.h"
#include "loguru/loguru.hpp"

QString UpdateChecker::apiUrl() { return QStringLiteral("https://api.github.com/repos/bedrock-dev/BedrockMap/releases?per_page=100"); }

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent) {
#ifdef Q_OS_ANDROID
    connect(&fetch_watcher_, &QFutureWatcher<QByteArray>::finished, this, [this] {
        const auto data = fetch_watcher_.result();
        if (data.isEmpty()) {
            emit checkFailed(tr("Unable to check for updates."));
            return;
        }
        handleResponse(data);
    });
#endif
}

void UpdateChecker::checkForUpdates() {
    const QString userAgent = QString("%1/%2").arg(QString::fromUtf8(constant::SOFTWARE_NAME), constant::softwareVersion().toString());
#ifdef Q_OS_ANDROID
    if (fetch_watcher_.isRunning()) return;
    // Use the system TLS implementation; the worker captures no QObject state.
    fetch_watcher_.setFuture(QtConcurrent::run([url = apiUrl(), userAgent] {
        const auto javaUrl = QJniObject::fromString(url);
        const auto javaAgent = QJniObject::fromString(userAgent);
        const auto response = QJniObject::callStaticObjectMethod(
            "io/github/hhhxiao/bedrockmap/UpdateRequest", "fetch", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
            javaUrl.object<jstring>(), javaAgent.object<jstring>());
        return response.isValid() ? response.toString().toUtf8() : QByteArray{};
    }));
#else
    if (active_reply_) return;

    QNetworkRequest request{QUrl(apiUrl())};
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(10000);  // 10 s so the check never hangs forever

    QNetworkReply* reply = network_.get(request);
    active_reply_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleReply(reply); });
#endif
}

#ifndef Q_OS_ANDROID
void UpdateChecker::handleReply(QNetworkReply* reply) {
    reply->deleteLater();
    if (active_reply_ != reply) return;
    active_reply_ = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        LOG_F(WARNING, "Update check failed: %s", reply->errorString().toStdString().c_str());
        emit checkFailed(reply->errorString());
        return;
    }
    handleResponse(reply->readAll());
}
#endif

void UpdateChecker::handleResponse(const QByteArray& data) {
    const auto fail = [this](const QString& message) {
        LOG_F(WARNING, "Update check failed: %s", message.toStdString().c_str());
        emit checkFailed(message);
    };
    const auto doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        fail(tr("updateChecker.parseFailed"));
        return;
    }

    // Include beta releases and compare versions instead of publication dates.
    std::optional<AppVersion> latest;
    QJsonObject latestRelease;
    const auto releases = doc.array();
    for (const auto& entry : releases) {
        const auto release = entry.toObject();
        if (release.value("draft").toBool()) continue;
        const auto version = AppVersion::parse(release.value("tag_name").toString());
        if (!version || (latest && version->compare(*latest) <= 0)) continue;
        latest = version;
        latestRelease = release;
    }
    if (!latest && !releases.isEmpty()) {
        fail(tr("updateChecker.invalidVersion"));
        return;
    }

    if (latest && latest->compare(constant::softwareVersion()) > 0) {
        emit updateAvailable(latestRelease.value("tag_name").toString(), latestRelease.value("body").toString(),
                             latestRelease.value("html_url").toString());
    } else {
        emit upToDate();
    }
}
