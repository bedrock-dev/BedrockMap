#include "fileopenhandler.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QGuiApplication>
#include <utility>

#include "base/platform.h"

FileOpenHandler::FileOpenHandler(QGuiApplication& application) : application_(application) {
    application_.installEventFilter(this);
#ifdef Q_OS_ANDROID
    // BedrockMapActivity converts ACTION_SEND to ACTION_VIEW. Qt forwards both
    // the launch intent and subsequent views through QDesktopServices.
    QDesktopServices::setUrlHandler(QStringLiteral("content"), this, "openUrl");
    QDesktopServices::setUrlHandler(QStringLiteral("file"), this, "openUrl");
#endif
}

FileOpenHandler::~FileOpenHandler() {
#ifdef Q_OS_ANDROID
    QDesktopServices::unsetUrlHandler(QStringLiteral("content"));
    QDesktopServices::unsetUrlHandler(QStringLiteral("file"));
#endif
    application_.removeEventFilter(this);
}

void FileOpenHandler::start() {
    ready_ = true;
    const auto pending = std::exchange(pending_, {});
    for (const auto& url : pending) openUrl(url);
}

void FileOpenHandler::openUrl(const QUrl& url) {
    if (!url.isValid() || (!url.isLocalFile() && !platform::isContentUri(url))) return;
    if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir()) {
        // Calling again inside the URL handler bypasses it, preserving actions
        // such as revealing a world's directory in the system file manager.
        QDesktopServices::openUrl(url);
        return;
    }
    // Android may deliver a new intent on a different thread. Always dispatch
    // document operations on the GUI thread and after QML initialization.
    QMetaObject::invokeMethod(
        this,
        [this, url] {
            if (!ready_) {
                pending_.append(url);
                return;
            }
            emit fileOpenRequested(url);
        },
        Qt::QueuedConnection);
}

bool FileOpenHandler::eventFilter(QObject* watched, QEvent* event) {
    if (watched == &application_ && event->type() == QEvent::FileOpen) {
        openUrl(static_cast<QFileOpenEvent*>(event)->url());
        event->accept();
        return true;
    }
    return QObject::eventFilter(watched, event);
}
