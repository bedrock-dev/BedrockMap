#include <vector>

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QKeyEvent>
#include <QMainWindow>
#include <QPushButton>
#include <unordered_map>

#include "chunkeditorwidget.h"
#include "mapwidget.h"
#include  <QFutureWatcher>
#include "nbtwidget.h"

QT_BEGIN_NAMESPACE
namespace Ui {
    class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
Q_OBJECT

    virtual void keyPressEvent(QKeyEvent *event) override;

public:
    MainWindow(QWidget *parent = nullptr);

    ~MainWindow();

    inline world *get_world() { return &this->world_; }

public slots:

    void updateXZEdit(int x, int z);
    // public

    void openChunkEditor(const bl::chunk_pos &p);

    void deleteChunks(const bl::chunk_pos &min, const bl::chunk_pos &max);

    void handle_chunk_delete_finished();

private slots:

    void on_goto_btn_clicked();

    void on_grid_checkbox_stateChanged(int arg1);

    void on_text_checkbox_stateChanged(int arg1);

    void open_level();

    void close_level();

    void close_and_exit();

    void on_debug_checkbox_stateChanged(int arg1);

    void toggle_chunk_edit_view();

    void toggle_full_map_mode();

    void on_enable_chunk_edit_check_box_stateChanged(int arg1);

    void on_screenshot_btn_clicked();

    void openNBTEditor();

    void on_refresh_cache_btn_clicked();

    void on_layer_slider_valueChanged(int value);

    void on_slime_layer_btn_clicked();

    void on_actor_layer_btn_clicked();

    void on_global_nbt_checkbox_stateChanged(int arg1);

   private:
    Ui::MainWindow *ui;
    std::unordered_map<MapWidget::MainRenderType, QPushButton *> layer_btns_;
    std::unordered_map<MapWidget::DimType, QPushButton *> dim_btns_;

    bool full_map_mode_{false};
    bool chunk_edit_widget_hided_{true};
    MapWidget *map_;
    ChunkEditorWidget *chunk_editor_widget_;
    world world_;

    bool write_mode_{false};

    //watcher
    QFutureWatcher<bool> delete_chunks_watcher_;

    //global nbt editors
    NbtWidget *level_dat_editor_;
    NbtWidget *player_editor_;
    NbtWidget *village_editor_;
};

#endif  // MAINWINDOW_H
