#include "mapwidget.h"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QDebug>
#include <QDesktopWidget>
#include <QImage>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QRgb>
#include <QVector>
#include <cmath>

#include "color.h"

namespace {

// QImage biomeChunkImage;

}  // namespace
void MapWidget::resizeEvent(QResizeEvent *event) { this->camera = QRect(-10, -10, this->width() + 10, this->height() + 10); }

void MapWidget::asyncRefresh() { this->update(); }

void MapWidget::paintEvent(QPaintEvent *event) {
    QPainter p(this);

    switch (this->layeType) {
        case MapWidget::Biome:
            drawBiome(event, &p);
            break;
        case MapWidget::Terrain:
            drawTerrain(event, &p);
            break;
        case MapWidget::Slime:
            drawSlimeChunks(event, &p);
            break;
        case MapWidget::Height:
            drawHeight(event, &p);
            break;
    }

    if (render_grid) this->drawGrid(event, &p);
    if (render_text) this->drawChunkPos(event, &p);

    this->debugDrawCamera(event, &p);
}

void MapWidget::mouseMoveEvent(QMouseEvent *event) {
    static QPoint lastMove;

    if (event->buttons() & Qt::LeftButton) {
        if (this->dragging) {
            this->origin +=
                QPoint{event->x() - lastMove.x(), event->y() - lastMove.y()};
        } else {
            this->dragging = true;
        }
        lastMove = {event->x(), event->y()};
        this->update();
    } else if (event->buttons() & Qt::RightButton) {
        // todo: Select
    } else {
        auto cursor = this->mapFromGlobal(QCursor::pos());

        auto [mi, ma, render] = this->getRenderRange(this->camera);

        int rx = cursor.x() - render.x();
        int ry = cursor.y() - render.y();

        int x = rx / (this->bw) + mi.get_min_pos().x;
        int y = ry / (this->bw) + mi.get_min_pos().z;
        emit this->mouseMove(x, y);
    }
}

void MapWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        this->dragging = false;
    }
}

void MapWidget::wheelEvent(QWheelEvent *event) {
    auto angle = event->angleDelta().y();
    auto lastBW = this->bw;
    if (angle > 0 && this->bw < 64) {
        this->bw++;
    } else if (angle < 0 && this->bw > 2) {
        this->bw--;
    }

    auto cursor = this->mapFromGlobal(QCursor::pos());
    double ratio = this->bw * 1.0 / lastBW;
    this->origin.setX((this->origin.x() - cursor.x()) * ratio + cursor.x());
    this->origin.setY((this->origin.y() - cursor.y()) * ratio + cursor.y());
    this->update();
}

void MapWidget::debugDrawCamera(QPaintEvent *evet, QPainter *painter) {
    //    QPen pen;  //画笔
    //    pen.setColor(QColor(255, 0, 0));
    //    QBrush brush(QColor(0, 255, 255, 125));  //画刷
    //    painter->setPen(pen);                    //添加画笔
    //    painter->setBrush(brush);                //添加画刷
    //    painter->drawEllipse(this->origin, 10, 10);
    //    painter->drawRect(this->origin.rx(), this->origin.ry(), this->bw * 16, this->bw * 16);
    //    // render range
    //    auto [minChunk, maxChunk, renderRange] = this->getRenderRange(this->camera);
    //    painter->fillRect(renderRange, QBrush(QColor(120, 241, 21, 100)));
    //    painter->drawEllipse(this->mapFromGlobal(QCursor::pos()), 20, 20);

    // debug
    painter->fillRect(QRectF(0, 0, 500, 400), QBrush(QColor(255, 255, 255, 150)));
    QFont font("Arial", 10);
    painter->setFont(font);
    auto dbgInfo = this->world->debug_info();
    int i = 0;
    for (auto &s : dbgInfo) {
        painter->drawText(QPoint(2, i * 40 + 20), s);
        i++;
    }
}

void MapWidget::drawOneChunk(QPaintEvent *event, QPainter *painter, const bl::chunk_pos &pos, const QPoint &start, QImage *q) {
    if (q)
        painter->drawImage(
            QRectF(start.x(), start.y(), 16 * this->bw, 16 * this->bw), *q,
            QRect(0, 0, 16, 16));
}

void MapWidget::forEachChunkInCamera(
    const std::function<void(const bl::chunk_pos &, const QPoint &)> &f) {
    auto [minChunk, maxChunk, renderRange] = this->getRenderRange(this->camera);
    for (int i = minChunk.x; i <= maxChunk.x; i++) {
        for (int j = minChunk.z; j <= maxChunk.z; j++) {
            int x = (i - minChunk.x) * bw * 16 + renderRange.x();
            int y = (j - minChunk.z) * bw * 16 + renderRange.y();
            f({i, j, minChunk.dim}, {x, y});
        }
    }
}

