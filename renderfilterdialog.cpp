#include "renderfilterdialog.h"
#include "ui_renderfilterdialog.h"
#include "asynclevelloader.h"
#include "iconmanager.h"
#include <QColor>
#include "color.h"

RenderFilterDialog::RenderFilterDialog(QWidget *parent) :
        QDialog(parent),
        ui(new Ui::RenderFilterDialog) {
    ui->setupUi(this);
    this->setWindowTitle("配置地图过滤器");
    ui->layer_slider->setSingleStep(1);
    ui->layer_slider->setRange(-63, 319);
    this->setWindowIcon(QIcon(":/res/filter_editor.png"));
}

RenderFilterDialog::~RenderFilterDialog() {
    delete ui;
}

void RenderFilterDialog::fillInUI() {
    //layer
    ui->layer_slider->setValue(this->filter_.layer);
    ui->enable_layer_box->setChecked(this->filter_.enable_layer_);
    ui->block_black_box->setChecked(this->filter_.block_black_mode_);
    ui->biome_black_box->setChecked(this->filter_.biome_black_mode_);
    ui->actor_black_box->setChecked(this->filter_.actor_black_mode_);

    QStringList actor_list;
    for (const auto &actor: this->filter_.actors_list_)actor_list << actor.c_str();
    ui->actor_text_edit->setPlainText(actor_list.join(','));

    QStringList block_list;
    for (const auto &block: this->filter_.blocks_list_)block_list << block.c_str();
    ui->block_text_edit->setPlainText(block_list.join(','));

    QStringList biome_list;
    for (const auto &biome: this->filter_.biomes_list_)biome_list << QString::number(biome);
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

    for (const auto &b: blocks) {
        auto s = b.trimmed();
        if (!s.isEmpty())this->filter_.blocks_list_.insert(s.toStdString());
    }
    for (const auto &b: biomes) {
        auto s = b.trimmed();
        if (!s.isEmpty())this->filter_.biomes_list_.insert(s.toInt());
    }
    for (const auto &b: actors) {
        auto s = b.trimmed();
        if (!s.isEmpty())this->filter_.actors_list_.insert(s.toStdString());
    }

}

void RenderFilterDialog::on_current_layer_lineedit_textEdited(const QString &arg1) {
    ui->layer_slider->setValue(ui->current_layer_lineedit->text().toInt());
}

void RenderFilterDialog::on_layer_slider_valueChanged(int value) {
    ui->current_layer_lineedit->setText(QString::number(value));
}


void MapFilter::bakeChunkTerrain(bl::chunk *ch, int rw, int rh, chunk_region *region) const {
    if (!ch)return;

    auto [miny, maxy] = ch->get_pos().get_y_range(ch->get_version());
    if (this->enable_layer_) { //有层，直接显示层数据
        if (this->layer > maxy || this->layer < miny)return;
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                auto &tips = region->tips_info_[(rw << 4) + i][(rh << 4) + j];
                auto block = ch->get_block(i, this->layer, j);
                auto name = QString(block.name.c_str()).replace("minecraft:", "");
                if ((this->blocks_list_.count(name.toStdString()) == 0) == this->block_black_mode_) {


                    region->terrain_bake_image_->setPixelColor((rw << 4) + i, (rh << 4) + j,
                                                               QColor(block.color.r, block.color.g, block.color.b,
                                                                      block.color.a));
                    tips.height = this->layer;
                    tips.block_name = name.toStdString();
                }
            }
        }
    } else {
        //无层，从上往下寻找白名单方块
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                int y = ch->get_height(i, j);
                while (y >= miny) {
                    auto &tips = region->tips_info_[(rw << 4) + i][(rh << 4) + j];
                    auto block = ch->get_block(i, y, j);
                    auto name = QString(block.name.c_str()).replace("minecraft:", "");
                    if (name != "unknown" &&
                        (this->blocks_list_.count(name.toStdString()) == 0) == this->block_black_mode_) {
                        if (name == "water") {
                            block.color = bl::get_water_color(block.color, tips.biome);
                        }
                        region->terrain_bake_image_->setPixelColor((rw << 4) + i, (rh << 4) + j,
                                                                   QColor(block.color.r,
                                                                          block.color.g, block.color.b,
                                                                          block.color.a));
                        tips.height = y;
                        tips.block_name = name.toStdString();
                        break;
                    }
                    y--;
                }
            }
        }
    }
}

void MapFilter::bakeChunkBiome(bl::chunk *ch, int rw, int rh, chunk_region *region) const {
    if (!ch)return;

    auto [miny, maxy] = ch->get_pos().get_y_range(ch->get_version());
    if (this->enable_layer_) { //有层，直接显示层数据
        if (this->layer > maxy || this->layer < miny)return;
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                auto &tips = region->tips_info_[(rw << 4) + i][(rh << 4) + j];
                auto biome = ch->get_biome(i, this->layer, j);
                if ((this->biomes_list_.count(static_cast<int>(biome)) == 0) == this->biome_black_mode_) {
                    auto c = bl::get_biome_color(biome);
                    region->biome_bake_image_->setPixelColor((rw << 4) + i, (rh << 4) + j,
                                                             QColor(c.r, c.g, c.b, c.a));
                    tips.biome = biome;
                }
            }
        }
    } else {
        //无层，从上往下寻找白名单方块
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                int y = ch->get_height(i, j) + 1;
                while (y >= miny) {
                    auto &tips = region->tips_info_[(rw << 4) + i][(rh << 4) + j];
                    auto biome = ch->get_biome(i, y, j);
                    if (biome != bl::none &&
                        (this->biomes_list_.count(static_cast<int>(biome)) == 0) == this->biome_black_mode_) {
                        auto c = bl::get_biome_color(biome);
                        region->biome_bake_image_->setPixelColor((rw << 4) + i, (rh << 4) + j,
                                                                 QColor(c.r, c.g, c.b, c.a));
                        tips.biome = biome;
                        break;
                    }
                    y--;
                }
            }
        }
    }
}

void MapFilter::bakeChunkActors(bl::chunk *ch, chunk_region *region) const {
    if (!ch)return;
    auto entities = ch->entities();
    for (auto &e: entities) {
        auto key = QString(e->identifier().c_str()).replace("minecraft:", "");
        if ((this->actors_list_.count(key.toStdString()) == 0) == this->actor_black_mode_) {
            region->actors_[ActorImage(key)].push_back(e->pos());
        }
    }
}
