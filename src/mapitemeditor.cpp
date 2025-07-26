#include "mapitemeditor.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>

#include "mainwindow.h"
#include "msg.h"
#include "ui_mapitemeditor.h"

MapItemEditor::MapItemEditor(MainWindow *mw, QWidget *parent) : QWidget(parent), mw_(mw), ui(new Ui::MapItemEditor) {
    ui->setupUi(this);
    this->setWindowTitle("Map Item Editor");
    this->map_nbt_editor_ = new NbtWidget();
    this->map_nbt_editor_->hideLoadDataBtn();
    ui->splitter->insertWidget(0, this->map_nbt_editor_);
    ui->splitter->setStretchFactor(0, 2);
    ui->splitter->setStretchFactor(1, 1);
    this->img = QImage(128, 128, QImage::Format_RGBA8888);
    img.fill(QColor(0, 0, 0, 0));
    this->map_nbt_editor_->setExtraLoadEvent([this](bl::palette::compound_tag *root) {
        if (!root) return;
        auto *color_tag = dynamic_cast<bl::palette::byte_array_tag *>(root->get("colors"));
        if (!color_tag || color_tag->value.size() != 65536) return;
        for (int i = 0; i < 128; i++) {
            for (int j = 0; j < 128; j++) {
                const int n = (i * 128 + j) * 4;
                this->img.setPixelColor(
                    i, j, QColor((uint8_t)color_tag->value[n], (uint8_t)color_tag->value[n + 1], (uint8_t)color_tag->value[n + 2]));
            }
        }

        this->update();
    });
}

MapItemEditor::~MapItemEditor() {
    this->clearData();
    delete ui;
}

void MapItemEditor::load_map_data(const bl::general_kv_nbts &data) {
    qInfo() << "Loading map data...";
    std::vector<NBTListItem *> items;
    for (auto &kv : data.data()) {
        auto *it = NBTListItem::from(dynamic_cast<bl::palette::compound_tag *>(kv.second->copy()), kv.first.c_str(), kv.first.c_str());
        items.push_back(it);
    }
    this->map_nbt_editor_->loadNewData(items);
}

void MapItemEditor::paintEvent(QPaintEvent *event) {
    auto w = ui->image_widget->width();
    int h = ui->image_widget->height() - 200;
    const int MAP_WIDTH = std::min(w, h) - 20;

    int start_x = ui->image_widget->x() + (ui->image_widget->width() - MAP_WIDTH) / 2 + 10;
    int start_y = ui->image_widget->y() + 30;
    QPainter painter(this);
    painter.drawImage(QRect{start_x, start_y, MAP_WIDTH, MAP_WIDTH}, img, QRect(0, 0, 128, 128));
}

void MapItemEditor::on_export_map_btn_clicked() {
    bool ok;
    int i = QInputDialog::getInt(this, tr("另存为"), tr("设置缩放比例"), 1, 1, 16, 1, &ok);

    if (!ok) return;

    auto new_img = img.scaled(img.width() * i, img.height() * i);
    auto fileName = QFileDialog::getSaveFileName(this, tr("Save File"), "/home/jana/untitled.png", tr("Images (*.png *.jpg)"));
    if (fileName.isEmpty()) return;
    new_img.save(fileName);
}

void MapItemEditor::on_change_map_btn_clicked() {
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open File"), "/home", tr("Images (*.png *.jpg)"));

    if (fileName.isEmpty()) {
        return;
    }

    QImage new_img(fileName);
    if (new_img.width() != new_img.height()) {
        WARN("图片的长宽不一致,将会进行自动拉伸");
        return;
    }

    auto scale_img = new_img.scaled(this->img.width(), this->img.height());
    this->img = scale_img;
    // TODO 写入图数据
    auto *it = this->map_nbt_editor_->openedItem();
    if (!it || !it->root_) return;
    auto *color_tag = dynamic_cast<bl::palette::byte_array_tag *>(it->root_->get("colors"));
    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            const int n = (i * 128 + j) * 4;
            auto c = this->img.pixelColor(i, j);
            color_tag->value[n] = (int8_t)c.red();
            color_tag->value[n + 1] = (int8_t)c.green();
            color_tag->value[n + 2] = (int8_t)c.blue();
        }
    }
    this->map_nbt_editor_->putModifyToCache(it->raw_key.toStdString(), it->root_->to_raw());
    this->update();
}

void MapItemEditor::on_save_map_btn_clicked() {
    if (!CHECK_CONDITION(this->mw_->enable_write(), "未开启写模式")) return;
    CHECK_DATA_SAVE(this->mw_->levelLoader()->modifyDBGlobal(this->map_nbt_editor_->getModifyCache()));
    this->map_nbt_editor_->clearModifyCache();
}
