#ifndef LEVEL_WIDGET_H
#define LEVEL_WIDGET_H
#include <qlabel.h>
#include <qobject.h>
#include <qtabwidget.h>
#include <qtmetamacros.h>
#include <qwidget.h>

#include <QObject>
#include <QWidget>
#include <atomic>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "chunkeditorwidget.h"
#include "floatingtoolbar.h"
#include "mapitemeditor.h"
#include "mapwidget.h"
#include "nbtwidget.h"
#include "renderfilterdialog.h"

class LevelStatusBar : public QWidget {
    Q_OBJECT

   public:
    LevelStatusBar(QWidget *parent);

    void setStatus(const QString &status) { status_msg_->setText(status); }
    void setSelectionInfo(int count);
    void setModifyInfo(int modified, int deleted);

   public slots:
    void onPosChanged(int x, int z, int dim);

   private:
    QLabel *pos_;
    QLabel *status_msg_;
    QLabel *sel_info_;
    QLabel *modify_info_;
};
class LevelTabWidget;
class LevelPageWidget : public QWidget {
    struct VillageDrawInfo {
        bl::block_pos p1;
        bl::block_pos p2;
        int dim{0};
    };

    Q_OBJECT

   public:
    LevelPageWidget(LevelTabWidget *parent, int id);

    ~LevelPageWidget() override;

    // ui setup
    void setupMapWidget();
    void setupToolBar();
    void setupSelectionToolBar();
    void setupDataWidget();

    // getter
    inline int getTabId() const { return this->tab_id_; }
    inline MapWidget *getMapWidget() { return this->mapWidget_; }
    AsyncLevelLoader *levelLoader() { return this->level_loader_.get(); }
    const QMap<QString, VillageDrawInfo> &getVillages() const { return this->villages_; }
    bool isDirty() const;

    QString getLevelName();
    bool loadLevel(const QString &path);
    void closeLevel();
    void toggleGlobalDataWidget();
    void openFilterDialog();
    void showChunkEditor(const bl::chunk_pos &pos);
    void syncToolbars();

    void setToolBarsVisible(bool visible) {
        if (toolbar_) toolbar_->setVisible(visible);
        if (selection_toolbar_) selection_toolbar_->setVisible(visible);
    }

    void refreshDirty();
    void commit();

   private:
    // data
    void collectVillagesGuiData(const bl::village_data::village_table_type &vs);
    void fillGlobalData(GlobalNBTLoadResult &result);

    // gui
    void setLevelStatusBar(const QString &status) { status_bar_->setStatus(status); }

   private slots:
    void onLoadGlobalDataFinished();

   private:
    // data source
    std::unique_ptr<AsyncLevelLoader> level_loader_;

    // global data
    QFutureWatcher<bool> load_global_data_watcher_;
    std::atomic_bool stop_loading_global_data_{false};
    GlobalNBTLoadResult global_data_;
    QMap<QString, VillageDrawInfo> villages_;

    // GUI
    LevelTabWidget *parent_;
    // map view
    MapWidget *mapWidget_;
    QSplitter *mainSplitter_;
    QSplitter *vertSplitter_;
    FloatingToolBar *toolbar_{nullptr};
    FloatingToolBar *selection_toolbar_{nullptr};
    QTabWidget *nbtTabWidget_;
    ChunkEditorWidget *chunkWidget_{nullptr};
    // nbt editor tabs
    NbtWidget *level_dat_editor_;
    NbtWidget *player_editor_;
    NbtWidget *village_editor_;
    NbtWidget *other_nbt_editor_;
    MapItemEditor *map_item_editor_;
    // status bar
    LevelStatusBar *status_bar_;
    // id in tabwidget
    // filter dialog
    RenderFilterDialog *render_filter_dialog_{nullptr};
    // toolbar group indices
    int tb_view_grp_{-1};
    int tb_dim_grp_{-1};
    int tb_layer_grp_{-1};
    int tb_overlay_grp_{-1};
    int sel_grp_{-1};

    const int tab_id_{-1};
};
#endif
