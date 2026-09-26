#include "cpumapwidget.h"

#include <qcolor.h>
#include <qnamespace.h>
#include <qpoint.h>
#include <qrect.h>

#include <QApplication>
#include <QImage>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QTimer>
#include <cmath>

#include "asynclevelloader.h"
#include "config.h"
#include "contextmenubuilder.h"
#include "importoverlay.h"
#include "maphost.h"
#include "mapinteraction.h"
#include "mapoverlays.h"
#include "mapview.h"
#include "render_options.h"

CpuMapWidget::CpuMapWidget(QWidget* parent, AsyncLevelLoader* loader, MapView* view, MapOverlays* overlays, ImportOverlay* import,
                           MapHost* host)
    : QWidget(parent), loader_(loader), view_(view), overlays_(overlays), import_(import), host_(host) {
    setMouseTracking(true);
    this->setContextMenuPolicy(Qt::CustomContextMenu);
    setFocusPolicy(Qt::FocusPolicy::StrongFocus);

    // Input is handled here as well as on the GPU renderer; because the view is
    // shared, either one can drive the same camera and selection.
    interaction_ = new MapInteraction(view_, this);
    connect(interaction_, &MapInteraction::cursorBlockChanged, this, &CpuMapWidget::mouseMove);

    // low-frequency timer only refreshes the debug window info (memory usage, etc.)
    debug_refresh_timer_ = new QTimer(this);
    connect(debug_refresh_timer_, &QTimer::timeout, this, [this] {
        if (overlays_ && overlays_->drawDebug()) update();
    });
    debug_refresh_timer_->start(2000);

    // The shared view tells us when anything about it changes, and the host when
    // the level data the tiles are baked from changed.
    if (view_) connect(view_, &MapView::viewChanged, this, qOverload<>(&QWidget::update));
}

CpuMapWidget::~CpuMapWidget() = default;

void CpuMapWidget::syncScaleLimits() { view_->applyConfiguredZoomLimits(loader_ && loader_->preloadAllChunkCoords()); }

bool CpuMapWidget::coordsOverviewMode() const { return overlays_ && overlays_->coordsOverviewMode(); }

void CpuMapWidget::foreachRegionInCamera(const std::function<void(const region_pos& p)>& f) const {
    auto [min_chunk, max_chunk, render_range] = view_->renderRange();
    (void)render_range;
    const auto region_min = constant::c2r(min_chunk);
    const auto region_max = constant::c2r(max_chunk);
    for (int i = region_min.x; i <= region_max.x; i += constant::RW) {
        for (int j = region_min.z; j <= region_max.z; j += constant::RW) {
            f({i, j, min_chunk.dim});
        }
    }
}

// base layers

void CpuMapWidget::drawImageInRegion(QPainter* p, const region_pos& pos, const QImage& image) const {
    if (!image.isNull()) p->drawImage(QRectF(pos.x, pos.z, constant::RW, constant::RW), image, image.rect());
}

void CpuMapWidget::drawBiome(QPainter* painter) {
    if (coordsOverviewMode()) return;
    foreachRegionInCamera([this, painter](const region_pos& rp) {
        auto top = loader_->bakedBiomeImage(rp);
        drawImageInRegion(painter, rp, top);
    });
}

void CpuMapWidget::drawTerrain(QPainter* painter) {
    // Zoomed out far enough, the block-accurate layers are replaced by the chunk
    // coordinate overview. Shared with the GPU renderer, which switches over at
    // the same zoom for the same reason: a far-out view spans so many region
    // tiles that baking them is what makes the frame rate collapse.
    if (coordsOverviewMode()) {
        overlays_->drawCoordsOverview(painter);
        return;
    }
    foreachRegionInCamera([this, painter](const bl::chunk_pos& rp) {
        auto terrain = loader_->bakedTerrainImage(rp);
        drawImageInRegion(painter, rp, terrain);
    });
}

// event

void CpuMapWidget::resizeEvent(QResizeEvent* event) {
    view_->setViewportSize(size());
    if (import_) import_->resize(width(), height());
}

