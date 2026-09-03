#ifndef BEDROCKMAP_UPDATEDIALOG_H
#define BEDROCKMAP_UPDATEDIALOG_H
#include <QDialog>

namespace Ui {
    class UpdateDialog;
}

// Modal dialog shown when a newer release is found. Displays the new version
// and the release notes from the GitHub API, with a button to open the release
// page in the browser.
class UpdateDialog : public QDialog {
    Q_OBJECT

   public:
    explicit UpdateDialog(const QString &newVersion, const QString &releaseNotes, const QString &htmlUrl, QWidget *parent = nullptr);
    ~UpdateDialog() override;

   private:
    Ui::UpdateDialog *ui;
    QString html_url_;
};
#endif  // BEDROCKMAP_UPDATEDIALOG_H
