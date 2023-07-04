#include "mapwidget.h"

#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QDebug>
#include <QDesktopWidget>
#include <QImage>
#include <QMenu>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QRgb>
#include <QVector>
#include <cmath>

#include "color.h"
#include "config.h"
#include "mainwindow.h"

namespace {

// QImage biomeChunkImage;

}  // namespace
void MapWidget::resizeEvent(QResizeEvent *event) {
    this->camera_ = QRect(-10, -10, this->width() + 10, this->height() + 10);
}

void MapWidget::asyncRefresh() { this->update(); }

void MapWidget::showContextMenu(const QPoint &p) {
    auto area = this->getRenderSelectArea();
    QMenu contextMenu(tr("Context menu"), this);
    if (this->has_selected_ && area.contains(p)) {
        QAction removeChunkAction("删除区块", this);
        QAction clearAreaAction("取消选中", this);
        connect(&clearAreaAction, SIGNAL(triggered()), this, SLOT(clear_select()));
        connect(&removeChunkAction, SIGNAL(triggered()), this, SLOT(delete_chunks()));

        contextMenu.addAction(&removeChunkAction);
        contextMenu.addAction(&clearAreaAction);
        contextMenu.exec(mapToGlobal(p));
    } else {
        auto pos = this->getCursorBlockPos();
        // for single chunk
        auto blockInfo = this->mw_->get_world()->getBlockTips(pos, static_cast<int>(this->dim_type_));
        QAction copyBlockName("Copy block name: " + QString(blockInfo.block_name.c_str()), this);
        QAction copyBiomeName("Copy biome ID: " + QString::number(static_cast<int>(blockInfo.biome)), this);
        QAction copyTpCommand(
                "Copy tp command: " + QString("tp @s %1 ~ %2").arg(QString::number(pos.x), QString::number(pos.z)),
                this);
        QAction openInChunkEditor("Open in chunk editor", this);
        connect(&openInChunkEditor, SIGNAL(triggered()), this, SLOT(openChunkEditor()));
        contextMenu.addAction(&copyBlockName);
        contextMenu.addAction(&copyBiomeName);
        contextMenu.addAction(&openInChunkEditor);
        contextMenu.exec(mapToGlobal(p));
    }
}

void MapWidget::paintEvent(QPaintEvent *event) {
    QPainter p(this);

    switch (this->main_render_type_) {
        case MapWidget::Biome:
            drawBiome(event, &p);
            break;
        case MapWidget::Terrain:
            drawTerrain(event, &p);
            break;
        case MapWidget::Height:
            drawHeight(event, &p);
            break;
    }

    if (draw_actors_)this->drawActors(event, &p);
    if (draw_slime_chunk_)this->drawSlimeChunks(event, &p);
    if (render_grid_) this->drawGrid(event, &p);
    if (render_text_) this->drawChunkPos(event, &p);
    if (render_debug_) this->drawDebugWindow(event, &p);
    this->drawSelectArea(event, &p);
}

void MapWidget::mouseMoveEvent(QMouseEvent *event) {
    static QPoint lastMove;

    if (event->buttons() & Qt::LeftButton) {
        if (this->dragging_) {
            //按住了ctrl
            if (this->CTRL_pressed_) {
                this->select_max_ = this->getCursorBlockPos().to_chunk_pos();
            } else {
                //没有按住就滑动
                this->origin_ += QPoint{event->x() - lastMove.x(), event->y() - lastMove.y()};
            }
        } else {
            //刚开始拖动
            this->dragging_ = true;
            if (this->CTRL_pressed_) {
                this->has_selected_ = true;
                this->select_min_ = this->getCursorBlockPos().to_chunk_pos();
            }
        }
        lastMove = {event->x(), event->y()};
        this->update();

    } else if (event->buttons() & Qt::RightButton) {
        // nothing
        // todo: Select
    } else {
        auto p = this->getCursorBlockPos();
        emit this->mouseMove(p.x, p.z);
    }
}

