#include "mapoverlays.h"

#include <qbrush.h>
#include <qfontmetrics.h>
#include <qpainter.h>
#include <qpen.h>
#include <qpoint.h>
#include <qsize.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include "asynclevelloader.h"
#include "config.h"
#include "maptile.h"
#include "mapview.h"
#include "processmonitor.h"
#include "utils.h"

namespace {

    // A block position as a fractional chunk position, for the world-space rects.
    QPointF blockPosToChunkPos(const bl::block_pos& pos) {
        const auto cp = pos.to_chunk_pos();
        const auto offset = pos.in_chunk_offset();
        return QPointF{cp.x + offset.x / 16., cp.z + offset.z / 16.};
    }

}  // namespace

QFont MapOverlays::CHUNK_TEXT_FONT = QFont("JetBrains Mono", 8);

MapOverlays::MapOverlays(MapView* view, AsyncLevelLoader* loader) : view_(view), level_loader_(loader) {}

QSize MapOverlays::targetSize(const QPainter* painter) const {
    // The widget (or image) being painted into. Taking it from the paint device
    // means the layers behave the same whether they are drawn into a widget or
    // into an offscreen image, which is how the benchmarks capture them.
    const QPaintDevice* device = painter->device();
    if (!device) return view_ ? view_->viewportSize() : QSize();
    return QSize(device->width(), device->height());
}

QPointF MapOverlays::chunkPosToViewPos(const bl::chunk_pos& cp) const {
    return world_to_view_.map(QPointF(static_cast<qreal>(cp.x), static_cast<qreal>(cp.z)));
}

QPointF MapOverlays::blockPosToViewPos(const bl::block_pos& bp) const {
    return world_to_view_.map(QPointF(static_cast<qreal>(bp.x) / 16.0, static_cast<qreal>(bp.z) / 16.0));
}

bool MapOverlays::coordsOverviewMode() const {
    if (!view_ || !level_loader_) return false;
    return level_loader_->preloadAllChunkCoords() && view_->scaleLevel() < setting::current().MINIMUM_SCALE_LEVEL;
}

void MapOverlays::forEachChunkInCamera(const std::function<void(const bl::chunk_pos&)>& f) const {
    if (!view_) return;
    const auto [min_chunk, max_chunk, rect] = view_->renderRange();
    (void)rect;
    for (int i = min_chunk.x; i <= max_chunk.x; ++i) {
        for (int j = min_chunk.z; j <= max_chunk.z; ++j) {
            f({i, j, min_chunk.dim});
        }
    }
}

void MapOverlays::foreachRegionInCamera(const std::function<void(const bl::chunk_pos&)>& f) const {
    if (!view_) return;
    const auto [min_chunk, max_chunk, rect] = view_->renderRange();
    (void)rect;
    const auto region_min = constant::c2r(min_chunk);
    const auto region_max = constant::c2r(max_chunk);
    for (int i = region_min.x; i <= region_max.x; i += constant::RW) {
        for (int j = region_min.z; j <= region_max.z; j += constant::RW) {
            f({i, j, min_chunk.dim});
        }
    }
}

