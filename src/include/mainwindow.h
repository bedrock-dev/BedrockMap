#include <QShortcut>
#include <vector>

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QDialog>
#include <QFutureWatcher>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <unordered_map>

#include "asynclevelloader.h"
#include "chunkeditorwidget.h"
#include "mapitemeditor.h"
#include "mapwidget.h"
#include "nbtwidget.h"
#include "renderfilterdialog.h"

QT_BEGIN_NAMESPACE
namespace Ui {
    class MainWindow;
}

struct LogoPos {
    int angle{0};
    double x{0.5};
    double y{0.5};

    LogoPos() {
        srand(time(nullptr));
        angle = rand() % 360;
        x = ((rand() % 100) / 100.0) * 0.6 + 0.2;
        y = ((rand() % 100) / 100.0) * 0.6 + 0.2;
    }
};

struct GlobalNBTLoadResult {
    bl::village_data villageData;
    bl::general_kv_nbts playerData;
    bl::general_kv_nbts mapData;
    bl::general_kv_nbts otherData;
};

QT_END_NAMESPACE

class MainWindow : public QMainWindow {
Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    ~MainWindow() override;

    AsyncLevelLoader *levelLoader() { return this->level_loader_; }

    MapWidget *mapWidget() { return this->map_widget_; }

public slots:

    inline bool enable_write() const { return this->write_mode_; }

    void updateXZEdit(int x, int z);

    // public
    bool openChunkEditor(const bl::chunk_pos &p);

    void deleteChunks(const bl::chunk_pos &min, const bl::chunk_pos &max);

    void handle_chunk_delete_finished();

    void handle_level_open_finished();

    inline QMap<QString, QRect> &get_villages() { return this->villages_; }

    void applyFilter();

protected:
    void paintEvent(QPaintEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;

private slots:

    void openLevel();

    void closeLevel();

    void close_and_exit();

    void openNBTEditor();

    void openMapItemEditor();

    void on_slime_layer_btn_clicked();

    void on_actor_layer_btn_clicked();

    void on_save_leveldat_btn_clicked();

    void on_save_village_btn_clicked();

    void on_save_players_btn_clicked();

    void on_village_layer_btn_clicked();

    void on_hsa_layer_btn_clicked();

    void on_edit_filter_btn_clicked();

    void resetToInitUI();

    void on_grid_btn_clicked();

    void on_coord_btn_clicked();

    void on_global_data_btn_clicked();

    void collect_villages(const std::unordered_map<std::string, std::array<bl::palette::compound_tag *, 4>> &vs);

    void on_save_other_btn_clicked();

    void prepareGlobalData(GlobalNBTLoadResult &result);

    void setupShortcuts();

private:
    QString getStaticTitle();

private:
    Ui::MainWindow *ui;
    std::unordered_map<MapWidget::MainRenderType, QPushButton *> layer_btns_;
    std::unordered_map<MapWidget::DimType, QPushButton *> dim_btns_;

    MapWidget *map_widget_;
    ChunkEditorWidget *chunk_editor_widget_;
    // data source
    AsyncLevelLoader *level_loader_{nullptr};

    bool write_mode_{false};

    // watcher
    QFutureWatcher<bool> delete_chunks_watcher_;
    QFutureWatcher<bool> load_global_data_watcher_;

    // global nbt editors
    NbtWidget *level_dat_editor_;
    NbtWidget *player_editor_;
    NbtWidget *village_editor_;
    NbtWidget *other_nbt_editor_;

    MapItemEditor *map_item_editor_;

    // global data
    QMap<QString, QRect> villages_;
    // filter
    RenderFilterDialog render_filter_dialog_{this};
    LogoPos logoPos{};

    // loading global data?
    std::atomic_bool loading_global_data_{false};
    std::atomic_bool global_data_loaded_{false};

    // Shortcuts
    std::vector<QShortcut *> shortcuts_;
};

#endif  // MAINWINDOW_H
