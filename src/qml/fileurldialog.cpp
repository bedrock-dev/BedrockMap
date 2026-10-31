#include "fileurldialog.h"

FileUrlDialog::FileUrlDialog(QObject* parent) : QQuickFileDialog(parent) {
    connect(this, &QQuickFileDialog::selectedFileChanged, this, &FileUrlDialog::selectedUrlChanged);
}

QUrl FileUrlDialog::selectedUrl() const {
#ifdef Q_OS_ANDROID
    if (useNativeDialog()) return selected_url_;
#endif
    return selectedFile();
}

void FileUrlDialog::captureSelection(QPlatformFileDialogHelper* dialog) {
    const QUrl url = dialog->selectedFiles().value(0);
    if (selected_url_ == url) return;
    selected_url_ = url;
    emit selectedUrlChanged();
}

void FileUrlDialog::onCreate(QPlatformDialogHelper* dialog) {
    QQuickFileDialog::onCreate(dialog);
#ifdef Q_OS_ANDROID
    if (auto* fileDialog = qobject_cast<QPlatformFileDialogHelper*>(dialog); fileDialog && useNativeDialog()) {
        // Qt 6.11 converts content:// through urlToLocalFileOrQrc(), producing
        // an empty path and rejecting the native selection as nonexistent.
        disconnect(fileDialog, &QPlatformFileDialogHelper::currentChanged, this, nullptr);
        connect(fileDialog, &QPlatformFileDialogHelper::currentChanged, this, [this, fileDialog] { captureSelection(fileDialog); });
    }
#endif
}

void FileUrlDialog::accept() {
#ifdef Q_OS_ANDROID
    if (auto* fileDialog = qobject_cast<QPlatformFileDialogHelper*>(handle()); fileDialog && useNativeDialog()) {
        // The platform helper already owns the URI and its SAF permission.
        // Snapshot it before accepted() handlers or dialog teardown can run.
        captureSelection(fileDialog);
        QQuickAbstractDialog::accept();
        return;
    }
#endif
    QQuickFileDialog::accept();
}
