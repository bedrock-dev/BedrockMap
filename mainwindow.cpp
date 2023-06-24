#include "mainwindow.h"

#include <QGridLayout>
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
    if (this->world.init(R"(C:\Users\xhy\Desktop\t)")) {
        this->map = new MapWidget(&this->world, nullptr);
        ui->map_tab_grid->addWidget(map, 1, 1);
    } else {
        ui->map_tab_grid->addWidget(new QLabel("Invalid world path"), 1, 1);
    }

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