void CpuMapWidget::paintEvent(QPaintEvent* event) {
    if (!loader_ || !loader_->isOpen() || !view_) return;
    // A widget painted before it was ever shown has no resize event yet, and the
    // zoom floor depends on whether the coordinate index is available, which only
    // the loader knows. Both are cheap to re-assert here.
    view_->setViewportSize(size());
    syncScaleLimits();
    auto [min_chunk, max_chunk, render_range] = view_->renderRange();
    (void)render_range;
    loader_->setRenderViewport(constant::c2r(min_chunk), constant::c2r(max_chunk));

    const RenderOption& option = view_->options();
    QPainter p(this);
    p.setTransform(view_->worldToView());

    if (option.layer == RenderOption::Terrain) drawTerrain(&p);
    if (option.layer == RenderOption::Biome) drawBiome(&p);
    // The overlays are shared with the other renderer of this view, so they get
    // the same transform this painter was given.
    if (overlays_) {
        overlays_->setTransform(view_->worldToView());
        if (option.getOther(RenderOption::HSA)) overlays_->drawHSAs(&p);
        if (option.getOther(RenderOption::Village)) overlays_->drawVillages(&p);
        if (option.getOther(RenderOption::SlimeChunk)) overlays_->drawSlimeChunks(&p);
        if (option.getOther(RenderOption::Grid)) overlays_->drawGrid(&p);
        if (view_->selectionVisible()) overlays_->drawSelection(&p);
        overlays_->drawOpenedChunk(&p);
    }
    if (import_ && import_->active()) import_->draw(&p, view_->scale());
    p.resetTransform();
    if (overlays_) {
        if (option.getOther(RenderOption::Actors)) overlays_->drawActors(&p);
        if (option.getOther(RenderOption::Coords)) overlays_->drawChunkPosText(&p);
        overlays_->drawCoordsMiniMap(&p);
        overlays_->drawDebugWindow(&p);
    }
    p.end();
}

void CpuMapWidget::mousePressEvent(QMouseEvent* event) { interaction_->mousePress(event); }

void CpuMapWidget::mouseMoveEvent(QMouseEvent* event) {
    // The import overlay owns the pointer while it is placing a structure.
    if (import_ && import_->active() && !(event->buttons() & Qt::LeftButton)) {
        import_->handleMouseMove(view_->viewPosToChunkPos(event->position()));
        update();
        return;
    }
    interaction_->mouseMove(event);
}

void CpuMapWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (import_ && import_->active() && !import_->placed()) {
            import_->handleLeftClick();
            update();
            return;
        }
    } else if (event->button() == Qt::RightButton) {
        if (import_ && import_->handleRightClick()) {
            update();
            return;
        }
        this->showContextMenu(event->position().toPoint());
        return;
    }
    interaction_->mouseRelease(event);
}

void CpuMapWidget::wheelEvent(QWheelEvent* event) { interaction_->wheel(event); }

void CpuMapWidget::keyPressEvent(QKeyEvent* event) {
    if (import_ && import_->handleKeyPress(event->key())) {
        update();
        event->accept();
        return;
    }
    if (interaction_->keyPress(event)) return;
    QWidget::keyPressEvent(event);
}

void CpuMapWidget::keyReleaseEvent(QKeyEvent* event) {
    if (interaction_->keyRelease(event)) return;
    QWidget::keyReleaseEvent(event);
}

void CpuMapWidget::focusOutEvent(QFocusEvent* event) {
    interaction_->reset();  // a pan key held while losing focus never gets its release
    QWidget::focusOutEvent(event);
}

void CpuMapWidget::showContextMenu(const QPoint& p) {
    // The menu acts through the host interface, so the GPU renderer shows the
    // same one; this widget resolves its own pixel-to-world mapping for it.
    const bl::chunk_pos chunk = view_->viewPosToChunkPos(QPointF(p));
    const QPoint block = view_->viewPosToBlockPos(QPointF(p));
    ContextMenuBuilder::show(host_, MapMenuRequest{mapToGlobal(p), chunk, bl::block_pos(block.x(), 0, block.y()), view_->dim(), this});
}