bl::block_pos MapWidget::getCursorBlockPos() {
    auto cursor = this->mapFromGlobal(QCursor::pos());

    auto [mi, ma, render] = this->getRenderRange(this->camera_);

    int rx = cursor.x() - render.x();
    int ry = cursor.y() - render.y();

    int x = rx / (this->bw_) + mi.get_min_pos().x;
    int y = ry / (this->bw_) + mi.get_min_pos().z;
    return bl::block_pos{x, 0, y};
}


void MapWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        this->dragging_ = false;
    }
}

void MapWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::RightButton) {
        this->showContextMenu(this->mapFromGlobal(QCursor::pos()));
    }
}

void MapWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Control) {
        CTRL_pressed_ = true;
    }
}

void MapWidget::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Control) {
        CTRL_pressed_ = false;
    }
}

void MapWidget::wheelEvent(QWheelEvent *event) {
    auto angle = event->angleDelta().y();
    auto lastBW = this->bw_;
    if (angle > 0 && this->bw_ < 64) {
        this->bw_++;
    } else if (angle < 0 && this->bw_ > 1) {
        this->bw_--;
    }

    auto cursor = this->mapFromGlobal(QCursor::pos());
    double ratio = this->bw_ * 1.0 / lastBW;
    this->origin_.setX((this->origin_.x() - cursor.x()) * ratio + cursor.x());
    this->origin_.setY((this->origin_.y() - cursor.y()) * ratio + cursor.y());
    this->update();
}

void MapWidget::drawDebugWindow(QPaintEvent *event, QPainter *painter) {

    QFont font("Consolas", 8);
    auto dbgInfo = this->mw_->get_world()->debug_info();
    const int font_height = 24;
    const int font_width = 12;

    int max_len = 1;
    for (auto &i: dbgInfo) {
        max_len = std::max(max_len, i.size());
    }
    painter->fillRect(QRectF(0, 0, max_len * font_width + 20, dbgInfo.size() * font_height + 20),
                      QBrush(QColor(255, 255, 255, 150)));
    painter->setFont(font);
    int i = 0;
    for (auto &s: dbgInfo) {
        painter->drawText(QPoint(10, i * font_height + 20), s);
        i++;
    }
}

void MapWidget::drawOneChunk(QPaintEvent *event, QPainter *painter, const bl::chunk_pos &pos, const QPoint &start,
                             QImage *q) const {
    if (q) painter->drawImage(QRectF(start.x(), start.y(), 16 * this->bw_, 16 * this->bw_), *q, QRect(0, 0, 16, 16));
}

void
MapWidget::drawRegion(QPaintEvent *event, QPainter *p, const region_pos &pos, const QPoint &start, QImage *img) const {
    if (img)
        p->drawImage(QRectF(start.x(), start.y(), 16 * this->bw_ * cfg::RW, 16 * this->bw_ * cfg::RW), *img,
                     QRect(0, 0, 16 * cfg::RW, 16 * cfg::RW));
}

void MapWidget::forEachChunkInCamera(const std::function<void(const bl::chunk_pos &, const QPoint &)> &f) {
    auto [minChunk, maxChunk, renderRange] = this->getRenderRange(this->camera_);
    for (int i = minChunk.x; i <= maxChunk.x; i += 1) {
        for (int j = minChunk.z; j <= maxChunk.z; j += 1) {
            int x = (i - minChunk.x) * bw_ * 16 + renderRange.x();
            int y = (j - minChunk.z) * bw_ * 16 + renderRange.y();
            f({i, j, minChunk.dim}, {x, y});
        }
    }
}

void MapWidget::foreachRegionInCamera(const std::function<void(const region_pos &, const QPoint &)> &f) {
    auto [minChunk, maxChunk, renderRange] = this->getRenderRange(this->camera_);

    auto reginMin = cfg::c2r(minChunk);
    auto regionMax = cfg::c2r(maxChunk);

    for (int i = reginMin.x; i <= regionMax.x; i += cfg::RW) {
        for (int j = reginMin.z; j <= regionMax.z; j += cfg::RW) {
            int x = (i - minChunk.x) * bw_ * 16 + renderRange.x();
            int y = (j - minChunk.z) * bw_ * 16 + renderRange.y();
            f({i, j, minChunk.dim}, {x, y});
        }
    }
}

