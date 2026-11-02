#include "folderurldialog.h"

#ifdef Q_OS_WASM
#include <QPointer>
#include "controller/appcontroller.h"
#include "io/browservfs.h"
#endif

FolderUrlDialog::FolderUrlDialog(QObject* parent) : QQuickFolderDialog(parent) {
    connect(this, &QQuickFolderDialog::selectedFolderChanged, this, &FolderUrlDialog::selectedUrlChanged);
}

QUrl FolderUrlDialog::selectedUrl() const {
#ifdef Q_OS_WASM
    return selected_url_;
#elif defined(Q_OS_ANDROID)
    if (useNativeDialog()) return selected_url_;
#endif
    return selectedFolder();
}

void FolderUrlDialog::open() {
#ifdef Q_OS_WASM
    if (browser_pending_) return;
    browser_pending_ = true;
    io::BrowserVfsBackend::selectDirectory(
        [self = QPointer<FolderUrlDialog>(this)](const QUrl& url, const QString& error) {
            if (!self) return;
            self->browser_pending_ = false;
            if (!error.isEmpty()) AppController::instance()->notifications()->error(error);
            if (url.isEmpty()) { self->reject(); return; }
            self->selected_url_ = url;
            emit self->selectedUrlChanged();
            self->QQuickAbstractDialog::accept();
        });
#else
    QQuickFolderDialog::open();
#endif
}

void FolderUrlDialog::done(int result) {
#ifdef Q_OS_WASM
    setResult(result);
    if (result == Accepted) emit accepted();
    else emit rejected();
#else
    QQuickFolderDialog::done(result);
#endif
}

void FolderUrlDialog::captureSelection(QPlatformFileDialogHelper* dialog) {
    const QUrl url = dialog->selectedFiles().value(0);
    if (selected_url_ == url) return;
    selected_url_ = url;
    emit selectedUrlChanged();
}

void FolderUrlDialog::onCreate(QPlatformDialogHelper* dialog) {
    QQuickFolderDialog::onCreate(dialog);
#ifdef Q_OS_ANDROID
    if (auto* folderDialog = qobject_cast<QPlatformFileDialogHelper*>(dialog); folderDialog && useNativeDialog()) {
        // Qt converts provider URIs to local files while validating the native
        // selection, which rejects SAF trees and directory documents.
        disconnect(folderDialog, &QPlatformFileDialogHelper::currentChanged, this, nullptr);
        connect(folderDialog, &QPlatformFileDialogHelper::currentChanged, this, [this, folderDialog] { captureSelection(folderDialog); });
    }
#endif
}

void FolderUrlDialog::accept() {
#ifdef Q_OS_ANDROID
    if (auto* folderDialog = qobject_cast<QPlatformFileDialogHelper*>(handle()); folderDialog && useNativeDialog()) {
        captureSelection(folderDialog);
        QQuickAbstractDialog::accept();
        return;
    }
#endif
    QQuickFolderDialog::accept();
}
