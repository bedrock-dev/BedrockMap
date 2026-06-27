#include "renderfilterdialog.h"

#include <QColor>
#include <cstdint>

#include "color.h"
#include "config.h"
#include "resourcemanager.h"
#include "sub_chunk.h"
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
 * 根据查找情况渲染一个方块的数据
 * @param f  MapFilter 对象
 * @param ch 区块对象
 * @param chx 区块内x坐标
 * @param chz 区块内z坐标
 * @param y   y坐标
 * @param rw  区域坐标w
 * @param rh 区域坐标h
 * @param region  区域数据对象
 */
void setRegionBlockData(const MapFilter *f, bl::chunk *ch, int chx, int chz, int y, int y_solid, int rw, int rh, ChunkRegion *region) {
    if (!ch || !f) return;
    const int X = (rw << 4) + chx;
    const int Z = (rh << 4) + chz;
    auto info = ch->get_block(chx, y, chz);
    auto biome = ch->get_biome(chx, y, chz);
    info.color = bl::blend_color_with_biome(info.name, info.color, biome);

    // 获取固体方块信息（用于 tips 和水体透明）
    bl::block_info solid_info;
    int16_t solid_h = -1;
    if (y_solid >= 0 && y_solid < y) {
        solid_info = ch->get_block(chx, y_solid, chz);
        solid_h = static_cast<int16_t>(y_solid);
    } else {
        solid_info = info;
        solid_h = static_cast<int16_t>(y);
    }

    // 决定写入地形的颜色和高度
    bl::block_info render_info = info;
    int render_y = y;

    if (cfg::TRANSPARENT_WATER && info.name == "minecraft:water" && solid_h >= 0 && solid_h < y) {
        // 保存水面颜色/深度，后续阴影计算完成后叠加
        uint32_t wc = qRgba(info.color.r, info.color.g, info.color.b, info.color.a);

        // 地形先用固体方块的颜色（后续阴影叠加后再叠水）
        render_info = solid_info;
        render_y = solid_h;

        // 写入 tips，供 applyWaterOverlay 使用
        auto &tips = region->tips_info_[X][Z];
        tips.water_surface_color = wc;
        tips.water_depth = static_cast<uint8_t>(y - solid_h);
    }

    // 直接 scanLine + QRgb* 写入，比 setPixelColor 快 10-20x
    reinterpret_cast<QRgb *>(region->terrain_bake_image_.scanLine(Z))[X] =
        qRgba(render_info.color.r, render_info.color.g, render_info.color.b, render_info.color.a);

    if ((f->biomes_list_.count(biome) == 0) == f->biome_black_mode_) {
        // 群系过滤(只是不显示，没有查找功能)
        auto biome_color = bl::get_biome_color(biome);
        reinterpret_cast<QRgb *>(region->biome_bake_image_.scanLine(Z))[X] =
            qRgba(biome_color.r, biome_color.g, biome_color.b, biome_color.a);
    }
    // height 图用原始水面高度（阴影需要平坦水面），tips.height = 水面, solid_height = 海底
    reinterpret_cast<QRgb *>(region->height_bake_image_.scanLine(Z))[X] = height_to_color(y, ch->get_pos().dim).rgba();
    // setup tips
    auto &tips = region->tips_info_[X][Z];
    tips.block_name = render_info.name;
    tips.solid_block_name = solid_info.name;
    tips.biome = biome;
    tips.height = static_cast<int16_t>(y);
    tips.solid_height = solid_h;
}

// 地形，群系渲染以及坐标数据设置
void MapFilter::renderImages(bl::chunk *ch, int rw, int rh, ChunkRegion *region) const {
    if (!ch || !region) return;
    auto [miny, maxy] = ch->get_pos().get_y_range(ch->get_version());
    if (this->enable_layer_) {
        // 选层模式
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
        // 无层：从高度图 O(1) 取 top_y，过滤器循环中顺便记下 solid_y
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                int y = ch->get_height(i, j);
                int solid_y = miny - 1;
                int found_y = miny - 1;
                bool found{false};
                while (y >= miny) {
                    auto b = ch->get_block_fast(i, y, j);

                    // 过滤器匹配
                    if (!found && (this->blocks_list_.count(b.name) == 0) == this->block_black_mode_) {
                        found = true;
                        found_y = y;
                        if (b.name != "minecraft:water") {
                            solid_y = y;  // 非水，地面即固体
                            break;
                        }
                        // 水，继续往下扫固体
                        y--;
                        continue;
                    }

                    // 已匹配到水，继续往下找固体
                    if (found && b.name != "minecraft:air" && b.name != "minecraft:water") {
                        solid_y = y;
                        break;
                    }

                    y--;
                }
                if (found) {
                    setRegionBlockData(this, ch, i, j, found_y, solid_y, rw, rh, region);
                }
            }
        }
    }
}

void MapFilter::bakeChunkActors(bl::chunk *ch, ChunkRegion *region) const {
    if (!ch) return;
    auto entities = ch->entities();
    auto mode = cfg::ACTOR_RENDER_STYLE;
    for (auto &e : entities) {
        auto key = QString(e->identifier().c_str()).replace("minecraft:", "");
        if ((this->actors_list_.count(key.toStdString()) == 0) == this->actor_black_mode_) {
            if (mode == 0) {  // 每个实体都需要渲染
                region->actors_[ActorImage(key)].push_back(e->pos());
            } else {
                auto chunk_pos = ch->get_pos();
                auto &ac = region->actors_counts_[chunk_pos][ActorImage(key)];
                ac = {
                    e->pos(),
                    ac.count + 1,
                };
            }
        }
    }
}
