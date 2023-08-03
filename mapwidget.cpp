#include "mapwidget.h"
#include <QFileDialog>
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
#include  <QClipboard>
#include <cmath>
#include <QFontMetrics>
#include  <QInputDialog>
#include "color.h"
#include "config.h"
#include "mainwindow.h"
#include <QFormLayout>
#include <QDialogButtonBox>

void MapWidget::resizeEvent(QResizeEvent *event) {
    this->camera_ = QRect(-10, -10, this->width() + 10, this->height() + 10);
}

void MapWidget::asyncRefresh() { this->update(); }

//显示右键菜单
void MapWidget::showContextMenu(const QPoint &p) {
    auto *cb = QApplication::clipboard();
    auto area = this->getRenderSelectArea();
    QMenu contextMenu(this);
    if (this->has_selected_ && area.contains(p)) {
        QAction removeChunkAction("删除区块", this);
        QAction clearAreaAction("取消选中", this);
        QAction screenShotAction("另存为图像", this);
        connect(&clearAreaAction, &QAction::triggered, this, [this] { this->has_selected_ = false; });
        connect(&removeChunkAction, SIGNAL(triggered()), this, SLOT(delete_chunks()));
        connect(&screenShotAction, &QAction::triggered, this, [this] { this->saveImageAction(false); });

        contextMenu.addAction(&clearAreaAction);
        contextMenu.addAction(&screenShotAction);
        contextMenu.addAction(&removeChunkAction);
        contextMenu.exec(mapToGlobal(p));
    } else {
        auto pos = this->getCursorBlockPos();
        // for single chunk
        QAction gotoAction("前往坐标", this);
        connect(&gotoAction, &QAction::triggered, this, [this] { this->gotoPositionAction(); });
        auto blockInfo = this->mw_->levelLoader()->getBlockTips(pos, this->dim_type_);
        //block name
        QAction copyBlockNameAction("复制方块名称: " + QString(blockInfo.block_name.c_str()), this);
        connect(&copyBlockNameAction, &QAction::triggered, this, [cb, &blockInfo] {
            cb->setText(blockInfo.block_name.c_str());
        });

        //biome
        QAction copyBiomeAction(("复制群系名称: " + bl::get_biome_name(blockInfo.biome)).c_str(), this);
        connect(&copyBiomeAction, &QAction::triggered, this, [cb, &blockInfo] {
            cb->setText(bl::get_biome_name(blockInfo.biome).c_str());
        });

        //height
        QAction copyHeightAction("复制高度信息: " + QString::number(blockInfo.height), this);
        connect(&copyHeightAction, &QAction::triggered, this, [cb, &blockInfo] {
            cb->setText(QString::number(blockInfo.height));
        });
        auto tpCmd = QString("tp @s %1 ~ %2").arg(QString::number(pos.x), QString::number(pos.z));
        QAction copyTpCommandAction("复制TP命令: " + tpCmd, this);
        connect(&copyTpCommandAction, &QAction::triggered, this, [cb, &tpCmd] {
            cb->setText(tpCmd);
        });

        QAction screenShotAction("另存为图像", this);
        connect(&screenShotAction, &QAction::triggered, this, [this] { this->saveImageAction(true); });

        //chunk editor
        QAction openInChunkEditor("在区块编辑器中打开", this);
        connect(&openInChunkEditor, &QAction::triggered, this, [pos, this] {
            auto cp = pos.to_chunk_pos();
            cp.dim = static_cast<int>(this->dim_type_);
            this->selectChunk(cp);
            this->mw_->openChunkEditor(cp);
        });

        contextMenu.addAction(&gotoAction);
        contextMenu.addAction(&copyBlockNameAction);
        contextMenu.addAction(&copyBiomeAction);
        contextMenu.addAction(&copyHeightAction);
        contextMenu.addAction(&copyTpCommandAction);
        contextMenu.addAction(&screenShotAction);
        contextMenu.addAction(&openInChunkEditor);
        contextMenu.exec(mapToGlobal(p));
    }
}

