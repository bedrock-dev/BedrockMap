#include <qcolor.h>
#include <qlogging.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpoint.h>
#include <qtypes.h>
#include <qvectornd.h>

#include <QtOpenGLWidgets/QtOpenGLWidgets>
#include <cmath>

#include "asynclevelloader.h"
#include "chunkoperator.h"
#include "levelpagewidget.h"

#ifdef WIN32
#define NOMINMAX
// clang-format off
#include <Windows.h>
#include <Psapi.h>
#include <Pdh.h>
// clang-format on
#endif

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
#include <Qaction>
#include <Qimage>
#include <Qmainwindow>
#include <Qwidget>
#include <utility>
#include <vector>

#include "bedrock_key.h"
#include "config.h"
#include "gotopositiondialog.h"
#include "loguru/loguru.hpp"
#include "mapwidget.h"
#include "msg.h"
#include "voxelwidget.h"

namespace {

    double getMemUsage() {
#ifdef WIN32
        PROCESS_MEMORY_COUNTERS_EX pmc;
        GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof(pmc));
        return static_cast<double>(pmc.WorkingSetSize >> 20);
#else
        return 0;
#endif
    }

    QPointF blockPosToFloatChunkPos(const bl::block_pos &pos) {
        auto cp = pos.to_chunk_pos();
        auto offset = pos.in_chunk_offset();
        return QPointF{cp.x + offset.x / 16., cp.z + offset.z / 16.};
    }
}  // namespace

QFont MapWidget::CHUNK_TEXT_FONT = QFont("JetBrains Mono", 8);

// ctor
MapWidget::MapWidget(QWidget *parent, AsyncLevelLoader *loader) : QWidget(parent), level_loader_(loader) {
    this->level_page_ = dynamic_cast<LevelPageWidget *>(parent);
    if (!this->level_page_) {
        LOG_F(WARNING, "The parent widget of mapwidget is not LevelPageWidget!");
    }

    // trigger redraw when an async region finishes loading, replacing the old 100ms timer polling
    if (this->level_loader_) {
        connect(this->level_loader_, &AsyncLevelLoader::regionReady, this, [this] { this->update(); });
    }
    // low-frequency timer only refreshes the debug window info (memory usage, etc.)
    this->sync_refresh_timer_ = new QTimer();
    connect(this->sync_refresh_timer_, &QTimer::timeout, this, [this] {
        if (this->draw_debug_window_) this->update();
    });
    this->sync_refresh_timer_->start(2000);

    setMouseTracking(true);
    this->setContextMenuPolicy(Qt::CustomContextMenu);
    setFocusPolicy(Qt::FocusPolicy::StrongFocus);

    // dialog
    this->goto_dialog_ = new GoToPositionDialog(this);
    voxel_preview_window_ = new VoxelPreviewWidget();
    connect(voxel_preview_window_, &VoxelPreviewWidget::exportMcstructureRequested, this,
            [this](VoxelSelection selection, bool hasSelection, bool compress, bool exportEntities, bool useNewFormat) {
                std::optional<bl::block_box> blockBounds;
                if (hasSelection && selection.isValid()) {
                    const auto origin = voxel_preview_window_->voxelOrigin();
                    blockBounds = bl::block_box{origin + bl::block_pos{static_cast<int>(std::floor(selection.minimum.x())),
                                                                       static_cast<int>(std::floor(selection.minimum.y())),
                                                                       static_cast<int>(std::floor(selection.minimum.z()))},
                                                origin + bl::block_pos{static_cast<int>(std::ceil(selection.maximum.x())),
                                                                       static_cast<int>(std::ceil(selection.maximum.y())),
                                                                       static_cast<int>(std::ceil(selection.maximum.z()))}};
                }
                exportSelectionToMcstructure(option_.dim, compress, exportEntities, blockBounds, useNewFormat ? 2 : 1);
            });
    // Center and resize to ~80% of the parent window once
    if (auto *win = window()) {
        QSize sz = win->size() * 0.8;
        voxel_preview_window_->resize(sz);
        voxel_preview_window_->move(win->geometry().center() - QPoint(sz.width() / 2, sz.height() / 2));
    }

    // chunk widget
    // transform
    world_to_view_xf_.scale(64, 64);

    // import overlay
    import_overlay_ = new ImportOverlay(this, loader, level_page_);
    connect(import_overlay_, &ImportOverlay::confirmed, this, [this] { update(); });

    // keyboard pan timer: ~60fps for smooth arrow-key movement
    pan_timer_ = new QTimer(this);
    pan_timer_->setInterval(16);  // ~60 fps
    connect(pan_timer_, &QTimer::timeout, this, [this] { onPanTick(); });
}

