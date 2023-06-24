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
    connect(this->map, SIGNAL(mouseMove(int, int)), this, SLOT(updateXZEdit(int, int)));
    ui->x_edit->setValidator(new QIntValidator(-1000000, 1000000, this));
    ui->z_edit->setValidator(new QIntValidator(-1000000, 1000000, this));
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::updateXZEdit(int x, int z) {
    bl::block_pos bp{x, 0, z};
    auto cp = bp.to_chunk_pos();
    ui->block_pos_label->setText(QString::number(x) + "," + QString::number(z) + " in [" + QString::number(cp.x) + "," + QString::number(cp.z) + "]");
}

void MainWindow::on_goto_btn_clicked() {
    int x = ui->x_edit->text().toInt();
    int z = ui->z_edit->text().toInt();
    this->map->gotoBlockPos(x, z);
}
