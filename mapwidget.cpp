#include "mapwidget.h"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QDebug>
#include <QDesktopWidget>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>

MapWidget::MapWidget(QWidget *parent) : QWidget(parent) {
    //初始化为正中央
}

void MapWidget::resizeEvent(QResizeEvent *event) {
    this->camera = QRect(20, 20, this->width() - 20, this->height() - 20);
}

void MapWidget::paintEvent(QPaintEvent *event) { this->debugDrawCamera(event); }

void MapWidget::mouseMoveEvent(QMouseEvent *event) {
    static QPoint lastMove;
    if (event->buttons() & Qt::LeftButton) {
        if (this->dragging) {
            this->origin +=
                QPoint{event->x() - lastMove.x(), event->y() - lastMove.y()};
            this->update();
        } else {
            this->dragging = true;
        }
        lastMove = {event->x(), event->y()};
    }
}

void MapWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        this->dragging = false;
    }
}

void MapWidget::wheelEvent(QWheelEvent *event) {
    auto angle = event->angleDelta().y();
    qDebug() << "angle is" << angle;  // 120
    auto last = bw;

    if (angle > 0 && this->bw < 80) {
        this->bw++;
    } else if (angle < 0 && this->bw > 2) {
        this->bw--;
    }

    // focus on cursor
    this->update();
}

void MapWidget::debugDrawCamera(QPaintEvent *evet) {
    QPainter painter(this);
    QPen pen;  //画笔
    pen.setColor(QColor(255, 0, 0));
    QBrush brush(QColor(0, 255, 255, 125));  //画刷
    painter.setPen(pen);                   //添加画笔
    painter.setBrush(brush);               //添加画刷

    painter.drawEllipse(this->origin, 10, 10);

    //    painter.drawRect(0, 0, width() - 10, height() - 10);

    painter.drawRect(this->origin.rx(), this->origin.ry(), this->bw * 16,
                     this->bw * 16);

    // render range
    auto [minChunk, maxChunk, renderRange] = this->getRenderRange(this->camera);

    pen.setColor(QColor(120, 0, 223));
    painter.setPen(pen);
    painter.drawRect(camera);
    pen.setColor(QColor(255, 255, 255));
    painter.setPen(pen);
    //    painter.drawRect(renderRange);
    //    //Grid
    QPen pen2;
    pen2.setBrush(QBrush(QColor(0, 0, 0, 255)));

    pen2.setWidth(4);
    painter.setPen(pen2);
    for (int i = minChunk.x; i <= maxChunk.x; i++) {
        for (int j = minChunk.z; j <= maxChunk.z; j++) {
            int x = (i - minChunk.x) * bw * 16 + renderRange.x();
            int y = (j - minChunk.z) * bw * 16 + renderRange.y();
            painter.drawRect(x, y, bw * 16, bw * 16);
            painter.drawText(
                x + bw, y + bw * 8,
                QString("[%1,%2]").arg(QString::number(i), QString::number(j)));
        }
    }
}

void MapWidget::debugDrawGrid(QPaintEvent *event) {}

std::tuple<bl::chunk_pos, bl::chunk_pos, QRect> MapWidget::getRenderRange(
    const QRect &camera) {
    //需要的参数
    // origin  焦点原点在什么地方
    // bw 像素宽度
    const int CHUNK_WIDTH = 16 * this->bw;
    int renderX =
        (camera.x() - origin.x()) / CHUNK_WIDTH * CHUNK_WIDTH + origin.x();
    int renderY =
        (camera.y() - origin.y()) / CHUNK_WIDTH * CHUNK_WIDTH + origin.y();
    if (renderX >= camera.x()) renderX -= CHUNK_WIDTH;
    if (renderY >= camera.y()) renderY -= CHUNK_WIDTH;

    int chunk_w = (camera.x() + camera.width() - renderX) / CHUNK_WIDTH;
    if ((camera.x() + camera.width() - renderX) % CHUNK_WIDTH != 0) chunk_w++;

    int chunk_h = (camera.y() + camera.height() - renderY) / CHUNK_WIDTH;
    if ((camera.y() + camera.height() - renderY) % CHUNK_WIDTH != 0) chunk_h++;

    QRect renderRange(

        renderX, renderY, chunk_w * CHUNK_WIDTH, chunk_h * CHUNK_WIDTH

    );
    auto minChunk = bl::chunk_pos{(renderX - origin.x()) / CHUNK_WIDTH,
                                  (renderY - origin.y()) / CHUNK_WIDTH};

    auto maxChunk =
        bl::chunk_pos{minChunk.x + chunk_w - 1, minChunk.z + chunk_h - 1};

    return {minChunk, maxChunk, renderRange};
}
