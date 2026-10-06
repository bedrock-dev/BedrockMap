#ifndef BEDROCKMAP_LEVELPAGEWIDGET_H
#define BEDROCKMAP_LEVELPAGEWIDGET_H
#include <qlabel.h>
#include <qobject.h>
#include <qtabwidget.h>
#include <qtmetamacros.h>
#include <qwidget.h>

#include <QObject>
#include <QSplitter>
#include <QWidget>
#include <atomic>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "chunkcoordsprogresswidget.h"
#include "chunkeditorwidget.h"
#include "cpumapwidget.h"
#include "floatingtoolbar.h"
#include "gpumapwidget.h"
#include "guitaskrunner.h"
#include "maphost.h"
#include "mapitemeditor.h"
#include "nbtwidget.h"
#include "renderfilterdialog.h"
#include "tabpagewidget.h"

class LevelStatusBar : public QWidget {
    Q_OBJECT

   public:
    LevelStatusBar(QWidget* parent);

    void setStatus(const QString& status) { status_msg_->setText(status); }
    void setSelectionInfo(int count);
    void setModifyInfo(int modified, int deleted);

   public slots:
    void onPosChanged(int x, int z, int dim);

   private:
    QLabel* pos_;
    QLabel* status_msg_;
    QLabel* sel_info_;
    QLabel* modify_info_;
};
class LevelTabWidget;
class QStackedWidget;
class VoxelWidget;

/// Read-only global structure viewer: NBT on the left and a voxel preview on
/// the right. The NBT editor remains exposed so LevelPageWidget can reuse the
/// existing dirty/commit handling for global data.
class StructureEditorWidget : public QWidget {
    Q_OBJECT

   public:
    explicit StructureEditorWidget(QWidget* parent = nullptr);

    [[nodiscard]] NbtWidget* nbtEditor() const { return nbt_editor_; }
    void loadStructureData(const bl::general_kv_nbts& data);
    void clearData();

   private slots:
    void onItemOpened(NBTListItem* item);

   private:
    NbtWidget* nbt_editor_{nullptr};
    VoxelWidget* voxel_widget_{nullptr};
    QStackedWidget* preview_stack_{nullptr};
    QLabel* preview_status_{nullptr};
};

class LevelPageWidget : public TabPageWidget {
    Q_OBJECT

   public:
    LevelPageWidget(LevelTabWidget* parent, int id);

    ~LevelPageWidget() override;

    // ui setup
    void setupMapPane();
    /// Both toolbars are built on the map pane. The state they show lives on the
    /// shared view, so whichever renderer is enabled, the same set drives it.
    void setupToolBar();
    void setupSelectionToolBar();
    void setupDataWidget();

    /// Selection mode is shared state, so it lives on the shared view rather
    /// than on a toolbar.
    void applySelectionMode(SelectionController::Mode mode);

    // getter
    inline int getTabId() const { return this->tab_id_; }
    /// The shared map state and the level operations behind it. Not a widget:
    /// the pane on screen is one of the renderers below.
    [[nodiscard]] MapHost* mapHost() { return this->map_host_; }
    /// The pane that is on screen: the GPU renderer when the setting asks for
    /// it, the CPU one otherwise. Only the configured renderer is constructed,
    /// so this is never a hidden widget.
    [[nodiscard]] QWidget* activeMapPane() const;
    [[nodiscard]] CpuMapWidget* cpuMapPane() const { return this->cpu_pane_; }
    AsyncLevelLoader* levelLoader() { return this->level_loader_.get(); }
    const QMap<QString, VillageDrawInfo>& getVillages() const { return this->villages_; }
    bool isDirty() const override;

    QString getLevelName();
    bool loadLevel(const QString& path);
    void closeLevel();
    void toggleGlobalDataWidget();
    void openFilterDialog();
    void showChunkEditor(const bl::chunk_pos& pos);
    void syncToolbars();

    void setToolBarsVisible(bool visible) {
        if (toolbar_) toolbar_->setVisible(visible);
        if (selection_toolbar_) selection_toolbar_->setVisible(visible);
        // The selection is part of the chrome: it must stay out of a screenshot,
        // which is the only thing that hides the toolbars on the map.
        if (map_host_) map_host_->mapView()->setSelectionVisible(visible);
    }

    void refreshDirty();
    bool commit() override;

   signals:
    void commitFinished(bool success);

   private:
    // data
    void collectVillagesGuiData(const bl::village_data::village_table_type& vs);
    void fillGlobalData(GlobalNBTLoadResult& result);

    // gui
    void setLevelStatusBar(const QString& status) { status_bar_->setStatus(status); }

   private slots:
    void onLoadGlobalDataFinished();
    void onLoadGlobalDataFailed(const QString& error);
    void onChunkCoordsPreloadFinished();
    void onCommitFinished();
    void onCommitFailed(const QString& error);
    /// Selection count in the status bar. Either renderer reports selection
    /// changes, so the count comes from the shared selection, not from the sender.
    void refreshSelectionInfo();

   private:
    // data source
    std::unique_ptr<AsyncLevelLoader> level_loader_;

    // global data
    GuiTaskRunner global_data_task_;
    GuiTaskRunner commit_task_;
    std::unique_ptr<bl::nbt::compound_tag> pending_level_dat_;
    std::unordered_map<std::string, std::string> pending_global_modifies_;
    std::atomic_bool stop_loading_global_data_{false};
    GlobalNBTLoadResult global_data_;
    QMap<QString, VillageDrawInfo> villages_;

    // GUI
    LevelTabWidget* parent_;
    // map: one host for the shared state, one renderer for what is drawn
    MapHost* map_host_{nullptr};
    CpuMapWidget* cpu_pane_{nullptr};
    GpuMapWidget* gpu_pane_{nullptr};
    QSplitter* mainSplitter_;
    QSplitter* vertSplitter_;
    QSplitter* map_row_{nullptr};
    ChunkCoordsProgressWidget* coords_progress_{nullptr};
    /// One set of toolbars for the map pane - the renderers are alternatives,
    /// not a pair, and both read the state they show from the shared view.
    FloatingToolBar* toolbar_{nullptr};
    FloatingToolBar* selection_toolbar_{nullptr};
    QTabWidget* nbtTabWidget_;
    ChunkEditorWidget* chunkWidget_{nullptr};
    // nbt editor tabs
    NbtWidget* level_dat_editor_;
    NbtWidget* player_editor_;
    NbtWidget* village_editor_;
    NbtWidget* other_nbt_editor_;
    StructureEditorWidget* structures_editor_;
    MapItemEditor* map_item_editor_;
    // status bar
    LevelStatusBar* status_bar_;
    // id in tabwidget
    // filter dialog
    RenderFilterDialog* render_filter_dialog_{nullptr};
    // toolbar group indices
    int tb_view_grp_{-1};
    int tb_dim_grp_{-1};
    int tb_layer_grp_{-1};
    int tb_overlay_grp_{-1};
    int sel_grp_{-1};

    const int tab_id_{-1};
};
#endif
