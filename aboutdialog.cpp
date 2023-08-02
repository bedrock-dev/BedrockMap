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
}

AboutDialog::~AboutDialog() {
    delete ui;
}
