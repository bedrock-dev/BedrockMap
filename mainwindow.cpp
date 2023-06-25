#include "mainwindow.h"

#include <QDialog>
#include <QFileDialog>
#include <QGridLayout>
#include <QMessageBox>
#include <QtDebug>

#include "./ui_mainwindow.h"
#include "mapwidget.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
      , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    ui->main_tab->setTabText(1, "NBT");
    ui->main_tab->setTabText(0, "Map");

    // Main map view

    //    this->world.init(R"(C:\Users\xhy\Desktop\t)");
    this->map = new MapWidget(&this->world, nullptr);
    ui->map_tab_grid->addWidget(map, 1, 1);

    // layer btns
    this->layer_btns = {{MapWidget::LayerType::Biome, ui->biome_layer_btn},
                        {MapWidget::LayerType::Terrain, ui->terrain_layer_btn},
                        {MapWidget::LayerType::Height, ui->height_layer_btn},
                        {MapWidget::LayerType::Slime, ui->slime_layer_btn}};

    for (auto &kv : this->layer_btns) {
        QObject::connect(kv.second, &QPushButton::clicked, [this, &kv]() {
            for (auto &x : this->layer_btns) {
                x.second->setEnabled(x.first != kv.first);
            }
            this->map->changeLayer(kv.first);
        });
    }

    // Dimension Btn
    this->dim_btns = {
        {MapWidget::DimType::OverWorld, ui->overwrold_btn},
        {MapWidget::DimType::Nether, ui->nether_btn},
        {MapWidget::DimType::TheEnd, ui->theend_btn},
    };

    for (auto &kv : this->dim_btns) {
        QObject::connect(kv.second, &QPushButton::clicked, [this, &kv]() {
            for (auto &x : this->dim_btns) {
                x.second->setEnabled(x.first != kv.first);
            }
            this->map->changeDimemsion(kv.first);
        });
    }
    // Label and edit
    connect(this->map, SIGNAL(mouseMove(int, int)), this, SLOT(updateXZEdit(int, int)));
    ui->x_edit->setValidator(new QIntValidator(-1000000, 1000000, this));
    ui->z_edit->setValidator(new QIntValidator(-1000000, 1000000, this));

    // menu actions
    connect(ui->action_open, SIGNAL(triggered()), this, SLOT(open_level()));
    connect(ui->action_close, SIGNAL(triggered()), this, SLOT(close_level()));
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

void MainWindow::on_grid_checkbox_stateChanged(int arg1) { this->map->enableGrid(arg1 > 0); }

void MainWindow::on_text_checkbox_stateChanged(int arg1) { this->map->enableText(arg1 > 0); }

void MainWindow::open_level() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("打开存档根目录"), "/home", QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!this->world.init(dir.toStdString())) {
        QMessageBox::information(NULL, "警告", "无法打开存档", QMessageBox::Yes, QMessageBox::Yes);
    }
}

void MainWindow::close_level() { this->world.close(); }
