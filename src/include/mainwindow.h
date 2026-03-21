#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <qboxlayout.h>
#include <qchar.h>
#include <qdialog.h>
#include <qfont.h>
#include <qlabel.h>
#include <qwidget.h>
#include <qwindowdefs.h>

#include <QDialog>
#include <QFutureWatcher>
#include <QKeyEvent>
#include <QLayout>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>
#include <string>
#include <unordered_map>
#include <vector>

#include "aboutdialog.h"
#include "asynclevelloader.h"
#include "chunkeditorwidget.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
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

struct VillageDrawInfo {
    QRect rect;
    int dim{0};
};

class LevelDBDebugDialog : public QDialog {
   public:
    LevelDBDebugDialog(QWidget *widget) : QDialog(widget) {
        setWindowTitle("LevelDB Stats");
        setFont(QFont("Consolas"));
        label = new QLabel(this);
        layout = new QVBoxLayout(this);
        layout->addWidget(label);

        this->setLayout(layout);
    }

    void initData(leveldb::DB *db) {
        QStringList data;
        if (db) {
            data << QString("Last Sequence Number:     %1").arg(db->LastSequence());

            uint64_t disk;
            leveldb::Range range(leveldb::Slice(""), leveldb::Slice("\xff\xff\xff\xff"));
            db->GetApproximateSizes(&range, 1, &disk);
            data << QString("Approximate Disk Size:    %1 bytes (%2 MiB)").arg(disk).arg(disk / 1024.0 / 1024.0);

            std::string memStr;
            db->GetProperty("leveldb.approximate-memory-usage", &memStr);
            auto mem = QString::fromStdString(memStr).toUInt();
            data << QString("Approximate Memory Size:  %1 bytes (%2 MiB)").arg(mem).arg(mem / 1024.0 / 1024.0);

            std::string stats;
            db->GetProperty("leveldb.stats", &stats);
            data << QString::fromStdString(stats);
        } else {
            data << "LevelDB not opened";
        }
        label->setText(data.join("\n"));
    }

   private:
    QLabel *label{nullptr};
    QVBoxLayout *layout{nullptr};
};

QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

   public:
    explicit MainWindow(QWidget *parent = nullptr);

   private:
    void setupMenuActions();
    void setupMapWidget();
    void setupToolBar();
    void setupGlobalNBTWidget();

   public:
    ~MainWindow() override;

    // getter
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

    inline QMap<QString, VillageDrawInfo> &get_villages() { return this->villages_; }

    void applyFilter();

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

    void collect_villages(const bl::village_data::village_table_type &vs);

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

    // Dialog
    AboutDialog *about_dialog_{nullptr};
    LevelDBDebugDialog *leveldb_dialog_{nullptr};

    MapItemEditor *map_item_editor_;

    // global data
    QMap<QString, VillageDrawInfo> villages_;
    // filter
    RenderFilterDialog render_filter_dialog_{this};
    LogoPos logoPos{};

    std::atomic_bool stop_loading_global_data_{true};
    std::atomic_bool global_data_loaded_{false};

    // Shortcuts
    std::vector<QShortcut *> shortcuts_;
};

#endif  // MAINWINDOW_H
