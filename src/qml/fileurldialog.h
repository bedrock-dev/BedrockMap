#ifndef BEDROCKMAP_FILEURLDIALOG_H
#define BEDROCKMAP_FILEURLDIALOG_H

#include <QtQuickDialogs2/private/qquickfiledialog_p.h>

/// Retains provider URLs for open and save dialogs without local-file validation.
class FileUrlDialog : public QQuickFileDialog {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl selectedUrl READ selectedUrl NOTIFY selectedUrlChanged FINAL)

   public:
    explicit FileUrlDialog(QObject* parent = nullptr);
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

#endif  // BEDROCKMAP_FILEURLDIALOG_H
