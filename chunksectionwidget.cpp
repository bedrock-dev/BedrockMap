#include "chunksectionwidget.h"

#include <QPainter>
#include <QtDebug>

#include "color.h"

ChunkSectionWidget::ChunkSectionWidget(QWidget *parent) : QWidget(parent) {
    //    this->resize(this->width(), this->width());
}

void ChunkSectionWidget::paintEvent(QPaintEvent *event) {
    if (!this->ch_) return;
    QPainter p(this);
    const int bw = this->get_block_pix();
    const int cw = bw << 4;
    int x_start = (this->width() - cw) >> 1;
    int z_start = 10;
    auto biomes = this->ch_->get_biome_y(this->y_level_);

    QPen pen(QColor(100, 100, 100));
    pen.setColor(QColor(100, 100, 100));

    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            QRect rect(x_start + i * bw, j * bw + z_start, bw, bw);
            auto c = this->ch_->get_block_color(i, this->y_level_, j);
            p.fillRect(rect, QBrush(QColor(c.r, c.g, c.b)));
            p.setPen(pen);
            p.drawRect(rect);
        }
    }

    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            QRect rect(x_start + i * bw, (j + 17) * bw + z_start, bw, bw);
            auto c = bl::get_biome_color(biomes[i][j]);
            p.fillRect(rect, QBrush(QColor(c.r, c.g, c.b)));
            p.setPen(pen);
            p.drawRect(rect);
        }
    }
}

void ChunkSectionWidget::resizeEvent(QResizeEvent *event) { this->update(); }

int ChunkSectionWidget::get_block_pix() {
    const int W = this->width();
    const int H = this->height();
    int edge = std::min(W, H / 2);
    return edge >> 4;
}