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
#include <QDialog>
#include "asynclevelloader.h"
#include <QMessageBox>

QT_BEGIN_NAMESPACE
namespace Ui {
    class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);

    ~MainWindow();


    AsyncLevelLoader *levelLoader() { return this->level_loader_; }

public slots:

    inline bool enable_write() const { return this->write_mode_; }

    void updateXZEdit(int x, int z);

    // public
    void openChunkEditor(const bl::chunk_pos &p);

    void deleteChunks(const bl::chunk_pos &min, const bl::chunk_pos &max);

    void handle_chunk_delete_finished();

    void handle_level_open_finished();

    void refreshTitle();

    inline QMap<QString, QRect> &get_villages() { return this->villages_; }

private slots:

    void on_goto_btn_clicked();

    void on_grid_checkbox_stateChanged(int arg1);

    void on_text_checkbox_stateChanged(int arg1);

    void openLevel();

    void closeLevel();

    void close_and_exit();

    void on_debug_checkbox_stateChanged(int arg1);

    void toggle_chunk_edit_view();

//    void toggle_full_map_mode();

    void on_enable_chunk_edit_check_box_stateChanged(int arg1);

    void on_screenshot_btn_clicked();

    void openNBTEditor();

    void on_refresh_cache_btn_clicked();

    void on_layer_slider_valueChanged(int value);

    void on_slime_layer_btn_clicked();

    void on_actor_layer_btn_clicked();

    void on_global_nbt_checkbox_stateChanged(int arg1);

    void on_save_leveldat_btn_clicked();

    void on_save_village_btn_clicked();

    void on_save_players_btn_clicked();


    //
    void collect_villages(const std::unordered_map<std::string, std::array<bl::palette::compound_tag *, 4>> &vs);


private:
    Ui::MainWindow *ui;
    std::unordered_map<MapWidget::MainRenderType, QPushButton *> layer_btns_;
    std::unordered_map<MapWidget::DimType, QPushButton *> dim_btns_;


    MapWidget *map_widget_;
    ChunkEditorWidget *chunk_editor_widget_;
    //data source
    AsyncLevelLoader *level_loader_{nullptr};

    bool write_mode_{false};

    //watcher
    QFutureWatcher<bool> delete_chunks_watcher_;
    QFutureWatcher<bool> open_level_watcher_;

    //global nbt editors
    NbtWidget *level_dat_editor_;
    NbtWidget *player_editor_;
    NbtWidget *village_editor_;

    QDialog *open_level_dialog_{nullptr};

    //global data
    QMap<QString, QRect> villages_;
    //dialogs
};

#endif  // MAINWINDOW_H
