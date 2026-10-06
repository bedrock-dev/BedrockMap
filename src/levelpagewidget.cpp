#include "levelpagewidget.h"

#include <qboxlayout.h>
#include <qcontainerfwd.h>
#include <qlabel.h>
#include <qlayoutitem.h>
#include <qnamespace.h>
#include <qwidget.h>
#include <qwindowdefs_win.h>

#include <QDialog>
#include <QLayout>
#include <QMessageBox>
#include <QSpacerItem>
#include <QStackedWidget>
#include <QSplitter>
#include <QToolButton>
#include <functional>
#include <memory>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "color.h"
#include "chunkeditorwidget.h"
#include "cpumapwidget.h"
#include "leveltabwidget.h"
#include "loguru/loguru.hpp"
#include "magic-enum/magic_enum.hpp"
#include "mapitemeditor.h"
#include "mcstructure.h"
#include "msg.h"
#include "pleasewaitdialog.h"
#include "resourcemanager.h"
#include "utils.h"
#include "voxelwidget.h"

namespace {
    VoxelGrid buildVoxelDataFromMcstructure(const bl::mcstructure& structure) {
        const int sx = structure.size_x();
        const int sy = structure.size_y();
        const int sz = structure.size_z();

        VoxelGrid data(sy, std::vector<std::vector<Voxel>>(sx, std::vector<Voxel>(sz, Voxel(QColor(255, 255, 255, 0), true))));
        if (sx <= 0 || sy <= 0 || sz <= 0 || structure.palette_size() == 0) return data;

        for (int x = 0; x < sx; ++x) {
            for (int y = 0; y < sy; ++y) {
                for (int z = 0; z < sz; ++z) {
                    const auto* block = structure.block_at(x, y, z);
                    if (!block || block->name == "minecraft:air" || block->name == "minecraft:unknown") continue;

                    const auto baseColor = bl::get_block_by_name_tag(block->name);
                    // mcstructure files do not carry biome data; use plains as a stable preview biome.
                    const auto color = bl::blend_color_with_biome(block->name, baseColor, bl::biome::plains);
                    data[y][x][z] = Voxel(QColor(color.r, color.g, color.b, color.a), color.a < 255);
                }
            }
        }
        return data;
    }
}  // namespace

StructureEditorWidget::StructureEditorWidget(QWidget* parent) : QWidget(parent) {
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    this->nbt_editor_ = new NbtWidget(splitter);
    this->nbt_editor_->setMode(NbtMode::Memory);
    this->nbt_editor_->setReadOnly(true);

    this->preview_stack_ = new QStackedWidget(splitter);
    this->preview_status_ = new QLabel(this->preview_stack_);
    this->preview_status_->setAlignment(Qt::AlignCenter);
    this->preview_status_->setWordWrap(true);
    this->preview_status_->setText(tr("levelPageWidget.structurePreview.noData"));
    this->voxel_widget_ = new VoxelWidget(this->preview_stack_);
    this->voxel_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    this->preview_stack_->addWidget(this->preview_status_);
    this->preview_stack_->addWidget(this->voxel_widget_);
    this->preview_stack_->setCurrentWidget(this->preview_status_);

    splitter->addWidget(this->nbt_editor_);
    splitter->addWidget(this->preview_stack_);
    // Give the NBT editor a little more room than the 3D preview by default.
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    connect(this->nbt_editor_, &NbtWidget::itemOpened, this, &StructureEditorWidget::onItemOpened);
}

void StructureEditorWidget::loadStructureData(const bl::general_kv_nbts& data) {
    std::vector<NBTListItem*> items;
    items.reserve(data.data().size());
    for (const auto& kv : data.data()) {
        auto* item = NBTListItem::from(dynamic_cast<compound_tag*>(kv.second->copy()), kv.first.c_str(), kv.first.c_str());
        item->setIcon(QIcon(QPixmap::fromImage(*OtherNBTIcon())));
        items.push_back(item);
    }

    this->nbt_editor_->loadNewData(items);
    this->preview_stack_->setCurrentWidget(this->preview_status_);
    this->preview_status_->setText(items.empty() ? tr("levelPageWidget.structurePreview.noData")
                                                  : tr("levelPageWidget.structurePreview.selectStructure"));
    if (!items.empty()) this->nbt_editor_->openItem(0);
}

