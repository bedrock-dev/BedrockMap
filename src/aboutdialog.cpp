#include "aboutdialog.h"

#include <qcontainerfwd.h>
#include <qnamespace.h>

#include <QIcon>

#include "config.h"
#include "ui_aboutdialog.h"

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent), ui(new Ui::AboutDialog) {
    ui->setupUi(this);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);
    setWindowTitle(tr("aboutDialog.title.about") + " - " + cfg::SOFTWARE_NAME.c_str());

    // Set logo
    ui->logo->setPixmap(QPixmap(":/res/ui/classic/icon.png").scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // Set name & version with rich text formatting
    QString name = QString::fromStdString(cfg::SOFTWARE_NAME);
    QString ver = QString::fromStdString(cfg::SOFTWARE_VERSION) + "." + QString(GIT_COMMIT_HASH);
    QString text = QString("<h2>%1</h2><p>%2</p>").arg(name, ver);
    ui->name_version_label->setText(text);

    // Hyperlinks — URLs stay in code, display text via tr() for i18n
    auto link = [](const QString& url, const QString& text) { return QString("<a href=\"%1\">%2</a>").arg(url, text); };

    ui->developer_link->setText(QString(tr("aboutDialog.developer")).arg(link(QStringLiteral("https://github.com/hhhxiao"), "hhhxiao")));
    ui->contributor_intzot_link->setText(link(QStringLiteral("https://space.bilibili.com/382492603"), "INTZOT"));
    ui->contributor_oeyan_link->setText(link(QStringLiteral("https://github.com/OEOTYAN"), "OEOYAN"));
    ui->checkUpdateLink->setText(link(QStringLiteral("https://github.com/bedrock-dev/BedrockMap/releases"), tr("aboutDialog.checkUpdate")));
    ui->joinQQGroupLink->setText(link(QStringLiteral("https://qm.qq.com/q/"), tr("aboutDialog.joinQQGroup")));
    ui->githubLink->setText(link(QStringLiteral("https://github.com/bedrock-dev/BedrockMap"), tr("aboutDialog.github")));
    // Close button
    connect(ui->closeBtn, &QPushButton::clicked, this, &QDialog::close);
}

AboutDialog::~AboutDialog() { delete ui; }
