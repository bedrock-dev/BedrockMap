#include "renderfilterdialog.h"

#include <QColor>

#include "asynclevelloader.h"
#include "color.h"
#include "iconmanager.h"
#include "ui_renderfilterdialog.h"

namespace {
    QColor height_to_color(int height, int dim) {
        static int min[]{-64, 0, 0};
        static int max[]{319, 127, 255};
        if (height < min[dim]) height = min[dim];
        if (height > max[dim]) height = max[dim];
        auto gray = static_cast<int>(static_cast<qreal>(height - min[dim]) / static_cast<qreal>(max[dim] - min[dim]) * 255.0);
        return {255 - gray, 255 - gray, 255 - gray};
    }
}  // namespace

RenderFilterDialog::RenderFilterDialog(QWidget *parent) : QDialog(parent), ui(new Ui::RenderFilterDialog) {
    ui->setupUi(this);
    this->setWindowTitle("配置地图过滤器");
    ui->layer_slider->setSingleStep(1);
    ui->layer_slider->setRange(-63, 319);
}

RenderFilterDialog::~RenderFilterDialog() { delete ui; }

void RenderFilterDialog::fillInUI() {
    // layer
    ui->layer_slider->setValue(this->filter_.layer);
    ui->enable_layer_box->setChecked(this->filter_.enable_layer_);
    ui->block_black_box->setChecked(this->filter_.block_black_mode_);
    ui->biome_black_box->setChecked(this->filter_.biome_black_mode_);
    ui->actor_black_box->setChecked(this->filter_.actor_black_mode_);

    QStringList actor_list;
    for (const auto &actor : this->filter_.actors_list_) actor_list << actor.c_str();
    ui->actor_text_edit->setPlainText(actor_list.join(','));

    QStringList block_list;
    for (const auto &block : this->filter_.blocks_list_) block_list << block.c_str();
    ui->block_text_edit->setPlainText(block_list.join(','));

    QStringList biome_list;
    for (const auto &biome : this->filter_.biomes_list_) biome_list << QString::number(biome);
    ui->biome_text_edit->setPlainText(biome_list.join(','));
}

void RenderFilterDialog::collectFilerData() {
    this->filter_.layer = ui->layer_slider->value();
    this->filter_.enable_layer_ = ui->enable_layer_box->isChecked();
    this->filter_.block_black_mode_ = ui->block_black_box->isChecked();
    this->filter_.actor_black_mode_ = ui->actor_black_box->isChecked();
    this->filter_.biome_black_mode_ = ui->biome_black_box->isChecked();

    auto blocks = ui->block_text_edit->toPlainText().trimmed().split(",");
    auto biomes = ui->biome_text_edit->toPlainText().trimmed().split(",");
    auto actors = ui->actor_text_edit->toPlainText().trimmed().split(",");

    this->filter_.blocks_list_.clear();
    this->filter_.biomes_list_.clear();
    this->filter_.actors_list_.clear();

    for (const auto &b : blocks) {
        auto s = b.trimmed();
        if (!s.isEmpty()) this->filter_.blocks_list_.insert(s.toStdString());
    }
    for (const auto &b : biomes) {
        auto s = b.trimmed();
        if (!s.isEmpty()) this->filter_.biomes_list_.insert(s.toInt());
    }
    for (const auto &b : actors) {
        auto s = b.trimmed();
        if (!s.isEmpty()) this->filter_.actors_list_.insert(s.toStdString());
    }
}

void RenderFilterDialog::on_current_layer_lineedit_textEdited(const QString &arg1) {
    ui->layer_slider->setValue(ui->current_layer_lineedit->text().toInt());
}

void RenderFilterDialog::on_layer_slider_valueChanged(int value) { ui->current_layer_lineedit->setText(QString::number(value)); }

void MapFilter::renderImages(bl::chunk *ch, int rw, int rh, ChunkRegion *region) const {
    if (!ch || !region) return;
    auto [miny, maxy] = ch->get_pos().get_y_range(ch->get_version());
    if (this->enable_layer_) {
        // TODO
    } else {
        // 无层，从上往下寻找白名单方块
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                const int X = (rw << 4) + i;
                const int Z = (rh << 4) + j;
                auto &tips = region->tips_info_[X][Z];
                int y = ch->get_height(i, j);

                bool found{false};

                while (y >= miny) {
                    auto b = ch->get_block_fast(i, y, j);
                    if ((this->blocks_list_.count(b.name) == 0) == this->block_black_mode_) {
                        found = true;
                        break;
                    }
                    y--;
                }

                tips.height = static_cast<int16_t>(y);
                auto hc = height_to_color(tips.height, ch->get_pos().dim);
                region->height_bake_image_->setPixelColor(X, Z, hc);

                if (found) {
                    auto info = ch->get_block(i, y, j);
                    auto biome = ch->get_biome(i, y, j);
                    info.color = bl::blend_color_with_biome(info.name, info.color, biome);
                    region->terrain_bake_image_->setPixelColor(X, Z, QColor(info.color.r, info.color.g, info.color.b, info.color.a));

                    if ((this->biomes_list_.count(biome) == 0) == this->biome_black_mode_) {
                        // 群系过滤(只是不显示，没有查找功能)
                        auto biome_color = bl::get_biome_color(biome);
                        region->biome_bake_image_->setPixelColor(X, Z, QColor(biome_color.r, biome_color.g, biome_color.b, biome_color.a));
                    }
                    tips.block_name = info.name;
                    tips.biome = biome;
                } else {
                    tips.biome = bl::none;
                    tips.block_name = "void";
                }
            }
        }
    }
}

