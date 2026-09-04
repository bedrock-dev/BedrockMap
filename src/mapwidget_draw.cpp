#include <qcolor.h>
#include <qlogging.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpoint.h>
#include <qtypes.h>
#include <qvectornd.h>

#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QColor>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFormLayout>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QRgb>
#include <Qmainwindow>
#include <QtOpenGLWidgets/QtOpenGLWidgets>
#include <Qwidget>
#include <cmath>
#include <utility>
#include <vector>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "chunkoperator.h"
#include "config.h"
#include "gotopositiondialog.h"
#include "loguru/loguru.hpp"
#include "mapwidget.h"
#include "msg.h"
#include "processmonitor.h"
#include "voxelwidget.h"

namespace {

    QPointF blockPosToFloatChunkPos(const bl::block_pos &pos) {
        auto cp = pos.to_chunk_pos();
        auto offset = pos.in_chunk_offset();
        return QPointF{cp.x + offset.x / 16., cp.z + offset.z / 16.};
    }
}  // namespace
void MapWidget::drawImageInRegion(QPaintEvent *event, QPainter *p, const region_pos &pos, QImage *img) const {
    if (img) p->drawImage(QRectF(pos.x, pos.z, constant::RW, constant::RW), *img, img->rect());
}

void MapWidget::drawGrid(QPaintEvent *event, QPainter *painter) {
    if (coordsOverviewMode()) return;
    auto pen = QPen(QColor(setting::current().GRID_LINE_COLOR), 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setBrush(Qt::NoBrush);
    pen.setCosmetic(true);

    QVector<QRect> chunkRects, largeRects;
    auto cw = this->chunkWidthInPixel();
    const auto gw = constant::GRID_WIDTH;
    forEachChunkInCamera([&chunkRects, &largeRects, cw, gw](const bl::chunk_pos &pos) {
        if (cw > 64) chunkRects.emplace_back(pos.x, pos.z, 1, 1);
        if (pos.x % constant::GRID_WIDTH == 0 && pos.z % constant::GRID_WIDTH == 0) {
            if (cw > 1.) {
                largeRects.emplace_back(pos.x - gw, pos.z - gw, gw, gw);
                largeRects.emplace_back(pos.x - gw, pos.z, gw, gw);
                largeRects.emplace_back(pos.x, pos.z - gw, gw, gw);
                largeRects.emplace_back(pos.x, pos.z, gw, gw);
            }
        }
    });
    pen.setWidth(1);
    painter->setPen(pen);
    painter->drawRects(chunkRects);
    pen.setWidth(3);
    painter->setPen(pen);
    painter->drawRects(largeRects);
}

void MapWidget::drawOpenedChunkHighlight(QPainter *p) {
    QColor color(setting::current().CHUNK_EDITOR_HIGHLIGHT_COLOR);
    QPen pen(color, setting::current().CHUNK_EDITOR_HIGHLIGHT_WIDTH);
    pen.setCosmetic(true);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->drawRect(QRectF(opened_chunk_pos_.x, opened_chunk_pos_.z, 1, 1));
}

void MapWidget::drawChunkPosText(QPaintEvent *event, QPainter *painter) {
    if (coordsOverviewMode()) return;
    QFontMetrics fm(CHUNK_TEXT_FONT);
    painter->setFont(CHUNK_TEXT_FONT);
    QPen pen(Qt::white);
    pen.setCosmetic(true);
    painter->setPen(pen);
    this->forEachChunkInCamera([event, this, painter, &fm, &pen](const bl::chunk_pos &ch) {
        if ((ch.x % constant::GRID_WIDTH == 0 && ch.z % constant::GRID_WIDTH == 0) || scaleLevel() >= 128) {
            auto text = QString("%1,%2").arg(QString::number(ch.x << 4), QString::number(ch.z << 4));
            auto p = chunkPosToViewPos(ch);
            auto rect = QRectF(p.x() + 2, p.y() + 2, fm.horizontalAdvance(text) + 4, fm.height() + 4);
            painter->fillRect(rect, QBrush(QColor(22, 22, 22, 90)));
            painter->drawText(rect, Qt::AlignCenter, text);
        }
    });
}

void MapWidget::drawDebugWindow(QPaintEvent *event, QPainter *painter) {
    QFont font("JetBrains Mono", 8, 150);
    painter->setFont(font);
    QFontMetrics fm(font);
    auto dbgInfo = level_loader_->debugInfo();
    dbgInfo.push_back(QString("Memory usage: %1 MiB").arg(QString::number(processmonitor::memoryUsageMiB())));
    int maxWidth = 1;
    for (auto &s : dbgInfo) maxWidth = std::max(maxWidth, fm.horizontalAdvance(s));
    constexpr int kMargin = 8;
    constexpr int kShadowOffset = 1;
    const int bgW = maxWidth + kMargin * 2;
    const int bgH = fm.height() * static_cast<int>(dbgInfo.size()) + kMargin * 2;
    const int baseX = width() - bgW;
    painter->fillRect(QRectF(baseX, 0, bgW, bgH), QBrush(QColor(22, 22, 22, 160)));
    for (int i = 0; i < static_cast<int>(dbgInfo.size()); i++) {
        QPoint pos(baseX + kMargin, kMargin + (i + 1) * fm.height());
        painter->setPen(QPen(QColor(0, 0, 0)));
        painter->drawText(pos + QPoint(kShadowOffset, kShadowOffset), dbgInfo[i]);
        painter->setPen(QPen(QColor(255, 255, 255)));
        painter->drawText(pos, dbgInfo[i]);
    }
}

/**
 * Rewritten here to use a caching mechanism
 * @param event
 * @param painter
 */
void MapWidget::drawSlimeChunks(QPaintEvent *event, QPainter *painter) {
    if (coordsOverviewMode()) return;
    // slime chunks only exist in the overworld
    if (option_.dim != 0) return;
    this->foreachRegionInCamera([event, this, painter](const region_pos &rp) {
        auto top = level_loader_->bakedSlimeChunkImage(rp);
        this->drawImageInRegion(event, painter, rp, top);
    });
}

void MapWidget::drawBiome(QPaintEvent *event, QPainter *painter) {
    if (coordsOverviewMode()) return;
    this->foreachRegionInCamera([event, this, painter](const region_pos &rp) {
        auto top = level_loader_->bakedBiomeImage(rp);
        this->drawImageInRegion(event, painter, rp, top);
    });
}

void MapWidget::drawTerrain(QPaintEvent *event, QPainter *painter) {
    if (coordsOverviewMode()) {
        auto [minChunk, maxChunk, renderRange] = this->getRenderRange(this->camera_);
        (void)renderRange;
        const auto alignToCoordsRegion = [](int value) {
            constexpr int size = constant::COORDS_REGION_SIZE;
            const int quotient = value / size;
            return (value % size < 0 ? quotient - 1 : quotient) * size;
        };
        const int minX = alignToCoordsRegion(minChunk.x);
        const int minZ = alignToCoordsRegion(minChunk.z);
        const int maxX = alignToCoordsRegion(maxChunk.x);
        const int maxZ = alignToCoordsRegion(maxChunk.z);
        for (int x = minX; x <= maxX; x += constant::COORDS_REGION_SIZE) {
            for (int z = minZ; z <= maxZ; z += constant::COORDS_REGION_SIZE) {
                const bl::chunk_pos rp{x, z, minChunk.dim};
                auto *image = level_loader_->chunkCoordsImage(rp);
                if (image) {
                    painter->drawImage(QRectF(x, z, constant::COORDS_REGION_SIZE, constant::COORDS_REGION_SIZE), *image, image->rect());
                }
            }
        }
        this->drawCoordsBoundingBox(painter);
        return;
    }
    this->foreachRegionInCamera([event, this, painter](const bl::chunk_pos &rp) {
        auto terrain = level_loader_->bakedTerrainImage(rp);
        this->drawImageInRegion(event, painter, rp, terrain);
    });
}

void MapWidget::drawCoordsBoundingBox(QPainter *painter) {
    if (!level_loader_->chunkCoordsReady()) return;
    const auto *bounds = level_loader_->chunkCoords().boundingBox(option_.dim);
    if (!bounds || !bounds->valid) return;

    QPen pen(QColor(0, 223, 162, 220), 2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(QRectF(bounds->min_x, bounds->min_z, static_cast<qreal>(bounds->max_x - bounds->min_x + 1),
                             static_cast<qreal>(bounds->max_z - bounds->min_z + 1)));
}

void MapWidget::drawCoordsMiniMap(QPainter *painter) {
    if (!level_loader_ || !level_loader_->preloadAllChunkCoords() || !level_loader_->chunkCoordsReady()) return;
    if (!isCoordsMiniMapEnabled()) return;

    const auto *bounds = level_loader_->chunkCoords().boundingBox(option_.dim);
    if (!bounds || !bounds->valid) return;

    const qreal boundsWidth = static_cast<qreal>(bounds->max_x) - bounds->min_x + 1.0;
    const qreal boundsHeight = static_cast<qreal>(bounds->max_z) - bounds->min_z + 1.0;
    if (boundsWidth <= 0.0 || boundsHeight <= 0.0) return;

    // The configured dimensions limit the minimap's side lengths. Its aspect ratio
    // follows the world's bounding box instead of a fixed panel ratio.
    const qreal maxWidth = static_cast<qreal>(std::min(setting::current().COORDS_MINIMAP_WIDTH, width()));
    const qreal maxHeight = static_cast<qreal>(std::min(setting::current().COORDS_MINIMAP_HEIGHT, height()));
    if (maxWidth <= 0.0 || maxHeight <= 0.0) return;

    const qreal scale = std::min(maxWidth / boundsWidth, maxHeight / boundsHeight);
    if (scale <= 0.0) return;

    const QSizeF mapSize(boundsWidth * scale, boundsHeight * scale);
    const QPointF mapTopLeft(static_cast<qreal>(width()) - mapSize.width(), static_cast<qreal>(height()) - mapSize.height());
    const QRectF globalRect(mapTopLeft, mapSize);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->fillRect(globalRect, QColor(20, 20, 20, 190));

    painter->setPen(QPen(QColor(0, 223, 162, 230), 2));
    painter->setBrush(QColor(0, 170, 120, 45));
    painter->drawRect(globalRect);

    const QTransform inverse = world_to_view_xf_.inverted();
    const QPointF viewCorners[] = {inverse.map(QPointF(0, 0)), inverse.map(QPointF(width(), 0)), inverse.map(QPointF(0, height())),
                                   inverse.map(QPointF(width(), height()))};
    qreal minX = viewCorners[0].x();
    qreal maxX = minX;
    qreal minZ = viewCorners[0].y();
    qreal maxZ = minZ;
    for (const auto &corner : viewCorners) {
        minX = std::min(minX, corner.x());
        maxX = std::max(maxX, corner.x());
        minZ = std::min(minZ, corner.y());
        maxZ = std::max(maxZ, corner.y());
    }

    const QRectF viewportRect(mapTopLeft.x() + (minX - bounds->min_x) * scale, mapTopLeft.y() + (minZ - bounds->min_z) * scale,
                              (maxX - minX) * scale, (maxZ - minZ) * scale);
    painter->setClipRect(globalRect);
    painter->setPen(QPen(QColor(255, 196, 64, 240), 2));
    painter->setBrush(QColor(255, 196, 64, 45));
    painter->drawRect(viewportRect);
    painter->restore();
}

void MapWidget::drawVillages(QPaintEvent *event, QPainter *p) {
    if (villages_.isEmpty()) return;
    auto pen = QPen(QColor(0, 223, 162), 3);
    pen.setCosmetic(true);
    p->setPen(pen);
    p->setBrush(QBrush(QColor(0, 223, 162, 30)));
    const auto &vs = villages_;
    auto [mi, ma, render] = this->getRenderRange(this->camera_);
    for (auto i = vs.cbegin(), end = vs.cend(); i != end; ++i) {
        if (this->option_.dim != i.value().dim) continue;
        auto p1 = blockPosToFloatChunkPos(i->p1);
        auto p2 = blockPosToFloatChunkPos(i->p2);
        p->drawRect(QRectF(p1, p2));
    }
}

void MapWidget::drawHSAs(QPaintEvent *event, QPainter *painter) {
    QColor colors[]{
        QColor(0, 0, 0, 0),         QColor(0, 223, 162, 255),  // 1:NetherFortress
        QColor(255, 0, 96, 255),                               // 2:SwampHut
        QColor(246, 250, 112, 255),                            // 3:OceanMonument
        QColor(0, 0, 0, 0),                                    //
        QColor(0, 121, 255, 255),                              // 5:PillagerOutpost
        QColor(0, 0, 0, 0),
    };
    this->foreachRegionInCamera([event, this, painter, colors](const bl::chunk_pos &rp) {
        auto hss = level_loader_->getHSAs(rp);
        for (auto &hsa : hss) {
            auto outlineColor = colors[static_cast<int>(hsa.type)];
            auto pen = QPen(outlineColor, 3);
            pen.setCosmetic(true);
            painter->setPen(pen);
            // max corner must be the far edge of the max block, otherwise the rect loses one block
            auto minP = blockPosToFloatChunkPos(hsa.min_pos);
            auto maxP = blockPosToFloatChunkPos(bl::block_pos(hsa.max_pos.x + 1, 0, hsa.max_pos.z + 1));
            auto rect = QRectF(minP, maxP);
            painter->drawRect(rect);
            outlineColor.setAlpha(100);
            painter->fillRect(rect, QBrush(outlineColor));
        }
    });
}

void MapWidget::drawActors(QPaintEvent *event, QPainter *painter) {
    if (coordsOverviewMode()) return;
    QPen pen(QColor(20, 20, 20));
    painter->setBrush(QBrush(QColor(255, 10, 10)));
    this->foreachRegionInCamera([event, this, painter, &pen](const bl::chunk_pos &ch) {
        if (setting::current().ACTOR_RENDER_STYLE == 0) {
            // draw all
            auto actors = level_loader_->getActorList(ch);
            for (auto &kv : actors) {
                if (!kv.first) continue;
                for (auto &actor : kv.second) {
                    auto pos = this->blockPosToViewPos(bl::block_pos(actor.x, 0, actor.z));
                    auto *img = kv.first;
                    auto w = img->width();
                    auto h = img->height();
                    painter->drawImage(QRectF(pos.x() - w / 2., pos.y() - h / 2., w, h), *img, img->rect());
                }
            }
        } else {  // only draw the first
            auto actorCtrs = level_loader_->getActorCountList(ch);
            for (auto &kv : actorCtrs) {
                auto chunk_pos = kv.first;
                auto &inChunkActors = kv.second;
                for (auto &iav : inChunkActors) {
                    auto img = iav.first;
                    auto countInfo = iav.second;
                    auto count = countInfo.count;
                    auto pos = this->blockPosToViewPos(bl::block_pos(countInfo.pos.x, 0, countInfo.pos.z));
                    auto scale = std::log2(count + 1);
                    auto w = img->width() * scale;
                    auto h = img->height() * scale;
                    painter->drawImage(QRectF(pos.x() - w / 2, pos.y() - h, w, h), *img, QRect(0, 0, img->width(), img->height()));
                }
            }
        }
    });
}
