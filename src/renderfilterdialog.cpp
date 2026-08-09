#include "renderfilterdialog.h"

#include <QColor>
#include <cstdint>

#include "color.h"
#include "config.h"
#include "resourcemanager.h"
#include "sub_chunk.h"
#include "ui_renderfilterdialog.h"

RenderFilterDialog::RenderFilterDialog(QWidget *parent) : QDialog(parent), ui(new Ui::RenderFilterDialog) {
    ui->setupUi(this);
    this->setWindowTitle(tr("renderFilterDialog.title.filter"));
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

/**
 * Render a block's data based on the filter lookup result
 * @param f  MapFilter object
 * @param ch chunk object
 * @param chx x coordinate inside the chunk
 * @param chz z coordinate inside the chunk
 * @param y   y coordinate
 * @param rw  region width
 * @param rh  region height
 * @param region  region data object
 */
void setRegionBlockData(const MapFilter *f, bl::chunk *ch, int chx, int chz, int y, int y_solid, int rw, int rh, ChunkRegion *region) {
    if (!ch || !f) return;
    const int X = (rw << 4) + chx;
    const int Z = (rh << 4) + chz;
    auto info = ch->get_block(chx, y, chz);
    auto biome = ch->get_biome(chx, y, chz);
    info.color = bl::blend_color_with_biome(info.name, info.color, biome);

    bl::block_info solid_info;
    int16_t solid_h = -1;
    if (y_solid >= 0 && y_solid < y) {
        solid_info = ch->get_block(chx, y_solid, chz);
        solid_h = static_cast<int16_t>(y_solid);
    } else {
        solid_info = info;
        solid_h = static_cast<int16_t>(y);
    }

    bl::block_info render_info = info;
    int render_y = y;

    if (setting::TRANSPARENT_WATER && info.name == "minecraft:water" && solid_h >= 0 && solid_h < y) {
        uint32_t wc = qRgba(info.color.r, info.color.g, info.color.b, info.color.a);
        render_info = solid_info;
        render_y = solid_h;
        auto &tips = region->tips_info_[X][Z];
        tips.water_surface_color = wc;
    }

    reinterpret_cast<QRgb *>(region->terrain_bake_image_.scanLine(Z))[X] =
        qRgba(render_info.color.r, render_info.color.g, render_info.color.b, render_info.color.a);

    if ((f->biomes_list_.count(biome) == 0) == f->biome_black_mode_) {
        auto biome_color = bl::get_biome_color(biome);
        reinterpret_cast<QRgb *>(region->biome_bake_image_.scanLine(Z))[X] =
            qRgba(biome_color.r, biome_color.g, biome_color.b, biome_color.a);
    }

    auto &tips = region->tips_info_[X][Z];
    tips.block_name = render_info.name;
    tips.solid_block_name = solid_info.name;
    // get_top_biome is robust when the exact surface layer stores none
    tips.biome = ch->get_top_biome(chx, chz);
    tips.height = static_cast<int16_t>(y);
    tips.solid_height = solid_h;
}

// terrain/biome rendering and coordinate data setup
void MapFilter::renderImages(bl::chunk *ch, int rw, int rh, ChunkRegion *region) const {
    if (!ch || !region) return;
    auto [miny, maxy] = ch->get_pos().get_y_range(ch->get_version());
    if (this->enable_layer_) {
        // layer selection mode
        if (this->layer > maxy || this->layer < miny) return;
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                auto b = ch->get_block_fast(i, this->layer, j);
                if ((this->blocks_list_.count(b.name) == 0) == this->block_black_mode_) {
                    setRegionBlockData(this, ch, i, j, this->layer, -1, rw, rh, region);
                }
            }
        }
    } else {
        // get_height() narrows the scan range, get_top_y skips unknown/air internally
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                auto [top_y, solid_y] = ch->get_top_y(i, j, ch->get_height(i, j));
                if (top_y < miny) continue;
                setRegionBlockData(this, ch, i, j, top_y, solid_y, rw, rh, region);
            }
        }
    }
}
