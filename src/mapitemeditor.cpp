#include "mapitemeditor.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>

#include "loguru/loguru.hpp"
#include "msg.h"
#include "palette.h"
#include "ui_mapitemeditor.h"

MapItemEditor::MapItemEditor(QWidget *parent) : QWidget(parent), ui(new Ui::MapItemEditor) {
    ui->setupUi(this);
    this->setWindowTitle(tr("mapItemEditor.title.mapItemEditor"));
    this->map_nbt_editor_ = new NbtWidget(this);
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
        this->img = QImage(128, 128, QImage::Format_RGBA8888);
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                const int n = (y * 128 + x) * 4;
                this->img.setPixelColor(
                    x, y, QColor((uint8_t)color_tag->value[n], (uint8_t)color_tag->value[n + 1], (uint8_t)color_tag->value[n + 2]));
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
    std::vector<NBTListItem *> items;
    for (auto &kv : data.data()) {
        auto *it = NBTListItem::from(dynamic_cast<bl::palette::compound_tag *>(kv.second->copy()), kv.first.c_str(), kv.first.c_str());
        items.push_back(it);
    }
    this->map_nbt_editor_->loadNewData(items);
}

void MapItemEditor::paintEvent(QPaintEvent *event) {
    int w = ui->image_widget->width();
    int h = ui->image_widget->height();
    int MAP_WIDTH = std::min(w, h) - 20;
    if (MAP_WIDTH < 16) MAP_WIDTH = 16;

    QPixmap pm = QPixmap::fromImage(img.scaled(MAP_WIDTH, MAP_WIDTH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    ui->image_widget->setPixmap(pm);
    ui->image_widget->setAlignment(Qt::AlignCenter);
}

void MapItemEditor::on_export_map_btn_clicked() {
    bool ok;
    int i = QInputDialog::getInt(this, msg::SAEVE_AS(), msg::SET_SCALE_LEVEL(), 1, 1, 16, 1, &ok);

    if (!ok) return;

    auto new_img = img.scaled(img.width() * i, img.height() * i);
    auto fileName = QFileDialog::getSaveFileName(this, tr("mapItemEditor.fileDialog.save"), "map.png", "Images (*.png *.jpg)");
    if (fileName.isEmpty()) return;
    new_img.save(fileName);
}

void MapItemEditor::on_change_map_btn_clicked() {
    QString fileName =
        QFileDialog::getOpenFileName(this, tr("mapItemEditor.fileDialog.open"), "", "Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)");

    if (fileName.isEmpty()) {
        return;
    }

    QPixmap pm;
    if (!pm.load(fileName)) {
        LOG_F(ERROR, "Cannot load image: %s", fileName.toStdString().c_str());
        return;
    }
    QImage new_img = pm.toImage();
    if (new_img.isNull()) return;
    if (new_img.width() != new_img.height()) {
        WARN(msg::IMAGE_ASPECT_MISMATCH());
    }

    this->img = new_img.scaled(128, 128, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888);

    auto *it = this->map_nbt_editor_->openedItem();
    if (!it || !it->root_) return;
    auto *color_tag = dynamic_cast<bl::palette::byte_array_tag *>(it->root_->get("colors"));
    if (!color_tag) {
        LOG_F(ERROR, "Can not find colors node, cancelled");
        return;
    }
    color_tag->value.resize(128 * 128 * 4);
    for (int y = 0; y < 128; y++) {
        for (int x = 0; x < 128; x++) {
            const int n = (y * 128 + x) * 4;
            auto c = this->img.pixelColor(x, y);
            color_tag->value[n] = static_cast<uint8_t>(c.red());
            color_tag->value[n + 1] = static_cast<uint8_t>(c.green());
            color_tag->value[n + 2] = static_cast<uint8_t>(c.blue());
            color_tag->value[n + 3] = static_cast<uint8_t>(c.alpha());
        }
    }
    this->map_nbt_editor_->putModifyToCache(it->raw_key.toStdString(), it->root_->to_raw());
    this->update();
}