void MapOverlays::drawCoordsOverview(QPainter* painter) const {
    if (!painter || !level_loader_ || !view_) return;
    const auto [min_chunk, max_chunk, rect] = view_->renderRange();
    (void)rect;
    constexpr int kTile = constant::COORDS_REGION_SIZE;
    const auto tile_floor = [](int value) { return (value / kTile - (value % kTile < 0 ? 1 : 0)) * kTile; };
    const auto tile_ceil = [](int value) { return (value / kTile + (value % kTile < 0 ? 0 : 1)) * kTile; };
    const int view_x0 = tile_floor(min_chunk.x);
    const int view_z0 = tile_floor(min_chunk.z);
    const int view_x1 = tile_ceil(max_chunk.x);
    const int view_z1 = tile_ceil(max_chunk.z);

    const auto bounds = level_loader_->chunkCoordsBoundingBox(view_->dim());
    if (!bounds || !bounds->valid) {
        // The index is still being scanned, so the whole view is the same
        // "loading" tile: fill it in one call instead of a blit per tile, which
        // on a far-out view would be tens of thousands of them.
        painter->fillRect(QRectF(view_x0, view_z0, view_x1 - view_x0, view_z1 - view_z0), QBrush(MapTile::COORDS_LOADING_TILE()));
        return;
    }

    // Tiles outside the archive's bounding box hold no chunks: lay their colour
    // down once and blit only the tiles the world covers, so a far-out view costs
    // what the world costs instead of what the (much larger) view span costs.
    painter->fillRect(QRectF(view_x0, view_z0, view_x1 - view_x0, view_z1 - view_z0), MapTile::COORDS_EMPTY_TILE().pixelColor(0, 0));
    const int x0 = std::max(view_x0, tile_floor(bounds->min_x));
    const int z0 = std::max(view_z0, tile_floor(bounds->min_z));
    const int x1 = std::min(view_x1, tile_ceil(bounds->max_x + 1));
    const int z1 = std::min(view_z1, tile_ceil(bounds->max_z + 1));
    for (int x = x0; x < x1; x += kTile) {
        for (int z = z0; z < z1; z += kTile) {
            const QImage image = level_loader_->chunkCoordsImage(bl::chunk_pos{x, z, min_chunk.dim});
            if (!image.isNull()) painter->drawImage(QRectF(x, z, kTile, kTile), image, image.rect());
        }
    }
    drawCoordsBoundingBox(painter);
}

