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

    // timer
    this->sync_refresh_timer_ = new QTimer();
    connect(this->sync_refresh_timer_, SIGNAL(timeout()), this, SLOT(asyncRefresh()));
    this->sync_refresh_timer_->start(100);

    setMouseTracking(true);
    this->setContextMenuPolicy(Qt::CustomContextMenu);
    setFocusPolicy(Qt::FocusPolicy::StrongFocus);

    // dialog
    this->goto_dialog_ = new GoToPositionDialog(this);
    chunk_render_window_ = new ChunkRenderWidget();
    // Center and resize to ~80% of the parent window once
    if (auto *win = window()) {
        QSize sz = win->size() * 0.8;
        chunk_render_window_->resize(sz);
        chunk_render_window_->move(win->geometry().center() - QPoint(sz.width() / 2, sz.height() / 2));
    }

    // chunk widget
    // transform
    world_to_view_xf_.scale(64, 64);

    // import overlay
    import_overlay_ = new ImportOverlay(this, loader, level_page_);
    connect(import_overlay_, &ImportOverlay::confirmed, this, [this] { update(); });
}

// transform & position translation
void MapWidget::doScale(const QPointF viewPos, qreal scale) {
    QPointF worldPos = world_to_view_xf_.inverted().map(viewPos);
    world_to_view_xf_.translate(viewPos.x(), viewPos.y());      // 移到鼠标屏幕点
    world_to_view_xf_.scale(scale, scale);                      // 缩放
    world_to_view_xf_.translate(-worldPos.x(), -worldPos.y());  // 移回世界点
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
    auto reginMin = cfg::c2r(minChunk);
    auto regionMax = cfg::c2r(maxChunk);
    for (int i = reginMin.x; i <= regionMax.x; i += cfg::RW) {
        for (int j = reginMin.z; j <= regionMax.z; j += cfg::RW) {
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
    if (option_.layer == RenderOption::Height) drawHeight(event, &p);

    if (option_.getOther(RenderOption::HSA)) this->drawHSAs(event, &p);
    if (option_.getOther(RenderOption::Village)) this->drawVillages(event, &p);
    if (option_.getOther(RenderOption::SlimeChunk)) this->drawSlimeChunks(event, &p);
    if (option_.getOther(RenderOption::Grid)) this->drawGrid(event, &p);
    if (!capturing_) this->drawSelection(&p);
    if (import_overlay_->active()) import_overlay_->draw(&p, scaleLevel());
    p.resetTransform();
    if (option_.getOther(RenderOption::Actors)) this->drawActors(event, &p);
    if (option_.getOther(RenderOption::Coords)) this->drawChunkPosText(event, &p);
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
        emit this->mouseMove(p.x, p.z);
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
    scale = std::clamp(scale, (qreal)cfg::MINIMUM_SCALE_LEVEL, (qreal)cfg::MAXIMUM_SCALE_LEVEL);

    world_to_view_xf_ = QTransform();
    world_to_view_xf_.translate(pos.x(), pos.y());
    world_to_view_xf_.scale(scale, scale);
    world_to_view_xf_.translate(-world.x(), -world.y());

    update();
    event->accept();
}

void MapWidget::asyncRefresh() { this->update(); }

void MapWidget::drawImageInRegion(QPaintEvent *event, QPainter *p, const region_pos &pos, QImage *img) const {
    if (img) p->drawImage(QRectF(pos.x, pos.z, cfg::RW, cfg::RW), *img, img->rect());
}

void MapWidget::drawGrid(QPaintEvent *event, QPainter *painter) {
    if (thumbnailMode()) return;
    auto pen = QPen(QColor(cfg::GRID_LINE_COLOR), 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setBrush(Qt::NoBrush);
    pen.setCosmetic(true);

    QVector<QRect> chunkRects, largeRects;
    auto cw = this->chunkWidthInPixel();
    const auto gw = cfg::GRID_WIDTH;
    forEachChunkInCamera([&chunkRects, &largeRects, cw, gw](const bl::chunk_pos &pos) {
        if (cw > 64) chunkRects.emplace_back(pos.x, pos.z, 1, 1);
        if (pos.x % cfg::GRID_WIDTH == 0 && pos.z % cfg::GRID_WIDTH == 0) {
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

void MapWidget::drawChunkPosText(QPaintEvent *event, QPainter *painter) {
    if (thumbnailMode()) return;
    QFontMetrics fm(CHUNK_TEXT_FONT);
    painter->setFont(CHUNK_TEXT_FONT);
    QPen pen(Qt::white);
    pen.setCosmetic(true);
    painter->setPen(pen);
    this->forEachChunkInCamera([event, this, painter, &fm, &pen](const bl::chunk_pos &ch) {
        if ((ch.x % cfg::GRID_WIDTH == 0 && ch.z % cfg::GRID_WIDTH == 0) || scaleLevel() >= 128) {
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
 * 重写这里，采用缓存机制
 * @param event
 * @param painter
 */
void MapWidget::drawSlimeChunks(QPaintEvent *event, QPainter *painter) {
    if (thumbnailMode()) return;
    this->foreachRegionInCamera([event, this, painter](const region_pos &rp) {
        auto top = level_loader_->bakedSlimeChunkImage(rp);
        this->drawImageInRegion(event, painter, rp, top);
    });
}

void MapWidget::drawBiome(QPaintEvent *event, QPainter *painter) {
    if (thumbnailMode()) return;
    this->foreachRegionInCamera([event, this, painter](const region_pos &rp) {
        auto top = level_loader_->bakedBiomeImage(rp);
        this->drawImageInRegion(event, painter, rp, top);
    });
}

void MapWidget::drawTerrain(QPaintEvent *event, QPainter *painter) {
    this->foreachRegionInCamera([event, this, painter](const bl::chunk_pos &rp) {
        auto terrain = thumbnailMode() ? level_loader_->bakeThumbnailImage(rp) : level_loader_->bakedTerrainImage(rp);
        this->drawImageInRegion(event, painter, rp, terrain);
    });
}

void MapWidget::drawHeight(QPaintEvent *event, QPainter *painter) {
    if (thumbnailMode()) return;
    this->foreachRegionInCamera([event, this, painter](const bl::chunk_pos &rp) {
        auto height = level_loader_->bakedHeightImage(rp);
        this->drawImageInRegion(event, painter, rp, height);
    });
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
        if (this->option_.dim != static_cast<RenderOption::DimType>(i.value().dim)) continue;
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
            auto rect = QRectF(blockPosToFloatChunkPos(hsa.min_pos), blockPosToFloatChunkPos(hsa.max_pos));
            painter->drawRect(rect);
            outlineColor.setAlpha(100);
            painter->fillRect(rect, QBrush(outlineColor));
        }
    });
}

void MapWidget::drawActors(QPaintEvent *event, QPainter *painter) {
    if (thumbnailMode()) return;
    QPen pen(QColor(20, 20, 20));
    painter->setBrush(QBrush(QColor(255, 10, 10)));
    this->foreachRegionInCamera([event, this, painter, &pen](const bl::chunk_pos &ch) {
        if (cfg::ACTOR_RENDER_STYLE == 0) {
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

// 显示右键菜单
// 显示右键菜单
void MapWidget::showContextMenu(const QPoint &p) { ContextMenuBuilder::show(this, this, mapToGlobal(p)); }

void MapWidget::keyPressEvent(QKeyEvent *event) {
    if (import_overlay_->handleKeyPress(event->key())) {
        update();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

MapWidget::~MapWidget() {
    delete this->sync_refresh_timer_;
    delete chunk_render_window_;
}
