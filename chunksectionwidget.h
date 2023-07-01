#ifndef CHUNKSECTIONWIDGET_H
#define CHUNKSECTIONWIDGET_H

#include <QWidget>

#include "chunk.h"

class ChunkSectionWidget : public QWidget {
Q_OBJECT
public:
    enum DrawType {
        Biome, Terrain
    };

    explicit ChunkSectionWidget(QWidget *parent = nullptr);

    void paintEvent(QPaintEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;

    void setDrawType(DrawType t) {
        this->dt_ = t;
        this->update();
    }

    void setYLevel(int y) {
        this->y_level_ = y;
        this->update();
    }

    void set_chunk(bl::chunk *ch) { this->ch_ = ch; }

    int get_block_pix();


signals:
private:
    DrawType dt_{Biome};
    int y_level_{0};
    bl::chunk *ch_{nullptr};
};

#endif // CHUNKSECTIONWIDGET_H
