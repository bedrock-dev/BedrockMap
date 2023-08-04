#include "aboutdialog.h"
#include "ui_aboutdialog.h"
#include <QIcon>
#include "config.h"

AboutDialog::AboutDialog(QWidget *parent) :
        QDialog(parent),
        ui(new Ui::AboutDialog) {
    ui->setupUi(this);
    setWindowIcon(QIcon(":/res/icon.png"));
    setWindowTitle(QString("关于 ") + cfg::SOFTWARE_NAME.c_str());
    ui->info_label->setText(ui->info_label->text().replace("{NAME_VERSION}", cfg::VERSION_STRING()));
    ui->logo->setPixmap(QPixmap(":/res/icon.png").scaled(128, 128));
}

AboutDialog::~AboutDialog() {
    delete ui;
}
