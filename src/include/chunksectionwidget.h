#ifndef CHUNKSECTIONWIDGET_H
#define CHUNKSECTIONWIDGET_H

#include <QWidget>

#include "chunk.h"

struct TerrainData {
    std::string block_palette{};
    std::string block_name{};
    bl::color block_color{0, 0, 0, 255};
    bl::biome biome{bl::none};
};

class ChunkSectionWidget : public QWidget {
Q_OBJECT
public:

    explicit ChunkSectionWidget(QWidget *parent = nullptr);

    void paintEvent(QPaintEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;

    inline void setYLevel(int y) {
        this->y_level_ = y;
        this->update();
    }

    void load_data(bl::chunk *ch);


    int get_block_pix();

    void mouseReleaseEvent(QMouseEvent *event) override;


    void showContextMenu(const QPoint &p);


private:

    inline std::array<std::array<TerrainData, 16>, 16> &get_layer_data(int y) {
        return this->data_[y + 64];
    }


signals:
private:
    int y_level_{0};
    std::array<std::array<std::array<TerrainData, 16>, 16>, 384> data_;
};

#endif // CHUNKSECTIONWIDGET_H
