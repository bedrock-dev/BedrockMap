#ifndef MAPWIDGET_H
#define MAPWIDGET_H

#include <QObject>
#include <QPaintEvent>
#include <QWidget>
#include <tuple>

#include "bedrock_key.h"
#include "world.h"

class MapWidget : public QWidget {
    Q_OBJECT
   public:
    explicit MapWidget(QWidget *parent = nullptr);

    void paintEvent(QPaintEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;

   private:
    // for debug

    void debugDrawCamera(QPaintEvent *evet);

    void debugDrawGrid(QPaintEvent *event);

    //给定窗口，计算该区域内需要渲染的所有区块的坐标数据以及渲染范围的坐标

    std::tuple<bl::chunk_pos, bl::chunk_pos, QRect> getRenderRange(
        const QRect &camera);

   signals:

   private:
    int bw{6};             //每个方块需要几个像素
    QPoint origin{0, 0};   //记录区块0,0相对widget左上角的坐标
    bl::chunk_pos spawn{0, 0};  // orgin 处要会绘制的区块坐标
    bool dragging{false};
    QRect camera{0, 0, width(),
                 height()};  //需要绘制的范围，后面设置成和widget等大即可
    world *world{nullptr};
};

#endif  // MAPWIDGET_H
