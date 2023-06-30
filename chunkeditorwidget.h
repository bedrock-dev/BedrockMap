#ifndef CHUNKEDITORWIDGET_H
#define CHUNKEDITORWIDGET_H

#include <QWidget>

#include "chunk.h"

namespace Ui {

    class ChunkEditorWidget;

}  // namespace Ui
class ChunkSectionWidget;

class NbtWidget;

class ChunkEditorWidget : public QWidget {
Q_OBJECT

public:
    explicit ChunkEditorWidget(QWidget *parent = nullptr);

    ~ChunkEditorWidget() override;

    void load_chunk_data(bl::chunk *chunk);

private slots:

    void on_close_btn_clicked();

    void on_terrain_level_slider_valueChanged(int value);

    void on_terrain_goto_level_btn_clicked();

private:
    void refreshData();

private:
    ChunkSectionWidget *chunk_section_{nullptr};

    NbtWidget *actor_editor_{nullptr};
    NbtWidget *block_entity_editor_{nullptr};
    NbtWidget *pending_tick_editor_{nullptr};

    Ui::ChunkEditorWidget *ui;
    int y_level{0};

    bl::chunk *chunk_{nullptr};
};

#endif  // CHUNKEDITORWIDGET_H
