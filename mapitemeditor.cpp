#include "mapitemeditor.h"
#include "ui_mapitemeditor.h"
#include <QPainter>

MapItemEditor::MapItemEditor(QWidget *parent) : QWidget(parent),
                                                ui(new Ui::MapItemEditor) {
    ui->setupUi(this);
    this->map_nbt_editor_ = new NbtWidget();
    this->map_nbt_editor_->hideLoadDataBtn();
    ui->splitter->insertWidget(0, this->map_nbt_editor_);
    ui->splitter->setStretchFactor(0, 2);
    ui->splitter->setStretchFactor(1, 1);
    this->img = QImage(128, 128, QImage::Format_RGBA8888);
    this->map_nbt_editor_->setExtraLoadEvent([this](bl::palette::compound_tag *root) {
        if (!root)return;
        auto *color_tag = dynamic_cast<bl::palette::byte_array_tag *>(root->get("colors"));
        if (!color_tag || color_tag->value.size() != 65536)return;
        for (int i = 0; i < 128; i++) {
            for (int j = 0; j < 128; j++) {
                const int n = (i * 128 + j) * 4;
                this->img.setPixelColor(i, j, QColor(
                        (uint8_t) color_tag->value[n],
                        (uint8_t) color_tag->value[n + 1],
                        (uint8_t) color_tag->value[n + 2]
                ));
            }
        }

        this->update();
    });
}


MapItemEditor::~MapItemEditor() {
    delete ui;
}

void MapItemEditor::load_map_data(const bl::general_kv_nbts &data) {
    std::vector<bl::palette::compound_tag *> tags;
    std::vector<std::string> labels;
    for (auto &kv: data.data()) {
        tags.push_back(dynamic_cast<bl::palette::compound_tag *>( kv.second->copy()));
        labels.push_back(kv.first);
    }

    this->map_nbt_editor_->load_new_data(tags, [](const bl::palette::compound_tag *) { return ""; }, labels, {});
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



