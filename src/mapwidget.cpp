#include "mapwidget.h"

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

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "chunkoperator.h"
#include "config.h"
#include "gotopositiondialog.h"
#include "loguru/loguru.hpp"
#include "voxelwidget.h"

QFont MapWidget::CHUNK_TEXT_FONT = QFont("JetBrains Mono", 8);

// ctor
MapWidget::MapWidget(QWidget *parent, AsyncLevelLoader *loader) : QWidget(parent), level_loader_(loader) {
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
    import_overlay_ = new ImportOverlay(this, loader);
    connect(import_overlay_, &ImportOverlay::confirmed, this, [this] { update(); });
    connect(import_overlay_, &ImportOverlay::toolbarsVisibleRequested, this, &MapWidget::toolbarsVisibleRequested);

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
    scale = std::clamp(scale, minimumZoomScale(), static_cast<qreal>(setting::current().MAXIMUM_SCALE_LEVEL));

    world_to_view_xf_ = QTransform();
    world_to_view_xf_.translate(pos.x(), pos.y());
    world_to_view_xf_.scale(scale, scale);
    world_to_view_xf_.translate(-world.x(), -world.y());

    update();
    event->accept();
}

void MapWidget::asyncRefresh() { this->update(); }

void MapWidget::gotoBlockPos(int x, int z) {
    auto viewPos = blockPosToViewPos(bl::block_pos(x, 0, z));
    auto delta = (camera_.center() - viewPos) / abs(scaleLevel());
    world_to_view_xf_.translate(delta.x(), delta.y());
    this->update();
}

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