void MapOverlays::drawCoordsBoundingBox(QPainter* painter) const {
    if (!level_loader_ || !level_loader_->chunkCoordsReady()) return;
    const auto bounds = level_loader_->chunkCoordsBoundingBox(view_ ? view_->dim() : 0);
    if (!bounds || !bounds->valid) return;

    QPen pen(QColor(0, 223, 162, 220), 2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(QRectF(bounds->min_x, bounds->min_z, static_cast<qreal>(bounds->max_x - bounds->min_x + 1),
                             static_cast<qreal>(bounds->max_z - bounds->min_z + 1)));
}

void MapOverlays::drawHSAs(QPainter* painter) const {
    if (!level_loader_) return;
    const QColor colors[]{
        QColor(0, 0, 0, 0),         QColor(0, 223, 162, 255),  // 1: NetherFortress
        QColor(255, 0, 96, 255),                               // 2: SwampHut
        QColor(246, 250, 112, 255),                            // 3: OceanMonument
        QColor(0, 0, 0, 0),         QColor(0, 121, 255, 255),  // 5: PillagerOutpost
        QColor(0, 0, 0, 0),
    };
    foreachRegionInCamera([this, painter, &colors](const bl::chunk_pos& rp) {
        for (const auto& hsa : level_loader_->getHSAs(rp)) {
            QColor outline = colors[static_cast<int>(hsa.type)];
            QPen pen(outline, 3);
            pen.setCosmetic(true);
            painter->setPen(pen);
            // The max corner has to be the far edge of the max block, otherwise the
            // rect loses one block.
            const auto min_p = blockPosToChunkPos(hsa.min_pos);
            const auto max_p = blockPosToChunkPos(bl::block_pos(hsa.max_pos.x + 1, 0, hsa.max_pos.z + 1));
            const QRectF rect(min_p, max_p);
            painter->drawRect(rect);
            outline.setAlpha(100);
            painter->fillRect(rect, QBrush(outline));
        }
    });
}

void MapOverlays::drawVillages(QPainter* painter) const {
    if (villages_.isEmpty() || !view_) return;
    QPen pen(QColor(0, 223, 162), 3);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(QBrush(QColor(0, 223, 162, 30)));
    for (auto i = villages_.cbegin(), end = villages_.cend(); i != end; ++i) {
        if (view_->dim() != i.value().dim) continue;
        painter->drawRect(QRectF(blockPosToChunkPos(i->p1), blockPosToChunkPos(i->p2)));
    }
}

void MapOverlays::drawSlimeChunks(QPainter* painter) const {
    if (coordsOverviewMode()) return;
    if (!level_loader_ || !view_ || view_->dim() != 0) return;  // slime chunks are overworld only
    foreachRegionInCamera([this, painter](const bl::chunk_pos& rp) {
        const QImage image = level_loader_->bakedSlimeChunkImage(rp);
        if (!image.isNull()) painter->drawImage(QRectF(rp.x, rp.z, constant::RW, constant::RW), image, image.rect());
    });
}

void MapOverlays::drawGrid(QPainter* painter) const {
    if (coordsOverviewMode() || !view_) return;
    QPen pen(QColor(setting::current().GRID_LINE_COLOR), 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setBrush(Qt::NoBrush);
    pen.setCosmetic(true);

    QVector<QRect> chunkRects, largeRects;
    const auto cw = view_->scaleLevel();
    const auto gw = constant::GRID_WIDTH;
    forEachChunkInCamera([&chunkRects, &largeRects, cw, gw](const bl::chunk_pos& pos) {
        if (cw > 64) chunkRects.emplace_back(pos.x, pos.z, 1, 1);
        if (pos.x % gw == 0 && pos.z % gw == 0 && cw > 1.) {
            largeRects.emplace_back(pos.x - gw, pos.z - gw, gw, gw);
            largeRects.emplace_back(pos.x - gw, pos.z, gw, gw);
            largeRects.emplace_back(pos.x, pos.z - gw, gw, gw);
            largeRects.emplace_back(pos.x, pos.z, gw, gw);
        }
    });
    pen.setWidth(1);
    painter->setPen(pen);
    painter->drawRects(chunkRects);
    pen.setWidth(3);
    painter->setPen(pen);
    painter->drawRects(largeRects);
}

void MapOverlays::drawSelection(QPainter* painter) const {
    if (!view_) return;
    view_->selection().draw(painter, view_->scaleLevel());
}

void MapOverlays::drawOpenedChunk(QPainter* painter) const {
    if (!view_ || !view_->openedChunk()) return;
    QPen pen(QColor(setting::current().CHUNK_EDITOR_HIGHLIGHT_COLOR), setting::current().CHUNK_EDITOR_HIGHLIGHT_WIDTH);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    const auto& pos = *view_->openedChunk();
    painter->drawRect(QRectF(pos.x, pos.z, 1, 1));
}

void MapOverlays::drawActors(QPainter* painter) const {
    if (coordsOverviewMode() || !level_loader_ || !view_) return;
    foreachRegionInCamera([this, painter](const bl::chunk_pos& rp) {
        if (setting::current().ACTOR_RENDER_STYLE == 0) {
            // Every actor, at its own position.
            for (auto& [image, positions] : level_loader_->getActorList(rp)) {
                if (!image) continue;
                for (auto& actor : positions) {
                    const auto pos = blockPosToViewPos(bl::block_pos(actor.x, 0, actor.z));
                    const auto w = image->width();
                    const auto h = image->height();
                    painter->drawImage(QRectF(pos.x() - w / 2., pos.y() - h / 2., w, h), *image, image->rect());
                }
            }
            return;
        }
        // One icon per actor type per chunk, scaled by how many there are.
        for (auto& [chunk, counts] : level_loader_->getActorCountList(rp)) {
            for (auto& [image, countInfo] : counts) {
                if (!image) continue;
                const auto pos = blockPosToViewPos(bl::block_pos(countInfo.pos.x, 0, countInfo.pos.z));
                const auto scale = std::log2(countInfo.count + 1);
                const auto w = image->width() * scale;
                const auto h = image->height() * scale;
                painter->drawImage(QRectF(pos.x() - w / 2, pos.y() - h, w, h), *image, QRect(0, 0, image->width(), image->height()));
            }
        }
    });
}

void MapOverlays::drawChunkPosText(QPainter* painter) const {
    if (coordsOverviewMode() || !view_) return;
    QFontMetrics fm(CHUNK_TEXT_FONT);
    painter->setFont(CHUNK_TEXT_FONT);
    QPen pen(Qt::white);
    pen.setCosmetic(true);
    painter->setPen(pen);
    const qreal scale = view_->scaleLevel();
    forEachChunkInCamera([this, painter, &fm, scale](const bl::chunk_pos& ch) {
        const bool on_major_grid = ch.x % constant::GRID_WIDTH == 0 && ch.z % constant::GRID_WIDTH == 0;
        if (!on_major_grid && scale < 128) return;
        const auto text = QString("%1,%2").arg(QString::number(ch.x << 4), QString::number(ch.z << 4));
        const auto pos = chunkPosToViewPos(ch);
        const auto rect = QRectF(pos.x() + 2, pos.y() + 2, fm.horizontalAdvance(text) + 4, fm.height() + 4);
        painter->fillRect(rect, QBrush(QColor(22, 22, 22, 90)));
        painter->drawText(rect, Qt::AlignCenter, text);
    });
}

void MapOverlays::drawCoordsMiniMap(QPainter* painter) const {
    if (!level_loader_ || !coords_minimap_) return;
    if (!level_loader_->preloadAllChunkCoords() || !level_loader_->chunkCoordsReady()) return;

    const auto bounds = level_loader_->chunkCoordsBoundingBox(view_ ? view_->dim() : 0);
    if (!bounds || !bounds->valid) return;

    const QSize size = targetSize(painter);
    if (size.isEmpty()) return;

    const qreal boundsWidth = static_cast<qreal>(bounds->max_x) - bounds->min_x + 1.0;
    const qreal boundsHeight = static_cast<qreal>(bounds->max_z) - bounds->min_z + 1.0;
    if (boundsWidth <= 0.0 || boundsHeight <= 0.0) return;

    // The configured dimensions limit the side lengths; the aspect ratio follows
    // the world's bounding box rather than a fixed panel ratio.
    const qreal maxWidth = static_cast<qreal>(std::min(setting::current().COORDS_MINIMAP_WIDTH, size.width()));
    const qreal maxHeight =
        static_cast<qreal>(std::min(setting::current().COORDS_MINIMAP_HEIGHT, std::max(0, size.height() - screen_inset_)));
    if (maxWidth <= 0.0 || maxHeight <= 0.0) return;

    const qreal scale = std::min(maxWidth / boundsWidth, maxHeight / boundsHeight);
    if (scale <= 0.0) return;

    const QSizeF mapSize(boundsWidth * scale, boundsHeight * scale);
    const QPointF mapTopLeft(static_cast<qreal>(size.width()) - mapSize.width(),
                             static_cast<qreal>(size.height() - screen_inset_) - mapSize.height());
    const QRectF globalRect(mapTopLeft, mapSize);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->fillRect(globalRect, QColor(20, 20, 20, 190));
    painter->setPen(QPen(QColor(0, 223, 162, 230), 2));
    painter->setBrush(QColor(0, 170, 120, 45));
    painter->drawRect(globalRect);

    const QTransform inverse = world_to_view_.inverted();
    const QPointF corners[] = {inverse.map(QPointF(0, 0)), inverse.map(QPointF(size.width(), 0)), inverse.map(QPointF(0, size.height())),
                               inverse.map(QPointF(size.width(), size.height()))};
    qreal minX = corners[0].x();
    qreal maxX = minX;
    qreal minZ = corners[0].y();
    qreal maxZ = minZ;
    for (const auto& corner : corners) {
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

void MapOverlays::drawDebugWindow(QPainter* painter) const {
    if (!draw_debug_ || !level_loader_) return;
    QFont font("JetBrains Mono", 8, 150);
    painter->setFont(font);
    QFontMetrics fm(font);
    auto info = level_loader_->debugInfo();
    info.push_back(QString("Memory usage: %1 MiB").arg(QString::number(processmonitor::memoryUsageMiB())));
    int max_width = 1;
    for (const auto& line : info) max_width = std::max(max_width, fm.horizontalAdvance(line));
    constexpr int kMargin = 8;
    constexpr int kShadowOffset = 1;
    const int bg_w = max_width + kMargin * 2;
    const int bg_h = fm.height() * static_cast<int>(info.size()) + kMargin * 2;
    const QSize size = targetSize(painter);
    const int base_x = size.width() - bg_w;
    painter->fillRect(QRectF(base_x, 0, bg_w, bg_h), QBrush(QColor(22, 22, 22, 160)));
    for (int i = 0; i < static_cast<int>(info.size()); ++i) {
        const QPoint pos(base_x + kMargin, kMargin + (i + 1) * fm.height());
        painter->setPen(QPen(QColor(0, 0, 0)));
        painter->drawText(pos + QPoint(kShadowOffset, kShadowOffset), info[i]);
        painter->setPen(QPen(QColor(255, 255, 255)));
        painter->drawText(pos, info[i]);
    }
}