bl::block_pos MapWidget::getCursorBlockPos() {
    auto cursor = this->mapFromGlobal(QCursor::pos());
    auto [mi, ma, render] = this->getRenderRange(this->camera_);
    int rx = cursor.x() - render.x();
    int ry = cursor.y() - render.y();
    int x = static_cast<int>( rx / (this->BW())) + mi.get_min_pos(bl::ChunkVersion::New).x;
    int y = static_cast<int>(ry / (this->BW())) + mi.get_min_pos(bl::ChunkVersion::New).z;
    return bl::block_pos{x, 0, y};
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
    if (draw_HSA_) this->drawHSAs(event, &p);
    if (draw_villages_) this->drawVillages(event, &p);
    if (draw_actors_) this->drawActors(event, &p);
    if (draw_slime_chunk_)this->drawSlimeChunks(event, &p);
    if (draw_grid_) this->drawGrid(event, &p);
    if (draw_coords_) this->drawChunkPosText(event, &p);
    if (draw_debug_window_) this->drawDebugWindow(event, &p);
    this->drawSelectArea(event, &p);
    this->drawMarkers(event, &p);
}

void MapWidget::mouseMoveEvent(QMouseEvent *event) {
    static QPoint lastMove;
    if (event->buttons() & Qt::LeftButton) {
        if (this->dragging_) {
            this->origin_ += QPoint{event->x() - lastMove.x(), event->y() - lastMove.y()};
            this->update();
        } else {
            this->dragging_ = true;
        }
        lastMove = {event->x(), event->y()};
    } else if (event->buttons() & Qt::MiddleButton) {
        if (!selecting_) {
            this->select_pos_1_ = getCursorBlockPos().to_chunk_pos();
            this->selecting_ = true;
            this->has_selected_ = true;
        } else {
            this->select_pos_2_ = getCursorBlockPos().to_chunk_pos();
        }
    } else if (event->buttons() & Qt::RightButton) {
        //pass
    } else {
        auto p = this->getCursorBlockPos();
        emit this->mouseMove(p.x, p.z);
    }
}


void MapWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        this->dragging_ = false;
    } else if (event->button() == Qt::MiddleButton) {
        this->selecting_ = false;
        qDebug() << "Selection: " << this->select_pos_1_.to_string().c_str() << " ~~ "
                 << this->select_pos_2_.to_string().c_str();
    } else if (event->button() == Qt::RightButton) {
        this->showContextMenu(this->mapFromGlobal(QCursor::pos()));
    }
}


void MapWidget::wheelEvent(QWheelEvent *event) {
    auto angle = event->angleDelta().y();

    auto lastCW = this->cw_;
    if (angle > 0) {
        auto ncw = static_cast<int>( static_cast<qreal>(this->cw_) * cfg::ZOOM_SPEED);
        if (ncw == this->cw_) ncw = cw_ + 1;
        if (ncw > cfg::MAXIMUM_SCALE_LEVEL) ncw = cfg::MAXIMUM_SCALE_LEVEL;
        this->cw_ = ncw;
    } else if (angle < 0) {
        auto ncw = static_cast<int>(static_cast<qreal>(this->cw_) / cfg::ZOOM_SPEED);
        if (ncw == this->cw_) ncw = cw_ - 1;
        if (ncw < cfg::MINIMUM_SCALE_LEVEL) ncw = cfg::MINIMUM_SCALE_LEVEL;
        this->cw_ = ncw;
    }

    auto cursor = this->mapFromGlobal(QCursor::pos());
    double ratio = this->cw_ * 1.0 / lastCW;
    this->origin_.setX(static_cast<int>((this->origin_.x() - cursor.x()) * ratio + cursor.x()));
    this->origin_.setY(static_cast<int>((this->origin_.y() - cursor.y()) * ratio + cursor.y()));
    this->update();
}


void
MapWidget::drawRegion(QPaintEvent *e, QPainter *p, const region_pos &pos, const QPoint &start, QImage *img) const {
    if (img)
        p->drawImage(QRectF(start.x(), start.y(), this->cw_ * cfg::RW, this->cw_ * cfg::RW), *img,
                     QRect(0, 0, cfg::RW << 4, cfg::RW << 4));
}