void MapWidget::drawGrid(QPaintEvent *event, QPainter *painter) {
    //细区块边界线
    QPen pen;
    pen.setColor(QColor(250, 250, 250));
    pen.setWidth(1);
    painter->setPen(pen);

    this->forEachChunkInCamera([event, this, painter](const bl::chunk_pos &ch, const QPoint &p) {
        if (this->bw >= 4) painter->drawRect(QRectF(p.x(), p.y(), this->bw * 16, this->bw * 16));
    });

    //粗经纬线
    pen.setColor(QColor(20, 20, 20));
    //根据bw计算几个区块合一起

    pen.setWidth(4);
    painter->setPen(pen);
    auto [minChunk, maxChunk, renderRange] = this->getRenderRange(this->camera);

    auto alignedMinChunkPos = bl::chunk_pos{minChunk.x / 16 * 16, minChunk.z / 16 * 16, 0};
    //纵轴线起始x坐标
    int xStart = (alignedMinChunkPos.x - minChunk.x) * this->bw * 16 + renderRange.x();
    //横轴线起始x坐标
    int yStart = (alignedMinChunkPos.z - minChunk.z) * this->bw * 16 + renderRange.y();

    const int step = 16 * 16 * this->bw;

    for (int i = xStart; i <= renderRange.width() + renderRange.x(); i += step) {
        painter->drawLine(QLine(i, 0, i, this->height()));
    }
    for (int i = yStart; i <= renderRange.height() + renderRange.y(); i += step) {
        painter->drawLine(QLine(0, i, this->width(), i));
    }
}

void MapWidget::drawChunkPos(QPaintEvent *event, QPainter *painter) {
    QFont font("微软雅黑", 8);
    painter->setFont(font);
    QPen pen;
    painter->setPen(pen);
    const int DIG = std::max(1, 32 / this->bw);

    this->forEachChunkInCamera([event, this, painter, DIG, &pen](const bl::chunk_pos &ch, const QPoint &p) {
        if ((ch.x % 16 == 0 && ch.z % 16 == 0) || this->bw >= 8) {
            auto text = QString("%1,%2").arg(QString::number(ch.x), QString::number(ch.z));
            auto rect = QRect{p.x() + 2, p.y() + 2, text.size() * 9, 20};
            painter->fillRect(rect, QBrush(QColor(255, 255, 255, 140)));
            painter->drawText(rect, Qt::AlignCenter, text);
        }
    });
}

void MapWidget::drawSlimeChunks(QPaintEvent *event, QPainter *painter) {
    const int W = this->bw * 16;
    auto img = QImage(W, W, QImage::Format_Indexed8);
    img.setColor(0, qRgba(40, 40, 40, 40));
    img.setColor(1, qRgba(02, 163, 52, 255));

    this->forEachChunkInCamera(
        [event, this, &img, painter](const bl::chunk_pos &ch, const QPoint &p) {
            bool isSlime = ch.is_slime();

            img.fill(isSlime ? 1 : 0);
            this->drawOneChunk(event, painter, ch, p, &img);
        });
}

void MapWidget::drawBiome(QPaintEvent *event, QPainter *painter) {
    this->forEachChunkInCamera([event, this, painter](const bl::chunk_pos &ch, const QPoint &p) {
        auto top = this->world->topBiome(ch);
        this->drawOneChunk(event, painter, ch, p, top);
    });
}

void MapWidget::drawTerrain(QPaintEvent *event, QPainter *p) {}

void MapWidget::drawHeight(QPaintEvent *event, QPainter *p) {}

void MapWidget::gotoBlockPos(int x, int z) {
    int px = this->camera.width() / 2;
    int py = this->camera.height() / 2;
    //坐标换算
    this->origin = {px - x * this->bw, py - z * this->bw};
    this->update();
}

std::tuple<bl::chunk_pos, bl::chunk_pos, QRect> MapWidget::getRenderRange(const QRect &camera) {
    //需要的参数
    // origin  焦点原点在什么地方
    // bw 像素宽度
    const int CHUNK_WIDTH = 16 * this->bw;
    int renderX = (camera.x() - origin.x()) / CHUNK_WIDTH * CHUNK_WIDTH + origin.x();
    int renderY = (camera.y() - origin.y()) / CHUNK_WIDTH * CHUNK_WIDTH + origin.y();
    if (renderX >= camera.x()) renderX -= CHUNK_WIDTH;
    if (renderY >= camera.y()) renderY -= CHUNK_WIDTH;
    int chunk_w = (camera.x() + camera.width() - renderX) / CHUNK_WIDTH;
    if ((camera.x() + camera.width() - renderX) % CHUNK_WIDTH != 0) chunk_w++;
    int chunk_h = (camera.y() + camera.height() - renderY) / CHUNK_WIDTH;
    if ((camera.y() + camera.height() - renderY) % CHUNK_WIDTH != 0) chunk_h++;

    QRect renderRange(renderX, renderY, chunk_w * CHUNK_WIDTH, chunk_h * CHUNK_WIDTH);
    const int dim = static_cast<int>(this->dimType);
    auto minChunk = bl::chunk_pos{(renderX - origin.x()) / CHUNK_WIDTH, (renderY - origin.y()) / CHUNK_WIDTH, dim};
    auto maxChunk = bl::chunk_pos{minChunk.x + chunk_w - 1, minChunk.z + chunk_h - 1, dim};
    return {minChunk, maxChunk, renderRange};
}