// transform & position translation
void MapWidget::doScale(const QPointF viewPos, qreal scale) {
    QPointF worldPos = world_to_view_xf_.inverted().map(viewPos);
    world_to_view_xf_.translate(viewPos.x(), viewPos.y());      // move to mouse screen point
    world_to_view_xf_.scale(scale, scale);                      // zoom
    world_to_view_xf_.translate(-worldPos.x(), -worldPos.y());  // move back to world point
}

void MapWidget::doTranslate(const QPointF &delta) { world_to_view_xf_.translate(delta.x(), delta.y()); }

std::tuple<bl::chunk_pos, bl::chunk_pos, QRect> MapWidget::getRenderRange(const QRect &camera) {
    auto viewToWorldXf = world_to_view_xf_.inverted();
    auto topLeft = viewToWorldXf.map(QPointF(camera.x(), camera.y()));
    auto bottomRight = viewToWorldXf.map(QPointF(camera.x() + camera.width(), camera.y() + camera.height()));
    const int dim = static_cast<int>(option_.dim);
    auto minChunk = bl::chunk_pos(topLeft.x() - 1, topLeft.y() - 1, dim);
    auto maxChunk = bl::chunk_pos(bottomRight.x(), bottomRight.y(), dim);
    return {minChunk, maxChunk, camera};
}

void MapWidget::forEachChunkInCamera(const std::function<void(const region_pos &p)> &f) {
    auto [minChunk, maxChunk, renderRange] = this->getRenderRange(this->camera_);
    for (int i = minChunk.x; i <= maxChunk.x; i += 1) {
        for (int j = minChunk.z; j <= maxChunk.z; j += 1) {
            f({i, j, minChunk.dim});
        }
    }
}

void MapWidget::foreachRegionInCamera(const std::function<void(const region_pos &p)> &f) {
    auto [minChunk, maxChunk, renderRange] = this->getRenderRange(this->camera_);
    auto reginMin = constant::c2r(minChunk);
    auto regionMax = constant::c2r(maxChunk);
    for (int i = reginMin.x; i <= regionMax.x; i += constant::RW) {
        for (int j = reginMin.z; j <= regionMax.z; j += constant::RW) {
            f({i, j, minChunk.dim});
        }
    }
}

bl::block_pos MapWidget::getCursorBlockPos() {
    auto cursor = this->mapFromGlobal(QCursor::pos());
    auto pos = viewPosToBlockPos(cursor);
    return bl::block_pos{pos.x(), 0, pos.y()};
}
// event

void MapWidget::resizeEvent(QResizeEvent *event) {
    this->camera_ = QRect(-10, -10, this->width() + 10, this->height() + 10);
    import_overlay_->resize(width(), height());
}

void MapWidget::paintEvent(QPaintEvent *event) {
    if (!level_loader_ || !level_loader_->isOpen()) return;
    QPainter p(this);
    p.setTransform(world_to_view_xf_);

    if (option_.layer == RenderOption::Terrain) drawTerrain(event, &p);
    if (option_.layer == RenderOption::Biome) drawBiome(event, &p);
    if (option_.getOther(RenderOption::HSA)) this->drawHSAs(event, &p);
    if (option_.getOther(RenderOption::Village)) this->drawVillages(event, &p);
    if (option_.getOther(RenderOption::SlimeChunk)) this->drawSlimeChunks(event, &p);
    if (option_.getOther(RenderOption::Grid)) this->drawGrid(event, &p);
    if (!capturing_) this->drawSelection(&p);
    if (opened_chunk_) this->drawOpenedChunkHighlight(&p);
    if (import_overlay_->active()) import_overlay_->draw(&p, scaleLevel());
    p.resetTransform();
    if (option_.getOther(RenderOption::Actors)) this->drawActors(event, &p);
    if (option_.getOther(RenderOption::Coords)) this->drawChunkPosText(event, &p);
    this->drawCoordsMiniMap(&p);
    if (draw_debug_window_) this->drawDebugWindow(event, &p);
    p.end();
}

