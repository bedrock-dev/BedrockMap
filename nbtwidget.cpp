#include "nbtwidget.h"

#include "ui_nbtwidget.h"

NbtWidget::NbtWidget(QWidget *parent) :
                                        QWidget(parent),
                                        ui(new Ui::NbtWidget)
{
    ui->setupUi(this);
    ui->splitter->setStretchFactor(0, 1);
    ui->splitter->setStretchFactor(1, 3);
}

NbtWidget::~NbtWidget() { delete ui; }
