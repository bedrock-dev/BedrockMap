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
    enum LayerType { Biome = 0, Terrain = 1, Slime = 2, Height = 3 };

    enum DimType { OverWorld = 0, Nether = 1, TheEnd = 2 };

    MapWidget(MainWindow *w, QWidget *parent) : QWidget(parent), mw_(w) {
        this->asyncRefreshTimer = new QTimer();
        connect(this->asyncRefreshTimer, SIGNAL(timeout()), this,
                SLOT(asyncRefresh()));
        this->asyncRefreshTimer->start(100);
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
    inline void changeDimemsion(DimType dim) {
        this->dimType = dim;
        this->update();
    }

    inline void changeLayer(LayerType layer) {
        this->layeType = layer;
        this->update();
    }

    inline void enableGrid(bool able) {
        qDebug() << able;
        this->render_grid = able;
        this->update();
    }
    inline void enableText(bool able) {
        this->render_text = able;
        this->update();
    }
    inline void enableDebug(bool able) {
        this->render_debug = able;
        this->update();
    }

    void saveImage(const QRect &rect);

   signals:
    void mouseMove(int x, int z);

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

    void debugDrawCamera(QPaintEvent *event, QPainter *p);

    // draw chunk help
    void drawOneChunk(QPaintEvent *event, QPainter *p, const bl::chunk_pos &pos,
                      const QPoint &start, QImage *img);
    void forEachChunkInCamera(
        const std::function<void(const bl::chunk_pos &, const QPoint &)> &f);
    // functio draw

    void drawGrid(QPaintEvent *event, QPainter *p);

    void drawChunkPos(QPaintEvent *event, QPainter *p);

    void drawSlimeChunks(QPaintEvent *event, QPainter *p);

    void drawBiome(QPaintEvent *event, QPainter *p);

    void drawTerrain(QPaintEvent *event, QPainter *p);

    void drawHeight(QPaintEvent *event, QPainter *p);

    void drawSelectArea(QPaintEvent *event, QPainter *p);

    QRect getRenderSelectArea();
    //给定窗口，计算该区域内需要渲染的所有区块的坐标数据以及渲染范围的坐标

    std::tuple<bl::chunk_pos, bl::chunk_pos, QRect> getRenderRange(
        const QRect &camera);

   signals:

   private:
    // bl::chunk_pos spawn{0, 0};  // orgin 处要会绘制的区块坐标

    // select area
    bl::chunk_pos select_min;
    bl::chunk_pos select_max;
    bool select{false};

    // operation control
    bool dragging{false};
    bool control_pressed{false};
    //

    MainWindow *mw_{nullptr};
    // render control
    QRect camera{0, 0, width(), height()};  //需要绘制的范围，后面设置成和widget等大即可
    DimType dimType{DimType::OverWorld};
    LayerType layeType{LayerType::Biome};
    QTimer *asyncRefreshTimer;

    int bw{6};            //每个方块需要几个像素
    QPoint origin{0, 0};  //记录区块0,0相对widget左上角的坐标

    bool render_grid{true};

    bool render_text{true};
    bool render_debug{false};

    // function control
};

#endif  // MAPWIDGET_H
