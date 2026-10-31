#ifndef BEDROCKMAP_FOLDERURLDIALOG_H
#define BEDROCKMAP_FOLDERURLDIALOG_H

#include <QtQuickDialogs2/private/qquickfolderdialog_p.h>

/// Retains tree URIs selected by Android's native directory picker.
class FolderUrlDialog : public QQuickFolderDialog {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl selectedUrl READ selectedUrl NOTIFY selectedUrlChanged FINAL)

   public:
    explicit FolderUrlDialog(QObject* parent = nullptr);
    [[nodiscard]] QUrl selectedUrl() const;

   signals:
    void selectedUrlChanged();

   protected:
    void onCreate(QPlatformDialogHelper* dialog) override;
    void accept() override;

   private:
    void captureSelection(QPlatformFileDialogHelper* dialog);
    QUrl selected_url_;
};

#endif  // BEDROCKMAP_FOLDERURLDIALOG_H