void StructureEditorWidget::clearData() {
    this->nbt_editor_->clearData();
    this->preview_stack_->setCurrentWidget(this->preview_status_);
    this->preview_status_->setText(tr("levelPageWidget.structurePreview.noData"));
}

void StructureEditorWidget::onItemOpened(NBTListItem* item) {
    if (!item || !item->root_) {
        this->preview_stack_->setCurrentWidget(this->preview_status_);
        this->preview_status_->setText(tr("levelPageWidget.structurePreview.invalid"));
        return;
    }

    const auto raw = item->root_->to_raw();
    const auto structure = bl::parse_mcstructure(reinterpret_cast<const byte_t*>(raw.data()), raw.size());
    if (structure.size_x() <= 0 || structure.size_y() <= 0 || structure.size_z() <= 0) {
        this->preview_stack_->setCurrentWidget(this->preview_status_);
        this->preview_status_->setText(tr("levelPageWidget.structurePreview.invalid"));
        return;
    }

    this->preview_stack_->setCurrentWidget(this->voxel_widget_);
    this->voxel_widget_->updateVoxelData(buildVoxelDataFromMcstructure(structure));
}

// status bar
LevelStatusBar::LevelStatusBar(QWidget* parent) : QWidget(parent) {
    status_msg_ = new QLabel(this);
    sel_info_ = new QLabel(this);
    modify_info_ = new QLabel(this);
    pos_ = new QLabel(this);
    pos_->setMargin(0);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->addWidget(status_msg_);
    layout->addStretch();
    layout->addWidget(sel_info_);
    layout->addWidget(modify_info_);
    layout->addWidget(pos_);
    setFixedHeight(14);
}

void LevelStatusBar::onPosChanged(int x, int z, int dim) {
    auto cp = bl::block_pos{x, 0, z}.to_chunk_pos();
    this->pos_->setText(QString("Dim:%1 Pos: %2,%3 / %4,%5").arg(dim).arg(cp.x).arg(cp.z).arg(x).arg(z));
}

void LevelStatusBar::setSelectionInfo(int count) {
    if (count > 0)
        sel_info_->setText(QString("Sel: %1  ").arg(count));
    else
        sel_info_->clear();
}

void LevelStatusBar::setModifyInfo(int modified, int deleted) {
    if (modified > 0 || deleted > 0)
        modify_info_->setText(QString("Modify: %1 Del:%2  ").arg(modified).arg(deleted));
    else
        modify_info_->clear();
}

