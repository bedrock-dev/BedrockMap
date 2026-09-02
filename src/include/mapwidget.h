#ifndef MAPWIDGET_H
#define MAPWIDGET_H

#include <qevent.h>
#include <qfont.h>
#include <qpoint.h>
#include <qtmetamacros.h>
#include <qtransform.h>
#include <qtypes.h>

#include <QObject>
#include <QPaintEvent>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <QtDebug>
#include <QtOpenGLWidgets/QtOpenGLWidgets>
#include <algorithm>
#include <array>
#include <optional>
#include <tuple>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "chunkoperator.h"
#include "config.h"
#include "contextmenubuilder.h"
#include "gotopositiondialog.h"
#include "importoverlay.h"
#include "selectionregion.h"
#include "utils.h"
#include "voxelwidget.h"

/// Encapsulates selection state and drawing.
class SelectionController {
   public:
    using Mode = SelectionRegion::Mode;

    bool isEmpty() const { return region_.isEmpty() && !dragging_; }
    bool isDragging() const { return dragging_; }
    const QRegion &region() const { return region_.region(); }
    Mode mode() const { return region_.mode(); }
    void setMode(Mode m) { region_.setMode(m); }
    void clear() { region_.clear(); }
    size_t rectCount() const { return region_.region().rectCount(); }
    int chunkCount() const { return region_.chunkCount(); }

    void startDrag(bl::chunk_pos p) {
        dragging_ = true;
        drag_start_ = p;
        drag_current_ = p;
    }
    void updateDrag(bl::chunk_pos p) { drag_current_ = p; }
    QRect finishDrag() {
        dragging_ = false;
        auto [mn, mx] = normalize_chunk_range(drag_start_, drag_current_);
        QRect rect(mn.x, mn.z, mx.x - mn.x + 1, mx.z - mn.z + 1);
        region_.applyRect(rect);
        return rect;
    }

    bool contains(const QPoint &p) const { return !region_.isEmpty() && region_.region().contains(p); }

    void draw(QPainter *p, qreal scaleLevel) const {
        if (region_.isEmpty() && !dragging_) return;

        QPainterPath path;
        for (const auto &r : region_.region()) {
            path.addRect(QRectF(static_cast<qreal>(r.x()), static_cast<qreal>(r.y()), static_cast<qreal>(r.width()),
                                static_cast<qreal>(r.height())));
        }
        path = path.simplified();
        p->fillPath(path, QColor(218, 255, 251, 80));
        p->strokePath(path, QPen(QColor(218, 255, 251), 2.0 / scaleLevel));

        if (dragging_) {
            auto [mn, mx] = normalize_chunk_range(drag_start_, drag_current_);
            QRectF rf(static_cast<qreal>(mn.x), static_cast<qreal>(mn.z), static_cast<qreal>(mx.x - mn.x + 1),
                      static_cast<qreal>(mx.z - mn.z + 1));
            p->setPen(QPen(QColor(218, 255, 251), 3.0 / scaleLevel, Qt::DashLine));
            p->setBrush(QColor(218, 255, 251, 60));
            p->drawRect(rf);
        }
    }

   private:
    SelectionRegion region_;
    bl::chunk_pos drag_start_;
    bl::chunk_pos drag_current_;
    bool dragging_{false};
};

class LevelPageWidget;
class MapWidget : public QWidget {
    Q_OBJECT

    friend class ContextMenuBuilder;

   public:
    struct RenderOption {
        enum LayerType { Terrain = 0, Biome = 1 };
        enum OtherType { Grid = 0, Coords = 1, SlimeChunk = 2, Actors = 3, Village = 4, HSA = 5, OtherLen = 6 };

        static constexpr int OverWorld = 0;
        static constexpr int Nether = 1;
        static constexpr int TheEnd = 2;

        RenderOption() { reset(); }

        int dim{OverWorld};
        LayerType layer{Terrain};
        std::array<bool, OtherType::OtherLen> others{};
        inline void reset() {
            dim = OverWorld;
            layer = Terrain;
            std::fill(others.begin(), others.end(), false);
            setOther(Grid, true);
        }

        inline void setDim(int nDim) { dim = nDim; }
        inline void setLayer(LayerType nLayer) { layer = nLayer; }

