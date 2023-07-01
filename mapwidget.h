#ifndef MAPWIDGET_H
#define MAPWIDGET_H

#include <QObject>
#include <QPaintEvent>
#include <QTimer>
#include <QWidget>
#include <QtDebug>
#include <array>
#include <tuple>

#include "bedrock_key.h"
#include "world.h"

class MainWindow;


class MapWidget : public QWidget {
Q_OBJECT

public:
    enum MainRenderType {
        Biome = 0, Terrain = 1, Height = 2
    };


    enum DimType {
        OverWorld = 0, Nether = 1, TheEnd = 2
    };

    MapWidget(MainWindow *w, QWidget *parent) : QWidget(parent), mw_(w) {
        this->sync_refresh_timer_ = new QTimer();
        connect(this->sync_refresh_timer_, SIGNAL(timeout()), this,
                SLOT(asyncRefresh()));
        this->sync_refresh_timer_->start(100);
        setMouseTracking(true);
        this->setContextMenuPolicy(Qt::CustomContextMenu);
        setFocusPolicy(Qt::FocusPolicy::StrongFocus);
    }

    void paintEvent(QPaintEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

    void keyPressEvent(QKeyEvent *event) override;

    void keyReleaseEvent(QKeyEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

    // void mouseDoubleClickEvent(QMouseEvent *event) override;

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

    inline void enableGrid(bool able) {
        qDebug() << able;
        this->render_grid_ = able;
        this->update();
    }

    inline void enableText(bool able) {
        this->render_text_ = able;
        this->update();
    }

    inline void enableDebug(bool able) {
        this->render_debug_ = able;
        this->update();
    }

    inline void toggleSlime() {
        this->draw_slime_chunk_ = !this->draw_slime_chunk_;
    }


    void saveImage(bool full);

signals:

    void mouseMove(int x, int z); //NOLINT

public slots:

    void asyncRefresh();

    // https://stackoverflow.com/questions/24254006/rightclick-event-in-qt-to-open-a-context-menu
    void showContextMenu(const QPoint &p);

    void gotoBlockPos(int x, int z);

    inline void focusOnCursor() {
        auto p = this->getCursorBlockPos();
        gotoBlockPos(p.x, p.z);
    }

    void openChunkEditor();


private:
    // for debug

    void drawDebugWindow(QPaintEvent *event, QPainter *p);

    // draw chunk help
    void
    drawOneChunk(QPaintEvent *event, QPainter *p, const bl::chunk_pos &pos, const QPoint &start, QImage *img) const;

    void drawRegion(QPaintEvent *event, QPainter *p, const region_pos &pos, const QPoint &start, QImage *img) const;

    void forEachChunkInCamera(const std::function<void(const bl::chunk_pos &, const QPoint &)> &f);

    void foreachRegionInCamera(const std::function<void(const region_pos &p, const QPoint &)> &f);

    // function draw

    void drawGrid(QPaintEvent *event, QPainter *p);

    void drawChunkPos(QPaintEvent *event, QPainter *p);

    void drawSlimeChunks(QPaintEvent *event, QPainter *p);

    void drawBiome(QPaintEvent *event, QPainter *p);

    void drawTerrain(QPaintEvent *event, QPainter *p);

    void drawHeight(QPaintEvent *event, QPainter *p);

    void drawSelectArea(QPaintEvent *event, QPainter *p);


    QRect getRenderSelectArea();
    //给定窗口，计算该区域内需要渲染的所有区块的坐标数据以及渲染范围的坐标

    std::tuple<bl::chunk_pos, bl::chunk_pos, QRect> getRenderRange(const QRect &camera);

signals:

private:
    // bl::chunk_pos spawn{0, 0};  // origin 处要会绘制的区块坐标

    // select area
    bl::chunk_pos select_min_;
    bl::chunk_pos select_max_;
    bool has_selected_{false};

    // operation control
    bool dragging_{false};
    bool CTRL_pressed_{false};
    //

    MainWindow *mw_{nullptr};
    // render control
    QRect camera_{0, 0, width(), height()};  //需要绘制的范围，后面设置成和widget等大即可
    DimType dim_type_{DimType::OverWorld};
    MainRenderType main_render_type_{MainRenderType::Biome};
    QTimer *sync_refresh_timer_;
    //extra layer
    bool draw_slime_chunk_{false};


    int bw_{6};            //每个方块需要几个像素
    QPoint origin_{0, 0};  //记录区块0,0相对widget左上角的坐标
    bool render_grid_{true};
    bool render_text_{true};
    bool render_debug_{false};

};


#endif  // MAPWIDGET_H