// level widget
LevelPageWidget::LevelPageWidget(LevelTabWidget* parent, int id) : TabPageWidget(parent), parent_(parent), tab_id_(id), commit_task_(this) {
    level_loader_ = std::make_unique<AsyncLevelLoader>();

    // gui
    setupMapPane();  // also builds the map chrome: toolbars for the active renderer
    setupDataWidget();

    // status bar
    status_bar_ = new LevelStatusBar(this);
    connect(&commit_task_, &GuiTaskRunner::started, this, []() { PleaseWaitDialog::instance().showBusy(); });
    connect(&commit_task_, &GuiTaskRunner::finished, this, &LevelPageWidget::onCommitFinished);
    connect(&commit_task_, &GuiTaskRunner::failed, this, &LevelPageWidget::onCommitFailed);

    // vertical splitter: map + nbt tabs
    // Only the enabled renderer is added: the map pane is not a comparison view,
    // so the active one gets the whole row.
    auto* mapRow = new QSplitter(Qt::Horizontal, this);
    map_row_ = mapRow;
    if (setting::current().PRELOAD_ALL_CHUNK_COORDS) {
        coords_progress_ = new ChunkCoordsProgressWidget(this);
        mapRow->addWidget(coords_progress_);
        // The renderer is temporarily detached from the layout. It is still a
        // visible child of this page, so hide it explicitly or it can paint
        // its default-sized rectangle at the page's top-left corner.
        activeMapPane()->hide();
    } else {
        mapRow->addWidget(activeMapPane());
    }
    mapRow->setStretchFactor(0, 1);
    mapRow->setChildrenCollapsible(true);

    vertSplitter_ = new QSplitter(Qt::Vertical, this);
    vertSplitter_->addWidget(mapRow);
    vertSplitter_->addWidget(nbtTabWidget_);
    vertSplitter_->setStretchFactor(0, 1);
    vertSplitter_->setStretchFactor(1, 0);

    // horizontal splitter: vertSplitter | chunkEditor
    chunkWidget_ = new ChunkEditorWidget(nullptr, level_loader_.get());
    chunkWidget_->hide();
    chunkWidget_->setMinimumWidth(200);

    mainSplitter_ = new QSplitter(Qt::Horizontal, this);
    mainSplitter_->addWidget(vertSplitter_);
    mainSplitter_->addWidget(chunkWidget_);
    mainSplitter_->setStretchFactor(0, 1);
    mainSplitter_->setStretchFactor(1, 0);
    mainSplitter_->setChildrenCollapsible(false);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(mainSplitter_);
    layout->addWidget(status_bar_);
    layout->setContentsMargins(0, 0, 0, 0);

    // connect signals
    // global data load runs as a silent background task; the signals fill the editors
    connect(&this->global_data_task_, &GuiTaskRunner::finished, this, &LevelPageWidget::onLoadGlobalDataFinished);
    connect(&this->global_data_task_, &GuiTaskRunner::failed, this, &LevelPageWidget::onLoadGlobalDataFailed);
    // The status bar follows the cursor, which only the widget under the pointer
    // knows; everything else is shared state and comes from the host.
    if (cpu_pane_) connect(cpu_pane_, &CpuMapWidget::mouseMove, this->status_bar_, &LevelStatusBar::onPosChanged);
    if (gpu_pane_) connect(gpu_pane_, &GpuMapWidget::mouseMove, this->status_bar_, &LevelStatusBar::onPosChanged);
    connect(map_host_, &MapHost::requestOpenChunkEditor, this, &LevelPageWidget::showChunkEditor);
    connect(map_host_, &MapHost::selectionChanged, this, &LevelPageWidget::refreshSelectionInfo);
    connect(map_host_, &MapHost::toolbarsVisibleRequested, this, &LevelPageWidget::setToolBarsVisible);
    connect(map_host_, &MapHost::syncToolbarsRequested, this, &LevelPageWidget::syncToolbars);
    connect(chunkWidget_, &ChunkEditorWidget::editorClosed, this, [this]() { map_host_->unselectChunk(); });
    connect(chunkWidget_, &ChunkEditorWidget::locateChunk, this, [this](int x, int z, int dim) {
        map_host_->setDim(dim);
        map_host_->gotoBlockPos(x * 16 + 8, z * 16 + 8);
    });
    connect(level_loader_.get(), &AsyncLevelLoader::dirtyChanged, this, [this]() {
        auto [e, ne] = level_loader_->chunkModifyCounts();
        status_bar_->setModifyInfo(ne, e);
        refreshDirty();
    });
    if (coords_progress_) {
        connect(level_loader_.get(), &AsyncLevelLoader::chunkCoordsPreloadProgress, coords_progress_,
                &ChunkCoordsProgressWidget::setProgress);
        connect(level_loader_.get(), &AsyncLevelLoader::chunkCoordsPreloadFinished, this, &LevelPageWidget::onChunkCoordsPreloadFinished);
    }
}

LevelPageWidget::~LevelPageWidget() {
    this->stop_loading_global_data_ = true;
    this->global_data_task_.waitForFinished();
    this->commit_task_.waitForFinished();
    this->level_loader_->close();
}

void LevelPageWidget::setupMapPane() {
    // The host owns the shared view, the overlays and the level editing actions.
    // The renderers only draw and take input, so exactly one of them is needed:
    // the other one is not a hidden fallback, it simply is not built.
    map_host_ = new MapHost(this, level_loader_.get());
    if (setting::current().GPU_RENDER_ENABLED) {
        gpu_pane_ = new GpuMapWidget(this, level_loader_.get(), map_host_->mapView(), &map_host_->overlays(), map_host_->importOverlay(),
                                     map_host_);
    } else {
        cpu_pane_ = new CpuMapWidget(this, level_loader_.get(), map_host_->mapView(), &map_host_->overlays(), map_host_->importOverlay(),
                                     map_host_);
    }
    // The import confirm bar and its warnings belong to the pane on screen.
    map_host_->setPaneWidget(activeMapPane());
    setupToolBar();
    setupSelectionToolBar();
}

QWidget* LevelPageWidget::activeMapPane() const {
    if (cpu_pane_) return static_cast<QWidget*>(cpu_pane_);
    return static_cast<QWidget*>(gpu_pane_);
}

void LevelPageWidget::refreshSelectionInfo() {
    if (!map_host_) return;
    status_bar_->setSelectionInfo(static_cast<int>(map_host_->selection().chunkCount()));
}