void MapWidget::mouseMoveEvent(QMouseEvent *event) {
    static QPointF lastMove;
    if (event->buttons() & Qt::LeftButton) {
        if (this->dragging_) {
            QPointF delta_screen = event->position() - lastMove;
            double scale = std::abs(scaleLevel());  // uniform scale
            QPointF delta_world = delta_screen / scale;
            world_to_view_xf_.translate(delta_world.x(), delta_world.y());
            update();
        } else {
            this->dragging_ = true;
        }
        lastMove = event->position();
    } else if (event->buttons() & Qt::MiddleButton) {
        if (import_overlay_->active()) return;
        if (!selection_.isDragging()) {
            selection_.startDrag(viewPosToChunkPos(event->position()));
        } else {
            selection_.updateDrag(viewPosToChunkPos(event->position()));
        }
        update();
    } else if (event->buttons() & Qt::RightButton) {
        // pass
    } else {
        if (import_overlay_->active()) {
            import_overlay_->handleMouseMove(viewPosToChunkPos(event->position()));
            update();
        }
        auto p = this->getCursorBlockPos();
        emit this->mouseMove(p.x, p.z, option_.dim);
    }
}

void MapWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (import_overlay_->active() && !import_overlay_->placed()) {
            import_overlay_->handleLeftClick();
            update();
            return;
        }
        this->dragging_ = false;
    } else if (event->button() == Qt::MiddleButton) {
        if (import_overlay_->active()) return;
        if (selection_.isDragging()) {
            auto rect = selection_.finishDrag();
            update();
            emit selectionChanged();
            LOG_F(INFO, "Selection applied: mode=%d rect=(%d,%d,%d,%d) total=%d rects", static_cast<int>(selection_.mode()), rect.x(),
                  rect.y(), rect.width(), rect.height(), static_cast<int>(selection_.rectCount()));
        }
    } else if (event->button() == Qt::RightButton) {
        if (import_overlay_->handleRightClick()) {
            update();
            return;
        }
        this->showContextMenu(event->position().toPoint());
    }
}

void MapWidget::wheelEvent(QWheelEvent *event) {
    int delta = event->angleDelta().y();
    if (delta == 0) {
        event->accept();
        return;
    }

    const double factor = (delta > 0) ? 1.1 : 1.0 / 1.1;

    QPointF pos = event->position();
    QPointF world = world_to_view_xf_.inverted().map(pos);

    double scale = world_to_view_xf_.m11() * factor;
    scale = std::clamp(scale, minimumZoomScale(), static_cast<qreal>(setting::MAXIMUM_SCALE_LEVEL));

    world_to_view_xf_ = QTransform();
    world_to_view_xf_.translate(pos.x(), pos.y());
    world_to_view_xf_.scale(scale, scale);
    world_to_view_xf_.translate(-world.x(), -world.y());

    update();
    event->accept();
}

void MapWidget::asyncRefresh() { this->update(); }

void MapWidget::drawImageInRegion(QPaintEvent *event, QPainter *p, const region_pos &pos, QImage *img) const {
    if (img) p->drawImage(QRectF(pos.x, pos.z, constant::RW, constant::RW), *img, img->rect());
}