void MapWidget::drawGrid(QPaintEvent *event, QPainter *painter) {
    //细区块边界线
    QPen pen;
    pen.setColor(QColor(200 - cfg::BG_GRAY, 200 - cfg::BG_GRAY, 200 - cfg::BG_GRAY));
    pen.setWidth(1);
    painter->setPen(pen);
    painter->setBrush(QBrush(QColor(0, 0, 0, 0)));
    this->forEachChunkInCamera([event, this, painter](const bl::chunk_pos &ch, const QPoint &p) {
        if (this->bw_ >= 4) painter->drawRect(QRect(p.x(), p.y(), this->bw_ * 16, this->bw_ * 16));
    });

    //粗经纬线
    //根据bw计算几个区块合一起

    pen.setWidth(4);
    painter->setPen(pen);
    auto [minChunk, maxChunk, renderRange] = this->getRenderRange(this->camera_);

    auto alignedMinChunkPos = bl::chunk_pos{minChunk.x / 16 * 16, minChunk.z / 16 * 16, 0};
    //纵轴线起始x坐标
    int xStart = (alignedMinChunkPos.x - minChunk.x) * this->bw_ * 16 + renderRange.x();
    //横轴线起始x坐标
    int yStart = (alignedMinChunkPos.z - minChunk.z) * this->bw_ * 16 + renderRange.y();

    const int step = 16 * 16 * this->bw_;

    for (int i = xStart; i <= renderRange.width() + renderRange.x(); i += step) {
        painter->drawLine(QLine(i, 0, i, this->height()));
    }
    for (int i = yStart; i <= renderRange.height() + renderRange.y(); i += step) {
        painter->drawLine(QLine(0, i, this->width(), i));
    }
}

void MapWidget::drawSelectArea(QPaintEvent *event, QPainter *p) {
    if (!this->has_selected_) return;
    p->fillRect(getRenderSelectArea(), QBrush(QColor(135, 206, 235, 200)));
}

QRect MapWidget::getRenderSelectArea() {
    auto [minChunk, maxChunk, renderRange] = this->getRenderRange(this->camera_);

    auto minX = std::min(this->select_min_.x, this->select_max_.x);
    auto minZ = std::min(this->select_min_.z, this->select_max_.z);

    auto maxX = std::max(this->select_min_.x, this->select_max_.x);
    auto maxZ = std::max(this->select_min_.z, this->select_max_.z);

    QRect x((minX - minChunk.x) * bw_ * 16 + renderRange.x(), (minZ - minChunk.z) * bw_ * 16 + renderRange.y(),
            (maxX - minX + 1) * bw_ * 16, (maxZ - minZ + 1) * bw_ * 16);
    return x;
}

void MapWidget::drawChunkPos(QPaintEvent *event, QPainter *painter) {
    QFont font("Consolas", 10);
    painter->setFont(font);
    QPen pen;
    painter->setPen(pen);
    const int DIG = std::max(1, 32 / this->bw_);

    this->forEachChunkInCamera([event, this, painter, DIG, &pen](const bl::chunk_pos &ch, const QPoint &p) {
        if ((ch.x % 16 == 0 && ch.z % 16 == 0) || this->bw_ >= 8) {
            auto text = QString("%1,%2").arg(QString::number(ch.x), QString::number(ch.z));
            auto rect = QRect{p.x() + 2, p.y() + 2, text.size() * 16, 40};
            painter->fillRect(rect, QBrush(QColor(255, 255, 255, 140)));
            painter->drawText(rect, Qt::AlignCenter, text);
        }
    });
}

void MapWidget::drawSlimeChunks(QPaintEvent *event, QPainter *painter) {
    this->forEachChunkInCamera([event, this, painter](const bl::chunk_pos &ch, const QPoint &p) {

        this->drawOneChunk(event, painter, ch, p, this->mw_->get_world()->slimeChunk(ch));
    });
}

void MapWidget::drawBiome(QPaintEvent *event, QPainter *painter) {
    this->foreachRegionInCamera([event, this, painter](const region_pos &ch, const QPoint &p) {
        auto top = this->mw_->get_world()->topBiome(ch);
        this->drawRegion(event, painter, ch, p, top);
    });
}