        inline bool toggleOther(OtherType type) {
            if (type < 0 || type >= OtherType::OtherLen) return true;
            others[type] = !others[type];
            return others[type];
        }

        inline void setOther(OtherType type, bool value) {
            if (type < 0 || type >= OtherType::OtherLen) return;
            others[type] = value;
        }
        inline bool getOther(OtherType type) {
            if (type < 0 || type >= OtherType::OtherLen) return false;
            return others[type];
        }
    };

    static QFont CHUNK_TEXT_FONT;

    // ctor
    MapWidget(QWidget *parent, AsyncLevelLoader *loader = nullptr);

    // transform & position translation
    void doScale(const QPointF viewCenter, qreal scale);

    void doTranslate(const QPointF &delta);

    std::tuple<bl::chunk_pos, bl::chunk_pos, QRect> getRenderRange(const QRect &camera);

    qreal chunkWidthInPixel() const { return world_to_view_xf_.m11(); }

    qreal scaleLevel() const { return world_to_view_xf_.m11(); }

    void forEachChunkInCamera(const std::function<void(const region_pos &p)> &f);

    void foreachRegionInCamera(const std::function<void(const region_pos &p)> &f);

    bl::block_pos getCursorBlockPos();

    QPointF blockPosToViewPos(const bl::block_pos &bp) {
        auto worldPos = QPointF(static_cast<qreal>(bp.x) / 16.0, static_cast<qreal>(bp.z) / 16.0);
        return world_to_view_xf_.map(worldPos);
    }

    QPointF chunkPosToViewPos(const bl::chunk_pos &cp) {
        auto worldPos = QPointF(static_cast<qreal>(cp.x), static_cast<qreal>(cp.z));
        return world_to_view_xf_.map(worldPos);
    }

    bl::chunk_pos viewPosToChunkPos(const QPointF &vp) {
        auto worldPos = world_to_view_xf_.inverted().map(vp);
        return bl::chunk_pos(static_cast<int>(std::floor(worldPos.x())), static_cast<int>(std::floor(worldPos.y())), option_.dim);
    }

    QPoint viewPosToBlockPos(const QPointF &vp) {
        auto worldPos = world_to_view_xf_.inverted().map(vp);
        auto x = static_cast<int>(std::floor(worldPos.x() * 16.0f));
        auto z = static_cast<int>(std::floor(worldPos.y() * 16.0f));
        return QPoint(x, z);
    }

    // getter
    MapWidget::RenderOption renderOption() { return option_; }
    AsyncLevelLoader *getLevelLoader() { return level_loader_; }

    // event

    void resizeEvent(QResizeEvent *event) override;

