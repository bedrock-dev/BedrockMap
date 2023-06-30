#include "mainwindow.h"

#include <QDialog>
#include <QFileDialog>
#include <QGridLayout>
#include <QMessageBox>
#include <QSplitter>
#include <QtDebug>

#include "./ui_mainwindow.h"
#include "mapwidget.h"
#include "nbtwidget.h"

void MainWindow::keyPressEvent(QKeyEvent *event) {}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);

    this->map_ = new MapWidget(this, nullptr);
    this->map_->gotoBlockPos(0, 0);
    this->chunk_editor_widget_ = new ChunkEditorWidget();
    ui->map_visual_layout->insertWidget(1, this->map_, 10);
    ui->splitter->replaceWidget(1, this->chunk_editor_widget_);

    // layer btn
    this->layer_btns_ = {{MapWidget::LayerType::Biome,   ui->biome_layer_btn},
                         {MapWidget::LayerType::Terrain, ui->terrain_layer_btn},

                         {MapWidget::LayerType::Height,  ui->height_layer_btn},
                         {MapWidget::LayerType::Slime,   ui->slime_layer_btn}};

    for (auto &kv: this->layer_btns_) {
        QObject::connect(kv.second, &QPushButton::clicked, [this, &kv]() {
            for (auto &x: this->layer_btns_) {
                x.second->setEnabled(x.first != kv.first);
            }
            this->map_->changeLayer(kv.first);
        });
    }

    // Dimension Btn
    this->dim_btns_ = {
            {MapWidget::DimType::OverWorld, ui->overwrold_btn},
            {MapWidget::DimType::Nether,    ui->nether_btn},
            {MapWidget::DimType::TheEnd,    ui->theend_btn},
    };

    for (auto &kv: this->dim_btns_) {
        QObject::connect(kv.second, &QPushButton::clicked, [this, &kv]() {
            for (auto &x: this->dim_btns_) {
                x.second->setEnabled(x.first != kv.first);
            }
            this->map_->changeDimension(kv.first);
        });
    }

    // Label and edit
    connect(this->map_, SIGNAL(mouseMove(int, int)), this, SLOT(updateXZEdit(int, int)));

    // menu actions
    connect(ui->action_open, SIGNAL(triggered()), this, SLOT(open_level()));
    connect(ui->action_close, SIGNAL(triggered()), this, SLOT(close_level()));
    connect(ui->action_full_map, SIGNAL(triggered()), this, SLOT(toggle_full_map_mode()));

    connect(ui->action_exit, SIGNAL(triggered()), this, SLOT(close_and_exit()));

    connect(ui->action_NBT, SIGNAL(triggered()), this, SLOT(openNBTEditor()));

    // init stat
    ui->biome_layer_btn->setEnabled(false);
    ui->overwrold_btn->setEnabled(false);
    ui->grid_checkbox->setChecked(true);
    ui->text_checkbox->setChecked(true);
    ui->debug_checkbox->setChecked(false);

    // debug
    ui->x_edit->setMaximumWidth(160);
    ui->z_edit->setMaximumWidth(160);

    // ui->chunk_edit_widget->setVisible(false);
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::updateXZEdit(int x, int z) {
    bl::block_pos bp{x, 0, z};
    auto cp = bp.to_chunk_pos();
    ui->block_pos_label->setText(QString::number(x) + "," + QString::number(z) + " in [" + QString::number(cp.x) + "," +
                                 QString::number(cp.z) + "]");
}

void MainWindow::openChunkEditor(const bl::chunk_pos &p) {
    if (!this->world_.is_open()) {
        QMessageBox::information(NULL, "警告", "未打开存档", QMessageBox::Yes, QMessageBox::Yes);
        return;
    }
    // add a watcher
    qDebug() << "try read chunk";
    auto *chunk = this->world_.getChunkDirect(p).result();
    qDebug() << "read chunk success fully";
    if (!chunk) {
        QMessageBox::information(NULL, "警告", "无法打开区块数据", QMessageBox::Yes, QMessageBox::Yes);
    } else {
        this->chunk_editor_widget_->setVisible(true);
        this->chunk_editor_widget_->load_chunk_data(chunk);
    }
}

void MainWindow::on_goto_btn_clicked() {
    int x = ui->x_edit->text().toInt();
    int z = ui->z_edit->text().toInt();
    this->map_->gotoBlockPos(x, z);
}

void MainWindow::on_grid_checkbox_stateChanged(int arg1) { this->map_->enableGrid(arg1 > 0); }

void MainWindow::on_text_checkbox_stateChanged(int arg1) { this->map_->enableText(arg1 > 0); }

void MainWindow::open_level() {
    QString root = QFileDialog::getExistingDirectory(this, tr("打开存档根目录"), "/home",
                                                     QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (root.size() == 0) {
        return;
    }

    if (!this->world_.init(root.toStdString())) {
        QMessageBox::information(NULL, "警告", "无法打开存档", QMessageBox::Yes, QMessageBox::Yes);
    }
}

void MainWindow::close_level() { this->world_.close(); }

void MainWindow::close_and_exit() {
    this->world_.close();
    this->close();
}

void MainWindow::on_debug_checkbox_stateChanged(int arg1) { this->map_->enableDebug(arg1 > 0); }

void MainWindow::toggle_chunk_edit_view() {
    auto x = this->chunk_editor_widget_->isVisible();
    this->chunk_editor_widget_->setVisible(!x);
}

void MainWindow::toggle_full_map_mode() {
    this->full_map_mode_ = !full_map_mode_;

    ui->map_toolbar_wdiget->setVisible(!full_map_mode_);
    ui->map_top_toolbar_widget->setVisible(!full_map_mode_);
    ui->map_buttom_toolbar_widget->setVisible(!full_map_mode_);
    if (this->full_map_mode_) {
        this->chunk_edit_widget_hided_ = this->chunk_editor_widget_->isVisible();
        this->chunk_editor_widget_->setVisible(false);
    } else {
        this->chunk_editor_widget_->setVisible(this->chunk_edit_widget_hided_);
    }
}

void MainWindow::on_enable_chunk_edit_check_box_stateChanged(int arg1) { this->write_mode_ = arg1 > 0; }

void MainWindow::on_screenshot_btn_clicked() { this->map_->saveImage(QRect{0, 0, 1, 1}); }

void MainWindow::openNBTEditor() {
    auto *w = new NbtWidget();
    auto g = this->geometry();
    const int ext = 100;
    w->setWindowTitle("NBT Editor");
    w->setGeometry(QRect(g.x() + ext, g.y() + ext, g.width() - ext * 2, g.height() - ext * 2));
    w->show();
}
