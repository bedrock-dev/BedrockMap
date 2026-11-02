#ifndef BEDROCKMAP_FOLDERURLDIALOG_H
#define BEDROCKMAP_FOLDERURLDIALOG_H

#include <QtQuickDialogs2/private/qquickfolderdialog_p.h>

/// Retains Android tree URIs and stages browser directory uploads.
class FolderUrlDialog : public QQuickFolderDialog {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl selectedUrl READ selectedUrl NOTIFY selectedUrlChanged FINAL)

   public:
    explicit FolderUrlDialog(QObject* parent = nullptr);
    [[nodiscard]] QUrl selectedUrl() const;

   public slots:
    void open();
    void done(int result) override;

   signals:
    void selectedUrlChanged();

   protected:
    void onCreate(QPlatformDialogHelper* dialog) override;
    void accept() override;

   private:
    void captureSelection(QPlatformFileDialogHelper* dialog);
    QUrl selected_url_;
    bool browser_pending_{false};
};

#endif  // BEDROCKMAP_FOLDERURLDIALOG_H
