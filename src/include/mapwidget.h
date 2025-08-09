#ifndef MAPWIDGET_H
#define MAPWIDGET_H

#include <QObject>
#include <QPaintEvent>
#include <QTimer>
#include <QWidget>
#include <QtDebug>
#include <tuple>

#include "bedrock_key.h"
#include "config.h"

class MainWindow;

class MapWidget : public QWidget {
    Q_OBJECT

   public:
    enum MainRenderType { Biome = 0, Terrain = 1, Height = 2 };

    enum DimType { OverWorld = 0, Nether = 1, TheEnd = 2 };

    MapWidget(MainWindow *w, QWidget *parent) : QWidget(parent), mw_(w) {
        this->sync_refresh_timer_ = new QTimer();
        connect(this->sync_refresh_timer_, SIGNAL(timeout()), this, SLOT(asyncRefresh()));
        this->sync_refresh_timer_->start(100);
        setMouseTracking(true);
        this->setContextMenuPolicy(Qt::CustomContextMenu);
        setFocusPolicy(Qt::FocusPolicy::StrongFocus);
    }

    void paintEvent(QPaintEvent *event) override;

    // mouse event
    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;

    bl::block_pos getCursorBlockPos();

   public:
    inline void changeDimension(DimType dim) {
        this->dim_type_ = dim;
        this->update();
    }

    inline void changeLayer(MainRenderType layer) {
        this->main_render_type_ = layer;
        this->update();
    }

    inline bool toggleSlime() {
        this->draw_slime_chunk_ = !this->draw_slime_chunk_;
        return this->draw_slime_chunk_;
    }

    inline bool toggleGrid() {
        this->draw_grid_ = !this->draw_grid_;
        return this->draw_grid_;
    }

    inline bool toggleCoords() {
        this->draw_coords_ = !this->draw_coords_;
        return this->draw_coords_;
    }

    inline bool toggleActor() {
        this->draw_actors_ = !this->draw_actors_;
        return this->draw_actors_;
    }

    inline bool toggleVillage() {
        this->draw_villages_ = !this->draw_villages_;
        return this->draw_villages_;
    }

    inline bool toggleHSAs() {
        this->draw_HSA_ = !this->draw_HSA_;
        return this->draw_HSA_;
    }

    inline void setDrawDebug(bool enable) { this->draw_debug_window_ = enable; }

    // 生成图片
    void saveImageAction(bool full_screen);

    // 前往坐标
    void gotoPositionAction();

    inline void selectChunk(const bl::chunk_pos &p) { this->opened_chunk_ = true, this->opened_chunk_pos_ = p; }

    inline void unselectChunk() {
        qDebug() << "Unselected";
        this->opened_chunk_ = false;
    }

    void advancePos(int x, int y);

   signals:

    void mouseMove(int x, int z);  // NOLINT

   public slots:

    void asyncRefresh();

    // https://stackoverflow.com/questions/24254006/rightclick-event-in-qt-to-open-a-context-menu
    void showContextMenu(const QPoint &p);

    void gotoBlockPos(int x, int z);

    inline void focusOnCursor() {
        auto p = this->getCursorBlockPos();
        gotoBlockPos(p.x, p.z);
    }

    void delete_chunks();

   private:
    [[nodiscard]] inline qreal BW() const { return static_cast<qreal>(this->cw_) / 16.0; }

   private:
    // for debug

    void drawDebugWindow(QPaintEvent *event, QPainter *p);

    void drawRegion(QPaintEvent *event, QPainter *p, const region_pos &pos, const QPoint &start, QImage *img) const;

    void forEachChunkInCamera(const std::function<void(const bl::chunk_pos &, const QPoint &)> &f);

    void foreachRegionInCamera(const std::function<void(const region_pos &p, const QPoint &)> &f);

    // function draw

    void drawGrid(QPaintEvent *event, QPainter *p);

    void drawChunkPosText(QPaintEvent *event, QPainter *painter);

    void drawSlimeChunks(QPaintEvent *event, QPainter *p);

    void drawBiome(QPaintEvent *event, QPainter *p);

    void drawTerrain(QPaintEvent *event, QPainter *p);

    void drawHeight(QPaintEvent *event, QPainter *p);

    void drawActors(QPaintEvent *event, QPainter *p);

    void drawHSAs(QPaintEvent *event, QPainter *p);

    void drawSelectArea(QPaintEvent *event, QPainter *p);

    void drawVillages(QPaintEvent *event, QPainter *p);

    void drawMarkers(QPaintEvent *event, QPainter *p);

    QRect getRenderSelectArea();
    // 给定窗口，计算该区域内需要渲染的所有区块的坐标数据以及渲染范围的坐标

    std::tuple<bl::chunk_pos, bl::chunk_pos, QRect> getRenderRange(const QRect &camera);

    ~MapWidget() override;

   signals:

   private:
    // objects

    // bl::chunk_pos spawn{0, 0};  // origin 处要会绘制的区块坐标
    // select area
    bl::chunk_pos select_pos_1_;
    bl::chunk_pos select_pos_2_;
    bool has_selected_{false};

    // operation control
    bool dragging_{false};
    bool selecting_{false};
    //

    MainWindow *mw_{nullptr};
    // render control
    QRect camera_{0, 0, width(), height()};  // 需要绘制的范围，后面设置成和widget等大即可
    DimType dim_type_{DimType::OverWorld};
    MainRenderType main_render_type_{MainRenderType::Terrain};
    QTimer *sync_refresh_timer_;
    // extra layer
    bool draw_slime_chunk_{false};
    bool draw_actors_{false};
    bool draw_villages_{false};
    bool draw_HSA_{false};

    int cw_{64};           // 每个区块需要几个像素
    QPoint origin_{0, 0};  // 记录区块0,0的左上角相对widget左上角的坐标
    bool draw_grid_{true};
    bool draw_coords_{false};
    bool draw_debug_window_{false};

    // opened chunk
    bool opened_chunk_{false};
    bl::chunk_pos opened_chunk_pos_;
};

#endif  // MAPWIDGET_H