void MapWidget::forEachChunkInCamera(const std::function<void(const bl::chunk_pos &, const QPoint &)> &f) {
    auto [minChunk, maxChunk, renderRange] = this->getRenderRange(this->camera_);
    for (int i = minChunk.x; i <= maxChunk.x; i += 1) {
        for (int j = minChunk.z; j <= maxChunk.z; j += 1) {
            int x = (i - minChunk.x) * cw_ + renderRange.x();
            int y = (j - minChunk.z) * cw_ + renderRange.y();
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
            int x = (i - minChunk.x) * cw_ + renderRange.x();
            int y = (j - minChunk.z) * cw_ + renderRange.y();
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
        if (this->cw_ >= 64) painter->drawRect(QRect(p.x(), p.y(), this->cw_, this->cw_));
    });

    //粗经纬线
    //根据bw计算几个区块合一起
    pen.setWidth(3);
    painter->setPen(pen);
    auto [minChunk, maxChunk, renderRange] = this->getRenderRange(this->camera_);


    auto alignedMinChunkPos = bl::chunk_pos{minChunk.x / cfg::GRID_WIDTH * cfg::GRID_WIDTH,
                                            minChunk.z / cfg::GRID_WIDTH * cfg::GRID_WIDTH, 0};

    //纵轴线起始x坐标
    int xStart = (alignedMinChunkPos.x - minChunk.x) * this->cw_ + renderRange.x();
    //横轴线起始x坐标
    int yStart = (alignedMinChunkPos.z - minChunk.z) * this->cw_ + renderRange.y();

    const int step = cfg::GRID_WIDTH * this->cw_;

    for (int i = xStart; i <= renderRange.width() + renderRange.x(); i += step) {
        painter->drawLine(QLine(i, 0, i, this->height()));
    }
    for (int i = yStart; i <= renderRange.height() + renderRange.y(); i += step) {
        painter->drawLine(QLine(0, i, this->width(), i));
    }
}

void MapWidget::drawSelectArea(QPaintEvent *event, QPainter *p) {
    if (!this->has_selected_) return;
    p->setPen(QPen(QColor(218, 255, 251), 6, Qt::DashLine));
    p->setBrush(QBrush(QColor(218, 255, 251, 100)));
    p->drawRect(getRenderSelectArea());
}

QRect MapWidget::getRenderSelectArea() {
    auto [minChunk, maxChunk, renderRange] = this->getRenderRange(this->camera_);

    auto minX = std::min(this->select_pos_1_.x, this->select_pos_2_.x);
    auto minZ = std::min(this->select_pos_1_.z, this->select_pos_2_.z);

    auto maxX = std::max(this->select_pos_1_.x, this->select_pos_2_.x);
    auto maxZ = std::max(this->select_pos_1_.z, this->select_pos_2_.z);

    QRect x((minX - minChunk.x) * cw_ + renderRange.x(), (minZ - minChunk.z) * cw_ + renderRange.y(),
            (maxX - minX + 1) * cw_, (maxZ - minZ + 1) * cw_);
    return x;
}

void MapWidget::drawChunkPosText(QPaintEvent *event, QPainter *painter) {
    QFont font("JetBrains Mono", 8);
    QFontMetrics fm(font);
    painter->setFont(font);
    QPen pen(QColor(255, 255, 255));
    painter->setPen(pen);

    this->forEachChunkInCamera([event, this, painter, &fm, &pen](const bl::chunk_pos &ch, const QPoint &p) {
        if ((ch.x % cfg::GRID_WIDTH == 0 && ch.z % cfg::GRID_WIDTH == 0) || this->cw_ >= 256) {
            auto text = QString("%1,%2").arg(QString::number(ch.x), QString::number(ch.z));
            auto rect = QRect{p.x() + 2, p.y() + 2, fm.width(text) + 4, fm.height() + 4};
            painter->fillRect(rect, QBrush(QColor(22, 22, 22, 90)));
            painter->drawText(rect, Qt::AlignCenter, text);
        }
    });
}

void MapWidget::drawDebugWindow(QPaintEvent *event, QPainter *painter) {
    QFont font("JetBrains Mono", 6);
    QFontMetrics fm(font);
    auto dbgInfo = this->mw_->levelLoader()->debugInfo();
    int max_len = 1;
    for (auto &i: dbgInfo) {
        max_len = std::max(max_len, fm.width(i));
    }
    painter->fillRect(QRectF(0, 0, max_len + 10, static_cast<qreal>(fm.height() * (dbgInfo.size() + 1))),
                      QBrush(QColor(22, 22, 22, 90)));
    painter->setFont(font);
    painter->setPen(QPen(QColor(255, 255, 255)));
    int i = 0;
    for (auto &s: dbgInfo) {
        painter->drawText(QPoint(5, (i + 1) * fm.height()), s);
        i++;
    }
}