void MapWidget::drawTerrain(QPaintEvent *event, QPainter *painter) {
    this->foreachRegionInCamera([event, this, painter](const bl::chunk_pos &ch, const QPoint &p) {
        auto height = this->mw_->get_world()->topBlock(ch);
        this->drawRegion(event, painter, ch, p, height);
    });

}

void MapWidget::drawActors(QPaintEvent *event, QPainter *painter) {
    QPen pen(QColor(20, 20, 20));
    painter->setBrush(QBrush(QColor(255, 10, 10)));
    this->foreachRegionInCamera([event, this, painter, &pen](const bl::chunk_pos &ch, const QPoint &p) {
        auto actors = this->mw_->get_world()->getActorList(ch);
        for (auto &actor: actors) {
            float x = (actor.x - (float) ch.x * 16.0f) * (float) this->bw_ + (float) p.x();
            float y = (actor.z - (float) ch.z * 16.0f) * (float) this->bw_ + (float) p.y();
            painter->drawEllipse(QRectF(x - 3.0, y - 3.0, 6.0, 6.0));
        }
    });
}


void MapWidget::drawHeight(QPaintEvent *event, QPainter *painter) {
    this->forEachChunkInCamera([event, this, painter](const bl::chunk_pos &ch, const QPoint &p) {
        auto height = this->mw_->get_world()->height(ch);
        this->drawOneChunk(event, painter, ch, p, height);
    });
}

void MapWidget::gotoBlockPos(int x, int z) {
    int px = this->camera_.width() / 2;
    int py = this->camera_.height() / 2;
    //坐标换算
    this->origin_ = {px - x * this->bw_, py - z * this->bw_};
    this->update();
}

void MapWidget::openChunkEditor() {
    auto cp = this->getCursorBlockPos().to_chunk_pos();
    cp.dim = static_cast<int>(this->dim_type_);
    return this->mw_->openChunkEditor(cp);
}

std::tuple<bl::chunk_pos, bl::chunk_pos, QRect> MapWidget::getRenderRange(const QRect &camera) {
    //需要的参数
    // origin  焦点原点在什么地方
    // bw 像素宽度
    const int CHUNK_WIDTH = 16 * this->bw_;
    int renderX = (camera.x() - origin_.x()) / CHUNK_WIDTH * CHUNK_WIDTH + origin_.x();
    int renderY = (camera.y() - origin_.y()) / CHUNK_WIDTH * CHUNK_WIDTH + origin_.y();
    if (renderX >= camera.x()) renderX -= CHUNK_WIDTH;
    if (renderY >= camera.y()) renderY -= CHUNK_WIDTH;
    int chunk_w = (camera.x() + camera.width() - renderX) / CHUNK_WIDTH;
    if ((camera.x() + camera.width() - renderX) % CHUNK_WIDTH != 0) chunk_w++;
    int chunk_h = (camera.y() + camera.height() - renderY) / CHUNK_WIDTH;
    if ((camera.y() + camera.height() - renderY) % CHUNK_WIDTH != 0) chunk_h++;

    QRect renderRange(renderX, renderY, chunk_w * CHUNK_WIDTH, chunk_h * CHUNK_WIDTH);
    const int dim = static_cast<int>(this->dim_type_);
    auto minChunk = bl::chunk_pos{(renderX - origin_.x()) / CHUNK_WIDTH, (renderY - origin_.y()) / CHUNK_WIDTH, dim};
    auto maxChunk = bl::chunk_pos{minChunk.x + chunk_w - 1, minChunk.z + chunk_h - 1, dim};
    return {minChunk, maxChunk, renderRange};
}

void MapWidget::saveImage(bool full_screen) {
    QMessageBox::information(nullptr, "警告", "开发中", QMessageBox::Yes, QMessageBox::Yes);
}

void MapWidget::delete_chunks() {
    this->select_max_.dim = this->dim_type_;
    this->select_min_.dim = this->dim_type_;
    this->mw_->deleteChunks(this->select_min_, this->select_max_);

}



