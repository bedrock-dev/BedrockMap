#include "folderurldialog.h"

FolderUrlDialog::FolderUrlDialog(QObject* parent) : QQuickFolderDialog(parent) {
    connect(this, &QQuickFolderDialog::selectedFolderChanged, this, &FolderUrlDialog::selectedUrlChanged);
}

QUrl FolderUrlDialog::selectedUrl() const {
#ifdef Q_OS_ANDROID
    if (useNativeDialog()) return selected_url_;
#endif
    return selectedFolder();
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