void LevelPageWidget::applySelectionMode(SelectionController::Mode mode) {
    // One mode for every renderer: it lives on the shared view.
    if (map_host_) map_host_->mapView()->setSelectionMode(mode);
    syncToolbars();
}

void LevelPageWidget::setupSelectionToolBar() {
    using SM = SelectionRegion::Mode;
    using GC = FloatingToolBar::GroupConfig;

    // The chrome belongs to the pane that is on screen, so it is parented to it.
    auto* stb = new FloatingToolBar(activeMapPane());
    selection_toolbar_ = stb;
    stb->setOrientation(Qt::Horizontal);
    stb->setAnchor(Qt::AlignHCenter | Qt::AlignTop);
    stb->setAnchorMargins(6);

    GC selGroup;
    selGroup.mode = GC::Exclusive;
    selGroup.buttons = {
        {ToolBarIcon("sel"), tr("levelPageWidget.toolBar.selection.replace")},
        {ToolBarIcon("add_sel"), tr("levelPageWidget.toolBar.selection.add")},
        {ToolBarIcon("del_sel"), tr("levelPageWidget.toolBar.selection.subtract")},
    };
    int selGrp = stb->addGroup(selGroup);
    sel_grp_ = selGrp;

    stb->addSeparator();

    GC saveGroup;
    saveGroup.mode = GC::Toggle;
    saveGroup.buttons = {
        {ToolBarIcon("save"), tr("levelPageWidget.toolBar.selection.save"), false},
    };
    int saveGrp = stb->addGroup(saveGroup);

    connect(stb, &FloatingToolBar::buttonToggled, this, [this, selGrp, saveGrp](int g, int b, bool checked) {
        if (g == selGrp && checked) {
            auto mode = static_cast<SM>(b);
            LOG_F(INFO, "Selection mode: %d", static_cast<int>(mode));
            applySelectionMode(mode);
        } else if (g == saveGrp && checked) {
            LOG_F(INFO, "Save save");
            if (!isDirty()) {
                INFO(msg::NOTHING_TO_SAVE());
            } else {
                connect(
                    this, &LevelPageWidget::commitFinished, this,
                    [this](bool success) {
                        if (success) INFO(msg::LEVEL_SAVED());
                    },
                    Qt::SingleShotConnection);
                commit();
            }
            // save action
        }
    });

    // default: Replace
    stb->setButtonChecked(selGrp, 0, true);
    applySelectionMode(SM::Replace);
}

