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

    ui->map_tab_grid->addWidget(new MapWidget(), 1, 1);
}

MainWindow::~MainWindow() { delete ui; }
