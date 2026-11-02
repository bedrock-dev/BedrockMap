#include "fileurldialog.h"

#ifdef Q_OS_WASM
#include "io/browservfs.h"
#endif

FileUrlDialog::FileUrlDialog(QObject* parent) : QQuickFileDialog(parent) {
    connect(this, &QQuickFileDialog::selectedFileChanged, this, &FileUrlDialog::selectedUrlChanged);
}

QUrl FileUrlDialog::selectedUrl() const {
#if defined(Q_OS_ANDROID) || defined(Q_OS_WASM)
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
#if defined(Q_OS_ANDROID) || defined(Q_OS_WASM)
    if (auto* fileDialog = qobject_cast<QPlatformFileDialogHelper*>(dialog); fileDialog && useNativeDialog()) {
        // Qt validates provider URIs as local paths, rejecting native selections.
        disconnect(fileDialog, &QPlatformFileDialogHelper::currentChanged, this, nullptr);
        connect(fileDialog, &QPlatformFileDialogHelper::currentChanged, this, [this, fileDialog] { captureSelection(fileDialog); });
    }
#endif
}

void FileUrlDialog::accept() {
#if defined(Q_OS_ANDROID) || defined(Q_OS_WASM)
    if (auto* fileDialog = qobject_cast<QPlatformFileDialogHelper*>(handle()); fileDialog && useNativeDialog()) {
        // The platform helper already owns the URI and its access permission.
        // Snapshot it before accepted() handlers or dialog teardown can run.
        captureSelection(fileDialog);
#ifdef Q_OS_WASM
        if (fileMode() == SaveFile) io::BrowserVfsBackend::registerWritable(selected_url_);
#endif
        QQuickAbstractDialog::accept();
        return;
    }
#endif
    QQuickFileDialog::accept();
}

#ifdef Q_OS_WASM
void FileUrlDialog::onShow(QPlatformDialogHelper* dialog) {
    QQuickFileDialog::onShow(dialog);
    auto* fileDialog = qobject_cast<QPlatformFileDialogHelper*>(dialog);
    if (fileMode() == SaveFile && fileDialog && fileDialog->selectedFiles().isEmpty()) {
        const QString suffix = defaultSuffix();
        fileDialog->selectFile(QUrl::fromLocalFile(QStringLiteral("/document") + (suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix)));
    }
}
#endif