void LevelPageWidget::setupToolBar() {
    auto* tb = new FloatingToolBar(activeMapPane());
    toolbar_ = tb;
    tb->setAnchorMargins(6);

    using Mr = RenderOption;
    using GC = FloatingToolBar::GroupConfig;

    // View group (toggle) — grid & coordinates, placed above all
    GC viewGroup;
    viewGroup.mode = GC::Toggle;
    viewGroup.buttons = {
        {ToolBarIcon("grid"), tr("levelPageWidget.toolBar.showGrid")},
        {ToolBarIcon("coord"), tr("levelPageWidget.toolBar.showCoord")},
    };
    int viewGrp = tb->addGroup(viewGroup);
    tb_view_grp_ = viewGrp;

    tb->addSeparator();

    // Dimension group (exclusive)
    GC dimGroup;
    dimGroup.mode = GC::Exclusive;
    dimGroup.buttons = {
        {ToolBarIcon("overworld"), tr("levelPageWidget.toolBar.overworld")},
        {ToolBarIcon("nether"), tr("levelPageWidget.toolBar.nether")},
        {ToolBarIcon("theend"), tr("levelPageWidget.toolBar.theend")},
    };
    int dimGrp = tb->addGroup(dimGroup);
    tb_dim_grp_ = dimGrp;

    tb->addSeparator();

    // Layer group (exclusive) — order matches RenderOption::LayerType
    GC layerGroup;
    layerGroup.mode = GC::Exclusive;
    layerGroup.buttons = {
        {ToolBarIcon("map"), tr("levelPageWidget.toolBar.terrain")},
        {ToolBarIcon("biome"), tr("levelPageWidget.toolBar.biome")},

    };
    // default to terrain (index 1)
    int layerGrp = tb->addGroup(layerGroup);
    tb_layer_grp_ = layerGrp;

    tb->addSeparator();

    // Overlay group (toggle)
    GC overlayGroup;
    overlayGroup.mode = GC::Toggle;
    overlayGroup.buttons = {
        {ToolBarIcon("slime"), tr("levelPageWidget.toolBar.slimeChunks")},
        {ToolBarIcon("actor"), tr("levelPageWidget.toolBar.entities")},
        {ToolBarIcon("village"), tr("levelPageWidget.toolBar.villages")},
        {ToolBarIcon("hsa"), tr("levelPageWidget.toolBar.HSAs")},
    };
    int overlayGrp = tb->addGroup(overlayGroup);
    tb_overlay_grp_ = overlayGrp;

    tb->addSeparator();

    // Action group (non-checkable buttons)
    GC actionGroup;
    actionGroup.mode = GC::Toggle;
    actionGroup.buttons = {
        {ToolBarIcon("filter"), tr("levelPageWidget.toolBar.filter"), false},
        {ToolBarIcon("global_nbt"), tr("levelPageWidget.toolBar.globalNbt"), false},
    };
    int actionGrp = tb->addGroup(actionGroup);

    // --- connect signals ---

    connect(tb, &FloatingToolBar::buttonToggled, this,
            [this, viewGrp, dimGrp, layerGrp, overlayGrp, actionGrp](int g, int b, bool checked) {
                auto* mw = map_host_;
                if (!mw) return;

                if (g == viewGrp) {
                    auto type = static_cast<Mr::OtherType>(b + Mr::Grid);
                    mw->setOther(type, checked);
                } else if (g == dimGrp && checked) {
                    mw->setDim(b);
                } else if (g == layerGrp && checked) {
                    mw->setLayer(static_cast<Mr::LayerType>(b));
                } else if (g == overlayGrp) {
                    auto type = static_cast<Mr::OtherType>(b + Mr::SlimeChunk);
                    mw->setOther(type, checked);
                } else if (g == actionGrp && checked) {
                    if (b == 0) {
                        openFilterDialog();
                    } else if (b == 1) {
                        toggleGlobalDataWidget();
                    }
                }
            });

    // default: grid on
    tb->setButtonChecked(viewGrp, 0, true);
    // default terrain (index 0 = map/terrain, index 1 = biome)
    tb->setButtonChecked(layerGrp, 0, true);
}

void LevelPageWidget::syncToolbars() {
    if (!map_host_) return;

    using Mr = RenderOption;
    auto opt = map_host_->renderOption();

    // The toolbar shows the shared state, so it is synced from the view rather
    // than from whichever map last handled input.
    if (toolbar_) {
        auto* tb = toolbar_;
        tb->blockSignals(true);
        tb->setButtonChecked(tb_view_grp_, 0, opt.getOther(Mr::Grid));
        tb->setButtonChecked(tb_view_grp_, 1, opt.getOther(Mr::Coords));
        // Dimension group — uncheck all if using a custom dimension
        for (int i = 0; i < 3; ++i) tb->setButtonChecked(tb_dim_grp_, i, opt.dim == i);
        // Layer group
        for (int i = 0; i < 3; ++i) tb->setButtonChecked(tb_layer_grp_, i, static_cast<int>(opt.layer) == i);
        // Overlay group
        tb->setButtonChecked(tb_overlay_grp_, 0, opt.getOther(Mr::SlimeChunk));
        tb->setButtonChecked(tb_overlay_grp_, 1, opt.getOther(Mr::Actors));
        tb->setButtonChecked(tb_overlay_grp_, 2, opt.getOther(Mr::Village));
        tb->setButtonChecked(tb_overlay_grp_, 3, opt.getOther(Mr::HSA));
        tb->blockSignals(false);
    }

    if (selection_toolbar_) {
        auto* stb = selection_toolbar_;
        const auto mode = map_host_->selection().mode();
        stb->blockSignals(true);
        for (int i = 0; i < 3; ++i) stb->setButtonChecked(sel_grp_, i, static_cast<int>(mode) == i);
        stb->blockSignals(false);
    }
}