    void paintEvent(QPaintEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

    void keyPressEvent(QKeyEvent *event) override;

    void keyReleaseEvent(QKeyEvent *event) override;

    // signals
   public:
    inline void changeDimension(int dim) {
        option_.setDim(dim);
        this->update();
    }

    inline void changeLayer(RenderOption::LayerType layer) {
        option_.setLayer(layer);
        this->update();
    }

    inline bool toggleOther(RenderOption::OtherType other) {
        auto ret = option_.toggleOther(other);
        this->update();
        return ret;
    }

    inline void setOther(RenderOption::OtherType other, bool value) {
        option_.setOther(other, value);
        this->update();
    }

    inline void setDim(int dim) { changeDimension(dim); }

    inline void setLayer(RenderOption::LayerType layer) { changeLayer(layer); }

    inline void setDrawDebug(bool enable) { this->draw_debug_window_ = enable; }
    inline bool isDebugEnabled() const { return draw_debug_window_; }

    inline void setTransparentVoid(bool v) {
        if (transparent_void_ == v) return;
        transparent_void_ = v;
        if (level_loader_) {
            level_loader_->setTransparentVoid(v);
            level_loader_->clearAllCache();
        }
        update();
    }

    inline bool toggleTransparentVoid() {
        setTransparentVoid(!transparent_void_);
        return transparent_void_;
    }

    inline bool transparentVoid() const { return transparent_void_; }

    // selection
    inline SelectionController &selection() { return selection_; }
    inline void setSelectionMode(SelectionController::Mode mode) { selection_.setMode(mode); }

    inline void selectChunk(const bl::chunk_pos &p) { this->opened_chunk_ = true, this->opened_chunk_pos_ = p; }

    inline void unselectChunk() { this->opened_chunk_ = false; }

    void clearSelection();
    void copySelectionToClipboard(int dim);
    void pasteFromClipboard(int dim);
    void exportSelectionToFile(int dim);
    void exportSelectionToMcstructure(int dim, bool compress = false, bool exportEntities = false,
                                      const std::optional<bl::block_box> &blockBounds = std::nullopt, int32_t version = 1);
    void importFromFile(int dim);
    void deleteSelection(int dim);
    void createVoidSelection(int dim);
    void setSelectionBiome(int biome, int dim);
    void show3DView(int dim);
    void syncToolbars();

    // signals
   signals:
    void mouseMove(int x, int z, int dim);  // NOLINT

    void requestOpenChunkEditor(const bl::chunk_pos &pos);

    void selectionChanged();

   public slots:

    void asyncRefresh();

    void gotoBlockPos(int x, int z);

   private:
    [[nodiscard]] inline bool coordsOverviewMode() const {
        return level_loader_ && level_loader_->preloadAllChunkCoords() && this->scaleLevel() < setting::MINIMUM_SCALE_LEVEL;
    }

    [[nodiscard]] inline qreal minimumZoomScale() const {
        if (level_loader_ && level_loader_->preloadAllChunkCoords()) return static_cast<qreal>(setting::MINIMUM_ZOOM_SCALE);
        return static_cast<qreal>(setting::MINIMUM_SCALE_LEVEL);
    }

   private:
    // for debug

    void drawDebugWindow(QPaintEvent *event, QPainter *p);

    // helper
    void drawImageInRegion(QPaintEvent *event, QPainter *p, const region_pos &pos, QImage *img) const;

    // function drawS
    void drawGrid(QPaintEvent *event, QPainter *p);

    void drawChunkPosText(QPaintEvent *event, QPainter *painter);

    void drawSlimeChunks(QPaintEvent *event, QPainter *p);

    void drawBiome(QPaintEvent *event, QPainter *p);

    void drawTerrain(QPaintEvent *event, QPainter *p);

    void drawCoordsBoundingBox(QPainter *p);

    bool modificationBlocked();

    void drawActors(QPaintEvent *event, QPainter *p);

    void drawHSAs(QPaintEvent *event, QPainter *p);

    void drawVillages(QPaintEvent *event, QPainter *p);

    void drawSelection(QPainter *p) { selection_.draw(p, scaleLevel()); }

    void drawOpenedChunkHighlight(QPainter *p);

   public:
    ~MapWidget() override;
    /// Capture the selected region as an image.
    /// For irregular selections, the bounding rectangle is used.
    /// @param scale  magnification factor (1.0 = original size, uses nearest-neighbor for pixel clarity)
    /// @return the captured image, or a null QImage if nothing is selected
    QImage captureSelectionToImage(double scale = 1.0);

   public slots:
    void saveSelectionImage();

    void saveFullscreenImage();

    void gotoPositionAction();

   private slots:
    // Menu & Action
    void showContextMenu(const QPoint &p);

    // Keyboard pan tick
    void onPanTick();

   private:
    // parent widget
    LevelPageWidget *level_page_{nullptr};

    // data source
    AsyncLevelLoader *level_loader_{nullptr};

    // selection
    SelectionController selection_;

    // operation control
    bool dragging_{false};
    bool capturing_{false};

    // gui
    GoToPositionDialog *goto_dialog_{nullptr};

    // render control
    RenderOption option_;
    bool draw_debug_window_{false};
    bool transparent_void_{false};
    QTransform world_to_view_xf_;
    QRect camera_{-10, -10, width() + 10, height() + 10};  // drawable range, later set to match the widget size
    QTimer *sync_refresh_timer_;

    // opened chunk
    bool opened_chunk_{false};
    bl::chunk_pos opened_chunk_pos_;

    // 3d
    VoxelPreviewWidget *voxel_preview_window_{nullptr};

    // import
    ImportOverlay *import_overlay_{nullptr};

    // keyboard pan
    QTimer *pan_timer_{nullptr};
    QSet<int> pressed_keys_;

    // exporter
    // ChunkOperator is static; no instance needed
};
#endif  // MAPWIDGET_H
