#include "chunksectionwidget.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

#include "color.h"
#include "loguru/loguru.hpp"

namespace {
    std::string getDisplayedPalette(const std::string& value) {
        std::string res;
        for (auto c : value) {
            if (c != '\n' && c != ' ' && c != '\r') {
                res.push_back(c);
            }
        }

        if (res.size() > 32) {
            res = res.substr(0, 32) + "...";
        }
        return res;
    }
}  // namespace

ChunkSectionWidget::ChunkSectionWidget(QWidget* parent) : QWidget(parent) {}

void ChunkSectionWidget::paintEvent(QPaintEvent* event) {
    QPainter p(this);

    const int bw = this->get_block_pix();
    const int cw = bw << 4;
    int x_start = (this->width() - cw) >> 1;
    int z_start = 10;
    QPen pen(QColor(100, 100, 100));
    pen.setColor(QColor(100, 100, 100));
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            QRect rect(x_start + i * bw, j * bw + z_start, bw, bw);
            const auto& data = this->get_layer_data(this->y_level_)[i][j];
            if (this->layer_ >= static_cast<int>(data.layers.size())) continue;
            const auto& layer = data.layers[this->layer_];
            if (layer.block_name == "minecraft:air") continue;
            auto c = layer.block_color;
            p.fillRect(rect, QBrush(QColor(c.r, c.g, c.b)));
            p.setPen(pen);
            if (draw_grid_) p.drawRect(rect);

            // paint info
        }
    }

    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            QRect rect(x_start + i * bw, (j + 17) * bw + z_start, bw, bw);
            auto c = bl::get_biome_color(this->get_layer_data(this->y_level_)[i][j].biome);
            p.fillRect(rect, QBrush(QColor(c.r, c.g, c.b)));
            p.setPen(pen);
            if (draw_grid_) p.drawRect(rect);
        }
    }
}

void ChunkSectionWidget::resizeEvent(QResizeEvent* event) { this->update(); }

int ChunkSectionWidget::get_block_pix() {
    const int W = this->width();
    const int H = this->height();
    int edge = std::min(W, H / 2);
    return edge >> 4;
}

void ChunkSectionWidget::load_data(bl::chunk* ch) {
    LOG_F(INFO, "Chunk Section load data begin");
    if (!ch) {
        LOG_F(WARNING, "Invalid Chunk");
        return;
    }
    auto [miny_y, max_y] = ch->get_y_range();
    for (int y = miny_y; y <= max_y; y++) {
        for (int x = 0; x < 16; x++) {
            for (int z = 0; z < 16; z++) {
                auto& data = this->get_layer_data(y)[x][z];
                data.layers.clear();
                data.biome = ch->get_biome(x, y, z);
                // collect every block layer (0..n); stop when the layer index runs out
                for (int layer = 0;; layer++) {
                    auto* raw = ch->get_block_raw(x, y, z, layer);
                    if (!raw) break;
                    auto info = ch->get_block_with_color(x, y, z, layer);
                    TerrainLayerData layer_data;
                    layer_data.block_name = info.name;
                    layer_data.block_color = bl::blend_color_with_biome(layer_data.block_name, info.color, data.biome);
                    layer_data.block_palette = raw->to_readable_string();
                    data.layers.push_back(std::move(layer_data));
                }
            }
        }
    }
    LOG_F(INFO, "Chunk Section load data finished");
}

void ChunkSectionWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        this->showContextMenu(this->mapFromGlobal(QCursor::pos()));
    }
}

void ChunkSectionWidget::showContextMenu(const QPoint& p) {
    const int bw = this->get_block_pix();
    const int cw = bw << 4;
    int x_start = (this->width() - cw) >> 1;
    int z_start = 10;
    QRect terrain_view_rect(x_start, z_start, bw * 16, bw * 16);
    QRect biome_view_rect(x_start, z_start + bw * 17, bw * 16, bw * 16);

    if (!terrain_view_rect.contains(p) && !biome_view_rect.contains(p)) return;

    QMenu contextMenu(this);
    auto rx = (p.x() - x_start) / bw;
    auto rz = ((p.y() - z_start) / bw) % 17;
    auto& data = this->get_layer_data(this->y_level_)[rx][rz];
    auto posString = QString("%1,%2,%3").arg(QString::number(rx), QString::number(this->y_level_), QString::number(rz));
    auto biomeString = QString("%1 (%2)").arg(data.biome).arg(bl::get_biome_name(data.biome).c_str());

    bool hasLayer = this->layer_ < static_cast<int>(data.layers.size());
    auto blockNameString = QString(hasLayer ? data.layers[this->layer_].block_name.c_str() : "minecraft:air");
    auto paletteString = QString(hasLayer ? data.layers[this->layer_].block_palette.c_str() : "");

    QAction posAction(tr("chunkSectionWidget.tooltip.position") + posString, this);
    QAction blockNameAction(tr("chunkSectionWidget.tooltip.blockName") + blockNameString, this);
    QAction blockPaletteAction(tr("chunkSectionWidget.tooltip.palette") + QString(getDisplayedPalette(paletteString.toStdString()).c_str()),
                               this);
    QAction biomeAction(tr("chunkSectionWidget.tooltip.biome") + biomeString, this);

    auto* cb = QApplication::clipboard();

    connect(&posAction, &QAction::triggered, this, [cb, &posString] { cb->setText(posString); });
    connect(&blockNameAction, &QAction::triggered, this, [cb, &blockNameString] { cb->setText(blockNameString); });
    connect(&blockPaletteAction, &QAction::triggered, this, [cb, &paletteString] { cb->setText(paletteString); });
    connect(&biomeAction, &QAction::triggered, this, [cb, &biomeString] { cb->setText(biomeString); });

    contextMenu.addAction(&posAction);
    contextMenu.addAction(&blockNameAction);
    contextMenu.addAction(&blockPaletteAction);
    contextMenu.addAction(&biomeAction);

    contextMenu.exec(mapToGlobal(p));
}
