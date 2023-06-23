#include "mainwindow.h"

#include <QGridLayout>

#include "./ui_mainwindow.h"
#include "mapwidget.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
      , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    ui->main_tab->setTabText(1, "NBT");
    ui->main_tab->setTabText(0, "Map");

    this->map = new MapWidget();
    ui->map_tab_grid->addWidget(map, 1, 1);
}

MainWindow::~MainWindow() { delete ui; }
