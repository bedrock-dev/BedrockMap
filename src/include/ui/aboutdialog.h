#ifndef BEDROCKMAP_ABOUTDIALOG_H
#define BEDROCKMAP_ABOUTDIALOG_H

#include <QDialog>

namespace Ui {
    class AboutDialog;
}

class UpdateChecker;

class AboutDialog : public QDialog {
    Q_OBJECT

   public:
    explicit AboutDialog(QWidget *parent = nullptr);

    ~AboutDialog() override;

   private slots:
    void onCheckForUpdates();
    void onUpdateAvailable(const QString &newVersion, const QString &releaseNotes, const QString &htmlUrl);
    void onUpToDate();
    void onCheckFailed(const QString &message);

   private:
    void restoreCheckLink();

    Ui::AboutDialog *ui;
    UpdateChecker *updater_{nullptr};
};

#endif  // BEDROCKMAP_ABOUTDIALOG_H
