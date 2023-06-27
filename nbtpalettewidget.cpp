#include "nbtpalettewidget.h"

#include "ui_nbtpalettewidget.h"

NbtPaletteWidget::NbtPaletteWidget(QWidget *parent) : QWidget(parent), ui(new Ui::NbtPaletteWidget) {
    ui->setupUi(this);
    this->nbt_widget_ = new NbtWidget();
    ui->splitter->replaceWidget(1, this->nbt_widget_);
}

NbtPaletteWidget::~NbtPaletteWidget()
{
    delete ui;
}
