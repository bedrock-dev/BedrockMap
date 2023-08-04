#include "mapitemeditor.h"
#include "ui_mapitemeditor.h"

MapItemEditor::MapItemEditor(QWidget *parent) :
                                                QWidget(parent),
                                                ui(new Ui::MapItemEditor)
{
    ui->setupUi(this);
}

MapItemEditor::~MapItemEditor()
{
    delete ui;
}
