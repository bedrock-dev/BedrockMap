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

    if (this->world.init(R"(C:\Users\xhy\Desktop\t)")) {
        this->map = new MapWidget(&this->world, nullptr);
        ui->map_tab_grid->addWidget(map, 1, 1);
    } else {
        ui->map_tab_grid->addWidget(new QLabel("Invalid world path"), 1, 1);
    }
}

MainWindow::~MainWindow() { delete ui; }
