#include "updatechecker.h"

#include <QNetworkRequest>
#include <QUrl>

#include "json/json.hpp"
#include "loguru/loguru.hpp"

using json = nlohmann::json;

QString UpdateChecker::apiUrl() { return QStringLiteral("https://api.github.com/repos/bedrock-dev/BedrockMap/releases/latest"); }

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent) {}

void UpdateChecker::checkForUpdates() {
    if (active_reply_) active_reply_->abort();  // supersede any in-flight request

    QNetworkRequest request{QUrl(apiUrl())};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QString("%1/%2").arg(QString::fromStdString(constant::SOFTWARE_NAME), constant::SOFTWARE_VERSION.toString()));
    request.setTransferTimeout(10000);  // 10 s so the check never hangs forever

    QNetworkReply *reply = network_.get(request);
    active_reply_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleReply(reply); });
}

void UpdateChecker::handleReply(QNetworkReply *reply) {
    if (active_reply_ == reply) active_reply_ = nullptr;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() == QNetworkReply::OperationCanceledError) return;  // superseded by a newer check
        LOG_F(WARNING, "Update check failed: %s", reply->errorString().toStdString().c_str());
        emit checkFailed(reply->errorString());
        return;
    }

    json doc;
    try {
        doc = json::parse(reply->readAll().toStdString());
    } catch (const json::exception &) {
        emit checkFailed(tr("updateChecker.parseFailed"));
        return;
    }
    if (!doc.is_object()) {
        emit checkFailed(tr("updateChecker.parseFailed"));
        return;
    }

    const QString tagName = QString::fromStdString(doc.value("tag_name", ""));
    const QString releaseNotes = QString::fromStdString(doc.value("body", ""));
    const QString htmlUrl = QString::fromStdString(doc.value("html_url", ""));

    const auto latest = AppVersion::parse(tagName);
    if (!latest) {
        LOG_F(WARNING, "Cannot compare versions: current=%s latest=%s", constant::SOFTWARE_VERSION.toString().toStdString().c_str(),
              tagName.toStdString().c_str());
        emit checkFailed(tr("updateChecker.invalidVersion"));
        return;
    }

    if (latest->compare(constant::SOFTWARE_VERSION) > 0) {
        emit updateAvailable(tagName, releaseNotes, htmlUrl);
    } else {
        emit upToDate();
    }
}
