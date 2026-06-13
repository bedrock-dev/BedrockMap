#include "gotopositiondialog.h"

#include <qvalidator.h>

#include "ui_gotopositiondialog.h"

GoToPositionDialog::GoToPositionDialog(QWidget *parent) : QDialog(parent), ui(new Ui::GoToPositionDialog) {
    ui->setupUi(this);
    setWindowTitle(tr("goToPositionDialog.title.goto"));
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);
    ui->x_lineedit->setValidator(new QIntValidator(this));
    ui->z_lineedit->setValidator(new QIntValidator(this));
    // todo
}

bool GoToPositionDialog::positionValid() { return !ui->x_lineedit->text().isEmpty() && !ui->z_lineedit->text().isEmpty(); }

int GoToPositionDialog::x() { return ui->x_lineedit->text().toInt(); }
int GoToPositionDialog::z() { return ui->z_lineedit->text().toInt(); }

GoToPositionDialog::~GoToPositionDialog() { delete ui; }