void LevelPageWidget::setupDataWidget() {
    // nbt
    nbtTabWidget_ = new QTabWidget(this);
    nbtTabWidget_->setContentsMargins(0, 0, 0, 0);
    level_dat_editor_ = new NbtWidget(nbtTabWidget_);
    player_editor_ = new NbtWidget(nbtTabWidget_);
    village_editor_ = new NbtWidget(nbtTabWidget_);
    other_nbt_editor_ = new NbtWidget(nbtTabWidget_);
    structures_editor_ = new StructureEditorWidget(nbtTabWidget_);
    level_dat_editor_->setMode(NbtMode::Memory);
    player_editor_->setMode(NbtMode::Memory);
    village_editor_->setMode(NbtMode::Memory);
    other_nbt_editor_->setMode(NbtMode::Memory);
    map_item_editor_ = new MapItemEditor(nbtTabWidget_);

    nbtTabWidget_->addTab(level_dat_editor_, "level.dat");
    nbtTabWidget_->addTab(player_editor_, "players");
    nbtTabWidget_->addTab(village_editor_, "villages");
    nbtTabWidget_->addTab(other_nbt_editor_, "other");
    nbtTabWidget_->addTab(structures_editor_, "structures");
    nbtTabWidget_->addTab(map_item_editor_, "Map");
    nbtTabWidget_->setTabPosition(QTabWidget::West);

    for (auto* editor :
         {level_dat_editor_, player_editor_, village_editor_, other_nbt_editor_, structures_editor_->nbtEditor(), map_item_editor_->nbtEditor()}) {
        connect(editor, &NbtWidget::nbtModified, this, &LevelPageWidget::refreshDirty);
        connect(editor, &NbtWidget::nbtModified, this, [this, editor]() {
            QWidget* realTab = editor;
            if (editor == map_item_editor_->nbtEditor()) realTab = map_item_editor_;
            int idx = nbtTabWidget_->indexOf(realTab);

            if (idx < 0) return;
            auto text = nbtTabWidget_->tabText(idx);
            if (editor->dirty()) {
                if (!text.endsWith(" *")) text += " *";
            } else {
                if (text.endsWith(" *")) text.chop(2);
            }
            nbtTabWidget_->setTabText(idx, text);
        });
    }
    nbtTabWidget_->hide();
}

QString LevelPageWidget::getLevelName() {
    if (!level_loader_ || !level_loader_->isOpen()) return {};
    return QString::fromStdString(level_loader_->level().dat().level_name());
}

bool LevelPageWidget::isDirty() const {
    for (auto* editor :
        {level_dat_editor_, player_editor_, village_editor_, other_nbt_editor_, structures_editor_->nbtEditor(), map_item_editor_->nbtEditor()}) {
        if (editor && editor->dirty()) return true;
    }
    return level_loader_->isDirty();
}

void LevelPageWidget::refreshDirty() {
    auto dirty = isDirty();
    auto* tw = qobject_cast<LevelTabWidget*>(parent_);
    if (!tw) return;
    int i = tw->indexOf(this);
    if (i < 0) return;
    auto name = getLevelName();
    if (name.isEmpty()) name = QString("Level %1").arg(tab_id_);
    tw->setTabText(i, dirty ? name + " *" : name);
}

bool LevelPageWidget::commit() {
    LOG_F(INFO, "Commit modifications");
    if (commit_task_.isRunning() || (map_host_ && map_host_->chunkEditRunning())) return false;

    std::unique_ptr<bl::nbt::compound_tag> levelDat;
    if (this->level_dat_editor_->dirty()) {
        auto nbts = this->level_dat_editor_->getPaletteCopy();
        if (nbts.size() == 1 && nbts[0]) {
            levelDat.reset(nbts[0]);
        } else {
            LOG_F(WARNING, "level.dat data is invalid, skip saving level.dat");
            for (auto* nbt : nbts) delete nbt;
            return false;
        }
    }

    std::unordered_map<std::string, std::string> allModifies;
    for (auto* editor : {player_editor_, village_editor_, other_nbt_editor_, structures_editor_->nbtEditor(), map_item_editor_->nbtEditor()}) {
        if (editor) {
            for (auto& kv : editor->getModifyCache()) {
                allModifies[kv.first] = kv.second;
            }
        }
    }

    pending_level_dat_ = std::move(levelDat);
    pending_global_modifies_ = std::move(allModifies);
    const auto globalModifies = pending_global_modifies_;
    commit_task_.start([this, globalModifies](GuiTaskRunner* task) {
        const bool success = level_loader_->commitEdits(globalModifies, pending_level_dat_.get());
        if (!success) task->fail(tr("Save failed"));
    });
    return true;
}

