#include "aboutdialog.h"

#include <qcontainerfwd.h>
#include <qnamespace.h>

#include <QIcon>
#include <QMessageBox>

#include "config.h"
#include "ui_aboutdialog.h"
#include "updatechecker.h"
#include "updatedialog.h"

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent), ui(new Ui::AboutDialog), updater_(new UpdateChecker(this)) {
    ui->setupUi(this);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);
    setWindowTitle(tr("aboutDialog.title.about") + " - " + constant::SOFTWARE_NAME.c_str());

    // Set logo
    ui->logo->setPixmap(QPixmap(":/res/ui/classic/icon.png").scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // Set name & version with rich text formatting
    QString name = QString::fromStdString(constant::SOFTWARE_NAME);
    QString ver = constant::SOFTWARE_VERSION.toString() + "." + QString(GIT_COMMIT_HASH);
    QString text = QString("<h2>%1</h2><p>%2</p>").arg(name, ver);
    ui->name_version_label->setText(text);

    // Hyperlinks — URLs stay in code, display text via tr() for i18n
    auto link = [](const QString &url, const QString &text) { return QString("<a href=\"%1\">%2</a>").arg(url, text); };

    ui->developer_link->setText(QString(tr("aboutDialog.developer")).arg(link(QStringLiteral("https://github.com/hhhxiao"), "hhhxiao")));
    ui->contributor_intzot_link->setText(link(QStringLiteral("https://space.bilibili.com/382492603"), "INTZOT"));
    ui->contributor_oeyan_link->setText(link(QStringLiteral("https://github.com/OEOTYAN"), "OEOYAN"));
    ui->joinQQGroupLink->setText(link(QStringLiteral("https://qm.qq.com/q/3KJZZCkDbW"), tr("aboutDialog.joinQQGroup")));
    ui->githubLink->setText(link(QStringLiteral("https://github.com/bedrock-dev/BedrockMap"), tr("aboutDialog.github")));
    restoreCheckLink();

    // Check for updates — triggered by clicking the check-update link
    connect(ui->checkUpdateLink, &QLabel::linkActivated, this, &AboutDialog::onCheckForUpdates);
    connect(updater_, &UpdateChecker::updateAvailable, this, &AboutDialog::onUpdateAvailable);
    connect(updater_, &UpdateChecker::upToDate, this, &AboutDialog::onUpToDate);
    connect(updater_, &UpdateChecker::checkFailed, this, &AboutDialog::onCheckFailed);

    // Close button
    connect(ui->closeBtn, &QPushButton::clicked, this, &QDialog::close);
}

AboutDialog::~AboutDialog() { delete ui; }

void AboutDialog::restoreCheckLink() {
    // The link never navigates; clicking it only triggers the update check.
    ui->checkUpdateLink->setText(QString("<a href=\"%1\">%2</a>").arg(UpdateChecker::apiUrl(), tr("aboutDialog.checkUpdate")));
}

void AboutDialog::onCheckForUpdates() {
    ui->checkUpdateLink->setText(tr("aboutDialog.checkingUpdate"));
    updater_->checkForUpdates();
}

void AboutDialog::onUpdateAvailable(const QString &newVersion, const QString &releaseNotes, const QString &htmlUrl) {
    restoreCheckLink();
    UpdateDialog dlg(newVersion, releaseNotes, htmlUrl, this);
    dlg.exec();
}

void AboutDialog::onUpToDate() {
    restoreCheckLink();
    QMessageBox::information(this, tr("aboutDialog.checkUpdate"), tr("updateChecker.upToDate"));
}

void AboutDialog::onCheckFailed(const QString &message) {
    restoreCheckLink();
    QMessageBox::warning(this, tr("aboutDialog.checkUpdate"), tr("updateChecker.checkFailed").arg(message));
}