void MapWidget::drawGrid(QPaintEvent *event, QPainter *painter) {
    if (coordsOverviewMode()) return;
    auto pen = QPen(QColor(setting::GRID_LINE_COLOR), 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
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
    QColor color(setting::CHUNK_EDITOR_HIGHLIGHT_COLOR);
    QPen pen(color, setting::CHUNK_EDITOR_HIGHLIGHT_WIDTH);
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
    dbgInfo.push_back(QString("Memory usage: %1 MiB").arg(QString::number(getMemUsage())));
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

    const auto *bounds = level_loader_->chunkCoords().boundingBox(option_.dim);
    if (!bounds || !bounds->valid) return;

    constexpr int padding = 6;
    const int availableWidth = std::min(setting::COORDS_MINIMAP_WIDTH, width());
    const int availableHeight = std::min(setting::COORDS_MINIMAP_HEIGHT, height());
    // Keep the minimap panel at a 16:9 aspect ratio while fitting the configured bounds.
    const int panelWidth = std::min(availableWidth, static_cast<int>(std::floor(availableHeight * 16.0 / 9.0)));
    const int panelHeight = static_cast<int>(std::floor(panelWidth * 9.0 / 16.0));
    if (panelWidth <= padding * 2 || panelHeight <= padding * 2) return;

    const QRectF panel(width() - panelWidth, height() - panelHeight, panelWidth, panelHeight);
    const QRectF area = panel.adjusted(padding, padding, -padding, -padding);
    const qreal boundsWidth = static_cast<qreal>(bounds->max_x) - bounds->min_x + 1.0;
    const qreal boundsHeight = static_cast<qreal>(bounds->max_z) - bounds->min_z + 1.0;
    if (boundsWidth <= 0.0 || boundsHeight <= 0.0) return;

    const qreal scale = std::min(area.width() / boundsWidth, area.height() / boundsHeight);
    if (scale <= 0.0) return;

    const QSizeF mapSize(boundsWidth * scale, boundsHeight * scale);
    const QPointF mapTopLeft(area.center().x() - mapSize.width() * 0.5, area.center().y() - mapSize.height() * 0.5);
    const QRectF globalRect(mapTopLeft, mapSize);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->fillRect(panel, QColor(20, 20, 20, 190));

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
    painter->setClipRect(area);
    painter->setPen(QPen(QColor(255, 196, 64, 240), 2));
    painter->setBrush(QColor(255, 196, 64, 45));
    painter->drawRect(viewportRect);
    painter->restore();
}

void MapWidget::drawVillages(QPaintEvent *event, QPainter *p) {
    if (!level_page_) return;
    auto pen = QPen(QColor(0, 223, 162), 3);
    pen.setCosmetic(true);
    p->setPen(pen);
    p->setBrush(QBrush(QColor(0, 223, 162, 30)));
    const auto &vs = level_page_->getVillages();
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
        if (setting::ACTOR_RENDER_STYLE == 0) {
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

void MapWidget::gotoBlockPos(int x, int z) {
    auto viewPos = blockPosToViewPos(bl::block_pos(x, 0, z));
    auto delta = (camera_.center() - viewPos) / abs(scaleLevel());
    world_to_view_xf_.translate(delta.x(), delta.y());
    this->update();
}

QImage MapWidget::captureSelectionToImage(double scale) {
    if (selection_.isEmpty()) {
        return {};
    }

    auto brect = selection_.region().boundingRect();

    // Map the bounding box from chunk-space to view (pixel) space.
    // Each chunk at integer (x, z) spans world-space [x, x+1) × [z, z+1).
    QPointF tl = world_to_view_xf_.map(QPointF(brect.left(), brect.top()));
    QPointF br = world_to_view_xf_.map(QPointF(brect.right() + 1, brect.bottom() + 1));

    QRectF viewRect = QRectF(tl, br).normalized();
    QRect captureRect = viewRect.toAlignedRect().intersected(rect());

    if (captureRect.isEmpty()) {
        return {};
    }

    // Temporarily hide selection overlay and floating toolbars
    capturing_ = true;
    if (level_page_) level_page_->setToolBarsVisible(false);
    update();
    QApplication::processEvents();

    QImage img = grab(captureRect).toImage();

    // Restore
    capturing_ = false;
    if (level_page_) level_page_->setToolBarsVisible(true);
    update();

    if (!qFuzzyCompare(scale, 1.0)) {
        img = img.scaled(img.size() * scale, Qt::KeepAspectRatio, Qt::FastTransformation);
    }

    return img;
}

void MapWidget::saveSelectionImage() {
    bool ok;
    int scale = QInputDialog::getInt(this, msg::SAEVE_AS(), msg::SET_SCALE_LEVEL(), 1, 1, 16, 1, &ok);
    if (!ok) return;
    QImage img = captureSelectionToImage(static_cast<double>(scale));
    if (img.isNull()) return;
    auto fileName = QFileDialog::getSaveFileName(this, tr("mapWidget.fileDialog.save"), {}, "Images (*.png *.jpg)");
    if (fileName.isEmpty()) return;
    img.save(fileName);
}

void MapWidget::saveFullscreenImage() {
    bool ok;
    int i = QInputDialog::getInt(this, msg::SAEVE_AS(), msg::SET_SCALE_LEVEL(), 1, 1, 16, 1, &ok);
    if (!ok) return;

    // Temporarily hide toolbars
    capturing_ = true;
    if (level_page_) level_page_->setToolBarsVisible(false);
    update();
    QApplication::processEvents();

    QImage img = this->grab().toImage();

    // Restore
    capturing_ = false;
    if (level_page_) level_page_->setToolBarsVisible(true);
    update();

    if (i != 1) {
        img = img.scaled(img.size() * i, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    auto fileName = QFileDialog::getSaveFileName(this, tr("mapWidget.fileDialog.save"), {}, "Images (*.png *.jpg)");
    if (fileName.isEmpty()) return;
    img.save(fileName);
}

void MapWidget::gotoPositionAction() {
    if (this->goto_dialog_->exec() == QDialog::Accepted) {
        if (this->goto_dialog_->positionValid()) {
            gotoBlockPos(goto_dialog_->x(), goto_dialog_->z());
        } else {
            WARN(msg::INVALID_COORDINATE());
        }
    }
}

void MapWidget::clearSelection() {
    selection_.clear();
    update();
    emit selectionChanged();
}

void MapWidget::copySelectionToClipboard(int dim) {
    if (selection_.isEmpty()) return;
    ExportedRegion region;
    auto sel = selection_.region();
    for (const auto &r : sel) {
        for (int x = r.x(); x < r.x() + r.width(); x++) {
            for (int z = r.y(); z < r.y() + r.height(); z++) {
                auto raw = level_loader_->getRawChunk(bl::chunk_pos(x, z, dim));
                if (raw.has_value()) region.addChunk(raw.value());
            }
        }
    }
    if (region.isEmpty()) return;
    auto data = region.serialize();
    auto *md = new QMimeData();
    md->setData("application/x-bedrockmap-region", QByteArray(data.data(), static_cast<int>(data.size())));
    auto *clip = QApplication::clipboard();
    clip->clear(QClipboard::Clipboard);
    clip->setMimeData(md, QClipboard::Clipboard);
    LOG_F(INFO, "MapWidget: copied %d chunks to clipboard", static_cast<int>(region.chunkCount()));
}

void MapWidget::pasteFromClipboard(int dim) {
    if (modificationBlocked()) return;
    auto *clip = QApplication::clipboard();
    const auto *md = clip->mimeData();
    if (!md || !md->hasFormat("application/x-bedrockmap-region")) {
        WARN(msg::PASTE_NO_DATA());
        return;
    }
    QByteArray rawData = md->data("application/x-bedrockmap-region");
    if (rawData.isEmpty()) {
        INFO(msg::PASTE_DATA_EMPTY());
        return;
    }
    bl::chunk_pos anchor(0, 0, dim);
    if (!import_overlay_->startPaste(rawData, dim, anchor)) {
        INFO(msg::PASTE_DATA_INVALID());
        return;
    }
    update();
}

void MapWidget::exportSelectionToFile(int dim) {
    if (selection_.isEmpty()) return;
    auto fp = QFileDialog::getSaveFileName(nullptr, QObject::tr("mapWidget.rightMenu.exportRegion"), {}, msg::ALL_FILES());
    if (fp.isEmpty()) return;
    ChunkOperator::exportRegion(selection_.region(), fp, *level_loader_, dim);
    INFO(msg::EXPORT_COMPLETE());
}

void MapWidget::exportSelectionToMcstructure(int dim, bool compress, bool exportEntities, const std::optional<bl::block_box> &blockBounds,
                                             int32_t version) {
    if (selection_.isEmpty() || !level_loader_) return;

    const auto filePath =
        QFileDialog::getSaveFileName(this, tr("mapWidget.rightMenu.exportMcstructure"), {}, tr("MCStructure files (*.mcstructure)"));
    if (filePath.isEmpty()) return;

    QString outputPath = filePath;
    if (QFileInfo(outputPath).suffix().isEmpty()) outputPath += QStringLiteral(".mcstructure");

    if (!BlockRegionOperator::exportMcstructure(selection_.region(), outputPath, *level_loader_, dim, compress, blockBounds, version,
                                                exportEntities)) {
        QMessageBox::warning(this, tr("mapWidget.rightMenu.exportMcstructure"), tr("mapWidget.rightMenu.exportMcstructureFailed"));
        return;
    }
    INFO(msg::EXPORT_COMPLETE());
}

void MapWidget::importFromFile(int dim) {
    if (modificationBlocked()) return;
    auto fp = QFileDialog::getOpenFileName(nullptr, QObject::tr("mapWidget.rightMenu.importRegion"), {}, msg::BCHKS_FILES());
    if (fp.isEmpty()) return;
    bl::chunk_pos anchor(0, 0, dim);
    import_overlay_->startImport(fp, dim, anchor);
    update();
}

void MapWidget::deleteSelection(int dim) {
    if (selection_.isEmpty() || modificationBlocked()) return;
    ChunkOperator::deleteRegion(selection_.region(), *level_loader_, dim);
    update();
}

void MapWidget::createVoidSelection(int dim) {
    if (selection_.isEmpty() || modificationBlocked()) return;
    ChunkOperator::createVoid(selection_.region(), *level_loader_, dim);
    update();
}

void MapWidget::setSelectionBiome(int biome, int dim) {
    if (selection_.isEmpty() || modificationBlocked()) return;
    ChunkOperator::setRegionBiome(selection_.region(), *level_loader_, static_cast<bl::biome>(biome), dim);
    update();
}

bool MapWidget::modificationBlocked() {
    if (!level_loader_ || !level_loader_->chunkCoordsLoading()) return false;
    QMessageBox::warning(this, msg::READ_ONLY(), msg::EDITING_DISABLED_DURING_COORDS_LOADING());
    return true;
}

void MapWidget::show3DView(int dim) {
    if (selection_.isEmpty() || selection_.rectCount() != 1) return;
    auto rect = selection_.region().boundingRect();
    bl::chunk_pos minPos(rect.x(), rect.y(), dim);
    bl::chunk_pos maxPos(rect.x() + rect.width() - 1, rect.y() + rect.height() - 1, dim);
    voxel_preview_window_->loadChunksAsync(minPos, maxPos, *level_loader_);
}

void MapWidget::syncToolbars() {
    if (level_page_) level_page_->syncToolbars();
}

// show right-click context menu
void MapWidget::showContextMenu(const QPoint &p) { ContextMenuBuilder::show(this, this, mapToGlobal(p)); }

void MapWidget::keyPressEvent(QKeyEvent *event) {
    if (import_overlay_->handleKeyPress(event->key())) {
        update();
        event->accept();
        return;
    }

    switch (event->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Left:
        case Qt::Key_Right:
            pressed_keys_.insert(event->key());
            if (!pan_timer_->isActive()) pan_timer_->start();
            event->accept();
            return;
        default:
            QWidget::keyPressEvent(event);
    }
}

void MapWidget::keyReleaseEvent(QKeyEvent *event) {
    switch (event->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Left:
        case Qt::Key_Right:
            pressed_keys_.remove(event->key());
            if (pressed_keys_.isEmpty()) pan_timer_->stop();
            event->accept();
            return;
        default:
            QWidget::keyReleaseEvent(event);
    }
}

void MapWidget::onPanTick() {
    constexpr qreal kPanSpeed = 20.0 / 60.0;
    QPointF delta;
    if (pressed_keys_.contains(Qt::Key_Left)) delta += QPointF(kPanSpeed, 0);
    if (pressed_keys_.contains(Qt::Key_Right)) delta += QPointF(-kPanSpeed, 0);
    if (pressed_keys_.contains(Qt::Key_Up)) delta += QPointF(0, kPanSpeed);
    if (pressed_keys_.contains(Qt::Key_Down)) delta += QPointF(0, -kPanSpeed);
    if (!delta.isNull()) {
        doTranslate(delta);
        update();
    }
}

MapWidget::~MapWidget() {
    delete this->sync_refresh_timer_;
    delete voxel_preview_window_;
}