void LevelPageWidget::onCommitFinished() {
    PleaseWaitDialog::instance().hideBusy();
    level_dat_editor_->clearModifyCache();
    player_editor_->clearModifyCache();
    village_editor_->clearModifyCache();
    other_nbt_editor_->clearModifyCache();
    structures_editor_->nbtEditor()->clearModifyCache();
    map_item_editor_->nbtEditor()->clearModifyCache();
    pending_level_dat_.reset();
    pending_global_modifies_.clear();
    refreshDirty();
    emit commitFinished(true);
}

void LevelPageWidget::onCommitFailed(const QString& error) {
    PleaseWaitDialog::instance().hideBusy();
    pending_level_dat_.reset();
    pending_global_modifies_.clear();
    QMessageBox::warning(this, tr("Save failed"), error);
    refreshDirty();
    emit commitFinished(false);
}

bool LevelPageWidget::loadLevel(const QString& path) {
    level_loader_->setPreloadAllChunkCoords(setting::current().PRELOAD_ALL_CHUNK_COORDS);
    auto ret = level_loader_->open(path.toStdString());
    if (!ret) {
        LOG_F(WARNING, "Can not open level: %s", path.toStdString().c_str());
        return false;
    }
    auto& dat = level_loader_->level().dat();
    const auto format = level_loader_->level().chunk_format();
    LOG_F(INFO, "Open level %s with version %s, chunk format %s", dat.level_name().c_str(), dat.min_compat_version().to_string().c_str(),
          std::string(magic_enum::enum_name(format)).c_str());
    Assert(dat.root() != nullptr, "The level.dat file is nullptr");
    auto* ld = dat.root()->as<bl::nbt::compound_tag*>();
    this->level_dat_editor_->loadNewData({NBTListItem::from(ld->copy()->as<bl::nbt::compound_tag*>(), "level.dat")});
    setLevelStatusBar(path + "  " + dat.min_compat_version().to_string().c_str());
    if (!setting::current().LOAD_GLOBAL_DATA) return true;
    global_data_task_.start([this](GuiTaskRunner* task) {
        try {
            level_loader_->loadGlobalData(std::ref(this->global_data_), std::ref(stop_loading_global_data_));
        } catch (std::exception& e) {
            LOG_F(WARNING, "Can not fully load global data: %s", e.what());
        }
    });
    return true;
}

void LevelPageWidget::onChunkCoordsPreloadFinished() {
    if (!coords_progress_ || !map_row_ || map_row_->indexOf(coords_progress_) < 0) return;

    map_row_->replaceWidget(0, activeMapPane());
    coords_progress_->hide();
    activeMapPane()->show();
    map_host_->setPaneWidget(activeMapPane());
    syncToolbars();
}

void LevelPageWidget::closeLevel() {
    this->stop_loading_global_data_ = true;
    global_data_task_.waitForFinished();
    if (map_host_) map_host_->waitForChunkEditTask();
    if (level_loader_->isOpen()) level_loader_->close();
}

void LevelPageWidget::toggleGlobalDataWidget() {
    if (!nbtTabWidget_) return;
    auto vis = nbtTabWidget_->isVisible();
    nbtTabWidget_->setVisible(!vis);
}

void LevelPageWidget::openFilterDialog() {
    auto* loader = level_loader_.get();
    if (!loader) return;
    if (!render_filter_dialog_) {
        render_filter_dialog_ = new RenderFilterDialog(this);
    }
    render_filter_dialog_->setFilter(loader->filter());
    if (render_filter_dialog_->exec() == QDialog::Accepted) {
        render_filter_dialog_->collectFilerData();
        loader->setFilter(render_filter_dialog_->getFilter());
        loader->clearAllCache();
    }
}