/**
 * 重写这里，采用缓存机制
 * @param event
 * @param painter
 */
void MapWidget::drawSlimeChunks(QPaintEvent *event, QPainter *painter) {
    this->foreachRegionInCamera([event, this, painter](const region_pos &rp, const QPoint &p) {
        auto top = this->mw_->levelLoader()->bakedSlimeChunkImage(rp);
        this->drawRegion(event, painter, rp, p, top);
    });
}

void MapWidget::drawBiome(QPaintEvent *event, QPainter *painter) {
    this->foreachRegionInCamera([event, this, painter](const region_pos &rp, const QPoint &p) {
        auto top = this->mw_->levelLoader()->bakedBiomeImage(rp);
        this->drawRegion(event, painter, rp, p, top);
    });

}

void MapWidget::drawTerrain(QPaintEvent *event, QPainter *painter) {
    this->foreachRegionInCamera([event, this, painter](const bl::chunk_pos &rp, const QPoint &p) {
        auto height = this->mw_->levelLoader()->bakedTerrainImage(rp);
        this->drawRegion(event, painter, rp, p, height);
    });
}


void MapWidget::drawVillages(QPaintEvent *event, QPainter *p) {
    auto &vs = this->mw_->get_villages();
    auto c = this->getRenderRange(this->camera_);
    auto [mi, ma, render] = this->getRenderRange(this->camera_);
    for (auto i = vs.cbegin(), end = vs.cend(); i != end; ++i) {
//        auto uuid = i.key();
        auto rect = i.value();
        auto min = rect.topLeft();
        auto x = (min.x() - mi.get_min_pos(bl::New).x) * this->BW() + render.x();
        auto z = (min.y() - mi.get_min_pos(bl::New).z) * this->BW() + render.y();
        auto rec = QRect(x, z, rect.width() * BW(), rect.height() * BW());
        if (rect.intersects(this->camera_)) {
            p->setPen(QPen(QColor(0, 223, 162), 3));
            p->setBrush(QBrush(QColor(0, 223, 162, 50)));
            p->drawRect(rec);

        }
    }
}

void MapWidget::drawHSAs(QPaintEvent *event, QPainter *painter) {
    QColor colors[]{QColor(0, 0, 0, 0),
                    QColor(0, 223, 162, 255), //1NetherFortress
                    QColor(255, 0, 96, 255), //2SwampHut
                    QColor(246, 250, 112, 255), //3OceanMonument
                    QColor(0, 0, 0, 0), //
                    QColor(0, 121, 255, 255), //5PillagerOutpost
                    QColor(0, 0, 0, 0),
    };
    this->foreachRegionInCamera([event, this, painter, colors](const bl::chunk_pos &rp, const QPoint &p) {
        auto hss = this->mw_->levelLoader()->getHSAs(rp);
        for (auto &hsa: hss) {
            int x = (hsa.min_pos.x - rp.x * 16) * this->BW() + p.x();
            int y = (hsa.min_pos.z - rp.z * 16) * this->BW() + p.y();
            auto outlineColor = colors[static_cast<int>(hsa.type)];
            painter->setPen(QPen(outlineColor, 3));
            auto rect = QRect(x, y,
                              abs(hsa.max_pos.x - hsa.min_pos.x + 1) * this->BW(),
                              abs(hsa.max_pos.z - hsa.min_pos.z + 1) * this->BW());
            painter->drawRect(rect);
            outlineColor.setAlpha(100);
            painter->fillRect(rect, QBrush(outlineColor));
        }
    });
}

void MapWidget::drawActors(QPaintEvent *event, QPainter *painter) {
    QPen pen(QColor(20, 20, 20));
    painter->setBrush(QBrush(QColor(255, 10, 10)));
    this->foreachRegionInCamera([event, this, painter, &pen](const bl::chunk_pos &ch, const QPoint &p) {
        auto actors = this->mw_->levelLoader()->getActorList(ch);
        for (auto &kv: actors) {
            if (!kv.first)continue;
            for (auto &actor: kv.second) {
                float x = (actor.x - (float) ch.x * 16.0f) * (float) this->BW() + (float) p.x();
                float y = (actor.z - (float) ch.z * 16.0f) * (float) this->BW() + (float) p.y();
                const int W = 18;
                painter->drawImage(QRectF(x - W, y - W, W * 2, W * 2), *kv.first, QRect(0, 0, 18, 18));
            }
        }
    });
}