// void MapFilter::bakeChunkHeight(bl::chunk *ch, int rw, int rh, ChunkRegion *region) const {
//     if (!ch || !region)return;
//     for (int i = 0; i < 16; i++) {
//         for (int j = 0; j < 16; j++) {
//             auto &tips = region->tips_info_[(rw << 4) + i][(rh << 4) + j];
//             int y = this->enable_layer_ ? this->layer : ch->get_height(i, j);
//             region->height_bake_image_->setPixelColor((rw << 4) + i, (rh << 4) + j,
//                                                       height_to_color(y, ch->get_pos().dim));
//             tips.height = static_cast<int16_t>(y);
//         }
//     }
// }
//
// void MapFilter::bakeChunkTerrain(bl::chunk *ch, int rw, int rh, ChunkRegion *region) const {
//     if (!ch || !region)return;
//     auto [miny, maxy] = ch->get_pos().get_y_range(ch->get_version());
//     if (this->enable_layer_) { //有层，直接显示层数据
//         if (this->layer > maxy || this->layer < miny)return;
//         for (int i = 0; i < 16; i++) {
//             for (int j = 0; j < 16; j++) {
//                 auto &tips = region->tips_info_[(rw << 4) + i][(rh << 4) + j];
//                 auto block = ch->get_block(i, this->layer, j);
//                 auto name = QString(block.name.c_str()).replace("minecraft:", "");
//                 if ((this->blocks_list_.count(name.toStdString()) == 0) == this->block_black_mode_) {
//                     block.color = bl::blend_color_with_biome(block.name, block.color, tips.biome);
//                     region->terrain_bake_image_->setPixelColor((rw << 4) + i, (rh << 4) + j,
//                                                                QColor(block.color.r, block.color.g, block.color.b,
//                                                                       block.color.a));
//                     tips.height = static_cast<int16_t>(this->layer);
//                     tips.block_name = name.toStdString();
//                 }
//             }
//         }
//     } else {
//         //无层，从上往下寻找白名单方块
//         for (int i = 0; i < 16; i++) {
//             for (int j = 0; j < 16; j++) {
//                 int y = ch->get_height(i, j);
//                 bool found{false};
//                 auto &tips = region->tips_info_[(rw << 4) + i][(rh << 4) + j];
//                 while (y >= miny) {
//                     auto b = ch->get_block(i, y, j);
//                     if (b.name != "minecraft:unknown" &&
//                         (this->blocks_list_.count(b.name) == 0) == this->block_black_mode_) {
//                         found = true;
//                         break;
//                     }
//                     y--;
//                 }
//                 tips.height = static_cast<int16_t>(y);
//                 auto info = ch->get_block(i, y, j);
//                 auto biome = ch->get_biome(i, y, j);
//                 if (found) {
//                     info.color = bl::blend_color_with_biome(info.name, info.color, biome);
//                     region->terrain_bake_image_->setPixelColor((rw << 4) + i, (rh << 4) + j,
//                                                                QColor(info.color.r,
//                                                                       info.color.g, info.color.b,
//                                                                       info.color.a));
//
//
//                     auto biome_color = bl::get_biome_color(biome);
//                     region->biome_bake_image_->setPixelColor((rw << 4) + i, (rh << 4) + j,
//                                                              QColor(biome_color.r, biome_color.g, biome_color.b,
//                                                                     biome_color.a));
//                     tips.block_name = info.name;
//                     tips.biome = biome;
//                 }
//             }
//         }
//     }
// }
//
// void MapFilter::bakeChunkBiome(bl::chunk *ch, int rw, int rh, ChunkRegion *region) const {
//     if (!ch || !region)return;
//
//     auto [miny, maxy] = ch->get_pos().get_y_range(ch->get_version());
//     if (this->enable_layer_) { //有层，直接显示层数据
//         if (this->layer > maxy || this->layer < miny)return;
//         for (int i = 0; i < 16; i++) {
//             for (int j = 0; j < 16; j++) {
//                 auto &tips = region->tips_info_[(rw << 4) + i][(rh << 4) + j];
//                 auto biome = ch->get_biome(i, this->layer, j);
//                 if (
//                         biome != bl::biome::none &&
//                         ((this->biomes_list_.count(static_cast<int>(biome)) == 0) == this->biome_black_mode_)
//                         ) {
//                     auto c = bl::get_biome_color(biome);
//                     region->biome_bake_image_->setPixelColor((rw << 4) + i, (rh << 4) + j,
//                                                              QColor(c.r, c.g, c.b, c.a));
//
//                     tips.biome = biome;
//                 }
//             }
//         }
//     } else {
//         //无层，从上往下寻找白名单方块
//         for (int i = 0; i < 16; i++) {
//             for (int j = 0; j < 16; j++) {
//                 int y = ch->get_height(i, j) + 1;
//                 while (y >= miny) {
//                     auto &tips = region->tips_info_[(rw << 4) + i][(rh << 4) + j];
//                     auto biome = ch->get_biome(i, y, j);
//                     if (biome != bl::none &&
//                         (this->biomes_list_.count(static_cast<int>(biome)) == 0) == this->biome_black_mode_) {
//                         auto c = bl::get_biome_color(biome);
//                         region->biome_bake_image_->setPixelColor((rw << 4) + i, (rh << 4) + j,
//                                                                  QColor(c.r, c.g, c.b, c.a));
//
//
//                         tips.biome = biome;
//                         break;
//                     }
//                     y--;
//                 }
//             }
//         }
//     }
// }

void MapFilter::bakeChunkActors(bl::chunk *ch, ChunkRegion *region) const {
    if (!ch) return;
    auto entities = ch->entities();
    for (auto &e : entities) {
        auto key = QString(e->identifier().c_str()).replace("minecraft:", "");
        if ((this->actors_list_.count(key.toStdString()) == 0) == this->actor_black_mode_) {
            region->actors_[ActorImage(key)].push_back(e->pos());
        }
    }
}