void LevelPageWidget::showChunkEditor(const bl::chunk_pos& pos) {
    auto opt = level_loader_->getRawChunk(pos);
    if (!opt) {
        WARN(msg::NO_CHUNK_FOUND());
        return;
    }

    // if the chunk editor has unsaved changes, prompt the user
    if (chunkWidget_->isVisible() && chunkWidget_->isDirty()) {
        auto btn = QMessageBox::question(this, msg::UNSAVED_CHANGES(), msg::UNSAVED_CHANGES_PROMPT(),
                                         QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (btn == QMessageBox::Cancel) return;
        if (btn == QMessageBox::Yes && !chunkWidget_->saveChunk()) return;
    }

    chunkWidget_->loadChunkData(std::move(*opt));
    map_host_->selectChunk(pos);
    int totalW = mainSplitter_->width();
    int chunkW = totalW / 3;
    if (chunkW < 300) chunkW = 300;
    mainSplitter_->setSizes({totalW - chunkW, chunkW});
    chunkWidget_->show();
}

void LevelPageWidget::collectVillagesGuiData(const bl::village_data::village_table_type& vs) {
    for (int i = 0; i < vs.size(); i++) {
        auto& villsInDim = vs[i];
        for (auto kv : villsInDim) {
            auto* nbt = kv.second[static_cast<int>(bl::village_key::key_type::INFO)];
            if (!nbt) continue;
            auto x0 = dynamic_cast<bl::nbt::int_tag*>(nbt->get("X0"));
            auto z0 = dynamic_cast<bl::nbt::int_tag*>(nbt->get("Z0"));
            auto x1 = dynamic_cast<bl::nbt::int_tag*>(nbt->get("X1"));
            auto z1 = dynamic_cast<bl::nbt::int_tag*>(nbt->get("Z1"));
            if (!x0 || !z0 || !x1 || !z1) continue;
            auto pos0 = bl::block_pos(x0->value, 0, z0->value);
            auto pos1 = bl::block_pos(x1->value, 0, z1->value);
            this->villages_.insert(kv.first.c_str(), VillageDrawInfo{pos0, pos1, i});
        }
    }
}

void LevelPageWidget::fillGlobalData(GlobalNBTLoadResult& res) {
    LOG_F(INFO, "Filling player data (%llu)...", static_cast<unsigned long long>(res.playerData.data().size()));
    auto& playerData = res.playerData.data();
    std::vector<NBTListItem*> playerNBTList;
    for (auto& kv : playerData) {
        auto* item = NBTListItem::from(dynamic_cast<compound_tag*>(kv.second->copy()), kv.first.c_str(), kv.first.c_str());
        item->setIcon(QIcon(QPixmap::fromImage(*PlayerNBTIcon())));
        playerNBTList.push_back(item);
    }
    this->player_editor_->loadNewData(playerNBTList);

    LOG_F(INFO, "Filling other data (%llu)...", static_cast<unsigned long long>(res.otherData.data().size()));
    auto& otherData = res.otherData.data();
    std::vector<NBTListItem*> otherNBTList;
    for (auto& kv : otherData) {
        auto* item = NBTListItem::from(dynamic_cast<compound_tag*>(kv.second->copy()), kv.first.c_str(), kv.first.c_str());
        item->setIcon(QIcon(QPixmap::fromImage(*OtherNBTIcon())));
        otherNBTList.push_back(item);
    }
    this->other_nbt_editor_->loadNewData(otherNBTList);

    LOG_F(INFO, "Filling village data (%llu)...", static_cast<unsigned long long>(res.villageData.data().size()));
    auto& villData = res.villageData.data();
    this->collectVillagesGuiData(villData);
    map_host_->setVillages(this->villages_);
    std::vector<NBTListItem*> villNBTList;
    for (const auto& dim : villData) {
        for (const auto& kv : dim) {
            int index = 0;
            for (auto& p : kv.second) {
                if (p) {
                    auto key = kv.first + "_" + bl::village_key::village_key_type_to_str(static_cast<bl::village_key::key_type>(index));
                    auto* item = NBTListItem::from(dynamic_cast<compound_tag*>(p->copy()), key.c_str(), ("VILLAGE_" + key).c_str());
                    item->setIcon(QIcon(QPixmap::fromImage(*VillageNBTIcon(static_cast<bl::village_key::key_type>(index)))));
                    villNBTList.push_back(item);
                }
                index++;
            }
        }
    }
    this->village_editor_->loadNewData(villNBTList);

    LOG_F(INFO, "Filling map data (%llu)...", static_cast<unsigned long long>(res.mapData.data().size()));
    this->map_item_editor_->load_map_data(res.mapData);

    LOG_F(INFO, "Filling structures data (%llu)...", static_cast<unsigned long long>(res.structuresData.data().size()));
    this->structures_editor_->loadStructureData(res.structuresData);
}

void LevelPageWidget::onLoadGlobalDataFinished() {
    fillGlobalData(this->global_data_);
    this->global_data_.clear();
}

void LevelPageWidget::onLoadGlobalDataFailed(const QString& error) {
    LOG_F(WARNING, "Load global data failed: %s", error.toStdString().c_str());
    onLoadGlobalDataFinished();  // still fill whatever was loaded
}