void MapWidget::drawHeight(QPaintEvent *event, QPainter *painter) {
    this->forEachChunkInCamera([event, this, painter](const bl::chunk_pos &ch, const QPoint &p) {
//        auto height = this->mw_->get_world()->height(ch);
//        this->drawOneChunk(event, painter, ch, p, height);
    });
}

void MapWidget::gotoBlockPos(int x, int z) {
    int px = this->camera_.width() / 2;
    int py = this->camera_.height() / 2;
    //坐标换算
    this->origin_ = {px - static_cast<int>(x * BW()), static_cast<int>(py - z * BW())};
    this->update();
}

std::tuple<bl::chunk_pos, bl::chunk_pos, QRect> MapWidget::getRenderRange(const QRect &camera) {
    //需要的参数
    // origin  焦点原点在什么地方
    // bw 像素宽度
    const int CHUNK_WIDTH = this->cw_;
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

void MapWidget::saveImageAction(bool full_screen) {
    bool ok;
    int i = QInputDialog::getInt(this, tr("另存为"),
                                 tr("设置缩放比例"), 1, 1, 16, 1, &ok);


    if (!ok)return;
    QPixmap img;
    if (!full_screen) {
        this->has_selected_ = false;
        this->update();
        img = this->grab(this->getRenderSelectArea());
    } else {
        img = this->grab();
    }
    auto new_img = img.scaled(img.width() * i, img.height() * i);
    auto fileName = QFileDialog::getSaveFileName(this, tr("Save File"),
                                                 "/home/jana/untitled.png",
                                                 tr("Images (*.png *.jpg)"));
    if (fileName.isEmpty())return;
    new_img.save(fileName);
}

void MapWidget::delete_chunks() {

    auto minX = std::min(this->select_pos_1_.x, this->select_pos_2_.x);
    auto minZ = std::min(this->select_pos_1_.z, this->select_pos_2_.z);
    auto maxX = std::max(this->select_pos_1_.x, this->select_pos_2_.x);
    auto maxZ = std::max(this->select_pos_1_.z, this->select_pos_2_.z);
    this->mw_->deleteChunks(bl::chunk_pos(minX, minZ, this->dim_type_), bl::chunk_pos(maxX, maxZ, this->dim_type_));
}


//https://www.cnblogs.com/grandyang/p/6263929.html
void MapWidget::gotoPositionAction() {
    //后面可能需要两个输入框
    QDialog dialog(this);
    dialog.setMinimumWidth(300);
    dialog.setWindowTitle("前往坐标");

    QFormLayout form(&dialog);
    auto *x_edit = new QLineEdit(&dialog);
    auto *z_edit = new QLineEdit(&dialog);
    x_edit->setText("0");
    z_edit->setText("0");
    x_edit->setValidator(new QIntValidator());
    z_edit->setValidator(new QIntValidator());

    form.addRow(QString("X: "), x_edit);
    form.addRow(QString("Z: "), z_edit);
    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                               Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);
    QObject::connect(&buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
    QObject::connect(&buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));
    if (dialog.exec() == QDialog::Accepted) {
        auto x = x_edit->text().toInt();
        auto z = z_edit->text().toInt();
        this->gotoBlockPos(x, z);
    }
}

void MapWidget::drawMarkers(QPaintEvent *event, QPainter *painter) {
    if (this->opened_chunk_ && this->opened_chunk_pos_.dim == this->dim_type_) {
        auto [minChunk, maxChunk, renderRange] = this->getRenderRange(this->camera_);
        int x = (this->opened_chunk_pos_.x - minChunk.x) * this->cw_ + renderRange.x();
        int y = (this->opened_chunk_pos_.z - minChunk.z) * this->cw_ + renderRange.y();
        int line_width = std::max(1, static_cast<int>(BW() * 2));
        QPen pen(QColor(34, 166, 153, 250), line_width);
        painter->setPen(pen);
        painter->drawRect(
                QRect(x, y, this->cw_, this->cw_));
    }
}







