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

class MapWidget : public QWidget {
    Q_OBJECT

   public:
    enum LayerType { Biome = 0, Terrain = 1, Slime = 2, Height = 3 };

    enum DimType { OverWorld = 0, Nether = 1, TheEnd = 2 };

    MapWidget(world *w, QWidget *parent) : QWidget(parent), world(w) {
        this->asyncRefreshTimer = new QTimer();
        connect(this->asyncRefreshTimer, SIGNAL(timeout()), this,
                SLOT(asyncRefresh()));
        this->asyncRefreshTimer->start(100);
        setMouseTracking(true);
    }

    void paintEvent(QPaintEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;

    void gotoBlockPos(int x, int z);

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

   signals:
    void mouseMove(int x, int z);

   public slots:
    void asyncRefresh();

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

    //给定窗口，计算该区域内需要渲染的所有区块的坐标数据以及渲染范围的坐标

    std::tuple<bl::chunk_pos, bl::chunk_pos, QRect> getRenderRange(
        const QRect &camera);

   signals:

   private:
    QTimer *asyncRefreshTimer;

    int bw{6};                  //每个方块需要几个像素
    QPoint origin{0, 0};        //记录区块0,0相对widget左上角的坐标
    bl::chunk_pos spawn{0, 0};  // orgin 处要会绘制的区块坐标
    bool dragging{false};
    QRect camera{0, 0, width(), height()};  //需要绘制的范围，后面设置成和widget等大即可
    world *world{nullptr};

    DimType dimType{DimType::OverWorld};
    LayerType layeType{LayerType::Biome};

    // render control
    bool render_grid{true};
    bool render_text{true};
};

#endif  // MAPWIDGET_H
