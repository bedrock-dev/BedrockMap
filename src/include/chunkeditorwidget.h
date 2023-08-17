#ifndef CHUNKEDITORWIDGET_H
#define CHUNKEDITORWIDGET_H

#include <QWidget>

#include "chunk.h"

namespace Ui {

    class ChunkEditorWidget;

}  // namespace Ui
class ChunkSectionWidget;

class NbtWidget;

class MainWindow;

class ChunkEditorWidget : public QWidget {
    Q_OBJECT
   public:
    explicit ChunkEditorWidget(MainWindow *mw, QWidget *parent = nullptr);

    ~ChunkEditorWidget() override;

    void loadChunkData(bl::chunk *chunk);

    void mousePressEvent(QMouseEvent *event) override;

    void clearData();

   private slots:

    void on_close_btn_clicked();

    void on_terrain_level_slider_valueChanged(int value);

    void on_terrain_goto_level_btn_clicked();

    void on_save_actor_btn_clicked();

    void on_save_block_actor_btn_clicked();

    void on_save_pt_btn_clicked();

    void on_locate_btn_clicked();

   private:
    void refreshBasicData();

   private:
    ChunkSectionWidget *chunk_section_{nullptr};

    NbtWidget *actor_editor_{nullptr};
    NbtWidget *block_entity_editor_{nullptr};
    NbtWidget *pending_tick_editor_{nullptr};

    Ui::ChunkEditorWidget *ui;
    int y_level{0};
    //    bl::chunk *chunk_{nullptr};
    bl::ChunkVersion cv{bl::Old};
    bl::chunk_pos cp_;
    MainWindow *mw_;
};

#endif  // CHUNKEDITORWIDGET_H
