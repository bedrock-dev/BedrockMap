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
#include <QSpacerItem>
#include <QSplitter>
#include <QToolButton>
#include <QtConcurrent/QtConcurrent>
#include <functional>
#include <memory>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "chunkeditorwidget.h"
#include "leveltabwidget.h"
#include "loguru/loguru.hpp"
#include "mapitemeditor.h"
#include "mapwidget.h"
#include "msg.h"
#include "resourcemanager.h"

// status bar
LevelStatusBar::LevelStatusBar(QWidget *parent) : QWidget(parent) {
    status_msg_ = new QLabel(this);
    sel_info_ = new QLabel(this);
    modify_info_ = new QLabel(this);
    pos_ = new QLabel(this);
    pos_->setMargin(0);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->addWidget(status_msg_);
    layout->addStretch();
    layout->addWidget(sel_info_);
    layout->addWidget(modify_info_);
    layout->addWidget(pos_);
    setFixedHeight(14);
}

void LevelStatusBar::onPosChanged(int x, int z) {
    auto cp = bl::block_pos{x, 0, z}.to_chunk_pos();
    this->pos_->setText(QString("Pos: %1,%2 / %3,%4").arg(cp.x).arg(cp.z).arg(x).arg(z));
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
LevelPageWidget::LevelPageWidget(LevelTabWidget *parent, int id) : QWidget(parent), parent_(parent), tab_id_(id) {
    level_loader_ = std::make_unique<AsyncLevelLoader>();

    // gui
    setupMapWidget();
    setupSelectionToolBar();
    setupDataWidget();

    // status bar
    status_bar_ = new LevelStatusBar(this);

    // vertical splitter: map + nbt tabs
    vertSplitter_ = new QSplitter(Qt::Vertical, this);
    vertSplitter_->addWidget(mapWidget_);
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

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(mainSplitter_);
    layout->addWidget(status_bar_);
    layout->setContentsMargins(0, 0, 0, 0);

    // connect signals
    connect(&this->load_global_data_watcher_, &QFutureWatcher<bool>::finished, this, &LevelPageWidget::onLoadGlobalDataFinished);
    connect(this->mapWidget_, &MapWidget::mouseMove, this->status_bar_, &LevelStatusBar::onPosChanged);
    connect(this->mapWidget_, &MapWidget::requestOpenChunkEditor, this, &LevelPageWidget::showChunkEditor);
    connect(chunkWidget_, &ChunkEditorWidget::editorClosed, this, [this]() {
        mapWidget_->unselectChunk();
        mapWidget_->update();
    });
    connect(chunkWidget_, &ChunkEditorWidget::locateChunk, this, [this](int x, int z, int dim) {
        mapWidget_->setDim(static_cast<MapWidget::RenderOption::DimType>(dim));
        mapWidget_->gotoBlockPos(x * 16 + 8, z * 16 + 8);
    });
    connect(level_loader_.get(), &AsyncLevelLoader::dirtyChanged, this, [this]() {
        auto [e, ne] = level_loader_->chunkModifyCounts();
        status_bar_->setModifyInfo(ne, e);
    });
    connect(this->mapWidget_, &MapWidget::selectionChanged, this, [this]() {
        auto count = mapWidget_->selection().chunkCount();
        status_bar_->setSelectionInfo(static_cast<int>(count));
    });
}

LevelPageWidget::~LevelPageWidget() {
    this->level_loader_->close();
    this->stop_loading_global_data_ = true;
}

void LevelPageWidget::setupMapWidget() {
    mapWidget_ = new MapWidget(this, level_loader_.get());
    setupToolBar();
}

void LevelPageWidget::setupSelectionToolBar() {
    using SM = SelectionRegion::Mode;
    using GC = FloatingToolBar::GroupConfig;

    selection_toolbar_ = new FloatingToolBar(mapWidget_);
    selection_toolbar_->setOrientation(Qt::Horizontal);
    selection_toolbar_->setAnchor(Qt::AlignHCenter | Qt::AlignTop);
    selection_toolbar_->setAnchorMargins(6);

    GC selGroup;
    selGroup.mode = GC::Exclusive;
    selGroup.buttons = {
        {ToolBarIcon("sel"), tr("levelPageWidget.toolBar.selection.replace")},
        {ToolBarIcon("add_sel"), tr("levelPageWidget.toolBar.selection.add")},
        {ToolBarIcon("del_sel"), tr("levelPageWidget.toolBar.selection.subtract")},
    };
    int selGrp = selection_toolbar_->addGroup(selGroup);
    sel_grp_ = selGrp;

    selection_toolbar_->addSeparator();

    GC saveGroup;
    saveGroup.mode = GC::Toggle;
    saveGroup.buttons = {
        {ToolBarIcon("save"), tr("levelPageWidget.toolBar.selection.save"), false},
    };
    int saveGrp = selection_toolbar_->addGroup(saveGroup);

    connect(selection_toolbar_, &FloatingToolBar::buttonToggled, this, [this, selGrp, saveGrp](int g, int b, bool checked) {
        if (g == selGrp && checked) {
            auto mode = static_cast<SM>(b);
            LOG_F(INFO, "Selection mode: %d", static_cast<int>(mode));
            mapWidget_->setSelectionMode(mode);
        } else if (g == saveGrp && checked) {
            LOG_F(INFO, "Save save");
            if (!isDirty()) {
                INFO(msg::NOTHING_TO_SAVE());
            } else {
                commit();
                INFO(msg::LEVEL_SAVED());
            }
            // save action
        }
    });

    // default: Replace
    selection_toolbar_->setButtonChecked(selGrp, 0, true);
    mapWidget_->setSelectionMode(SM::Replace);
}

void LevelPageWidget::setupToolBar() {
    toolbar_ = new FloatingToolBar(mapWidget_);
    toolbar_->setAnchorMargins(6);

    using Mr = MapWidget::RenderOption;
    using GC = FloatingToolBar::GroupConfig;

    // View group (toggle) — grid & coordinates, placed above all
    GC viewGroup;
    viewGroup.mode = GC::Toggle;
    viewGroup.buttons = {
        {ToolBarIcon("grid"), tr("levelPageWidget.toolBar.showGrid")},
        {ToolBarIcon("coord"), tr("levelPageWidget.toolBar.showCoord")},
    };
    int viewGrp = toolbar_->addGroup(viewGroup);
    tb_view_grp_ = viewGrp;

    toolbar_->addSeparator();

    // Dimension group (exclusive)
    GC dimGroup;
    dimGroup.mode = GC::Exclusive;
    dimGroup.buttons = {
        {ToolBarIcon("overworld"), tr("levelPageWidget.toolBar.overworld")},
        {ToolBarIcon("nether"), tr("levelPageWidget.toolBar.nether")},
        {ToolBarIcon("theend"), tr("levelPageWidget.toolBar.theend")},
    };
    int dimGrp = toolbar_->addGroup(dimGroup);
    tb_dim_grp_ = dimGrp;

    toolbar_->addSeparator();

    // Layer group (exclusive) — order matches RenderOption::LayerType
    GC layerGroup;
    layerGroup.mode = GC::Exclusive;
    layerGroup.buttons = {
        {ToolBarIcon("map"), tr("levelPageWidget.toolBar.terrain")},
        {ToolBarIcon("biome"), tr("levelPageWidget.toolBar.biome")},

    };
    // default to terrain (index 1)
    int layerGrp = toolbar_->addGroup(layerGroup);
    tb_layer_grp_ = layerGrp;

    toolbar_->addSeparator();

    // Overlay group (toggle)
    GC overlayGroup;
    overlayGroup.mode = GC::Toggle;
    overlayGroup.buttons = {
        {ToolBarIcon("slime"), tr("levelPageWidget.toolBar.slimeChunks")},
        {ToolBarIcon("actor"), tr("levelPageWidget.toolBar.entities")},
        {ToolBarIcon("village"), tr("levelPageWidget.toolBar.villages")},
        {ToolBarIcon("hsa"), tr("levelPageWidget.toolBar.HSAs")},
    };
    int overlayGrp = toolbar_->addGroup(overlayGroup);
    tb_overlay_grp_ = overlayGrp;

    toolbar_->addSeparator();

    // Action group (non-checkable buttons)
    GC actionGroup;
    actionGroup.mode = GC::Toggle;
    actionGroup.buttons = {
        {ToolBarIcon("filter"), tr("levelPageWidget.toolBar.filter"), false},
        {ToolBarIcon("global_nbt"), tr("levelPageWidget.toolBar.globalNbt"), false},
    };
    int actionGrp = toolbar_->addGroup(actionGroup);

    // --- connect signals ---

    connect(toolbar_, &FloatingToolBar::buttonToggled, this,
            [this, viewGrp, dimGrp, layerGrp, overlayGrp, actionGrp](int g, int b, bool checked) {
                auto *mw = mapWidget_;
                if (!mw) return;

                if (g == viewGrp) {
                    auto type = static_cast<Mr::OtherType>(b + Mr::Grid);
                    mw->setOther(type, checked);
                    mw->update();
                } else if (g == dimGrp && checked) {
                    mw->setDim(static_cast<Mr::DimType>(b));
                    mw->update();
                } else if (g == layerGrp && checked) {
                    mw->setLayer(static_cast<Mr::LayerType>(b));
                    mw->update();
                } else if (g == overlayGrp) {
                    auto type = static_cast<Mr::OtherType>(b + Mr::SlimeChunk);
                    mw->setOther(type, checked);
                    mw->update();
                } else if (g == actionGrp && checked) {
                    if (b == 0) {
                        openFilterDialog();
                    } else if (b == 1) {
                        toggleGlobalDataWidget();
                    }
                }
            });

    // default: grid on
    toolbar_->setButtonChecked(viewGrp, 0, true);
    // default terrain (index 0 = map/terrain, index 1 = biome)
    toolbar_->setButtonChecked(layerGrp, 0, true);
}

void LevelPageWidget::syncToolbars() {
    if (!mapWidget_) return;

    using Mr = MapWidget::RenderOption;
    auto opt = mapWidget_->renderOption();

    // View group
    if (auto *tb = toolbar_) {
        tb->blockSignals(true);
        tb->setButtonChecked(tb_view_grp_, 0, opt.getOther(Mr::Grid));
        tb->setButtonChecked(tb_view_grp_, 1, opt.getOther(Mr::Coords));
        // Dimension group
        for (int i = 0; i < 3; ++i) tb->setButtonChecked(tb_dim_grp_, i, static_cast<int>(opt.dim) == i);
        // Layer group
        for (int i = 0; i < 3; ++i) tb->setButtonChecked(tb_layer_grp_, i, static_cast<int>(opt.layer) == i);
        // Overlay group
        tb->setButtonChecked(tb_overlay_grp_, 0, opt.getOther(Mr::SlimeChunk));
        tb->setButtonChecked(tb_overlay_grp_, 1, opt.getOther(Mr::Actors));
        tb->setButtonChecked(tb_overlay_grp_, 2, opt.getOther(Mr::Village));
        tb->setButtonChecked(tb_overlay_grp_, 3, opt.getOther(Mr::HSA));
        tb->blockSignals(false);
    }

    // Selection toolbar
    if (auto *stb = selection_toolbar_) {
        stb->blockSignals(true);
        auto mode = mapWidget_->selection().mode();
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
    map_item_editor_ = new MapItemEditor(nbtTabWidget_);

    nbtTabWidget_->addTab(level_dat_editor_, "level.dat");
    nbtTabWidget_->addTab(player_editor_, "players");
    nbtTabWidget_->addTab(village_editor_, "villages");
    nbtTabWidget_->addTab(other_nbt_editor_, "other");
    nbtTabWidget_->addTab(map_item_editor_, "Map");
    nbtTabWidget_->setTabPosition(QTabWidget::West);

    for (auto *editor : {level_dat_editor_, player_editor_, village_editor_, other_nbt_editor_, map_item_editor_->nbtEditor()}) {
        connect(editor, &NbtWidget::nbtModified, this, &LevelPageWidget::refreshDirty);
        connect(editor, &NbtWidget::nbtModified, this, [this, editor]() {
            QWidget *realTab = editor;
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
}

QString LevelPageWidget::getLevelName() {
    if (!level_loader_ || !level_loader_->isOpen()) return {};
    return QString::fromStdString(level_loader_->level().dat().level_name());
}

bool LevelPageWidget::isDirty() const {
    for (auto *editor : {level_dat_editor_, player_editor_, village_editor_, other_nbt_editor_, map_item_editor_->nbtEditor()}) {
        if (editor && editor->dirty()) return true;
    }
    return level_loader_->isDirty();
}

void LevelPageWidget::refreshDirty() {
    auto dirty = isDirty();
    auto *tw = qobject_cast<LevelTabWidget *>(parent_);
    if (!tw) return;
    int i = tw->indexOf(this);
    if (i < 0) return;
    auto name = getLevelName();
    if (name.isEmpty()) name = QString("Level %1").arg(tab_id_);
    tw->setTabText(i, dirty ? name + " *" : name);
}

void LevelPageWidget::commit() {
    LOG_F(INFO, "Commit modifications");

    //    sync level.dat change
    auto nbts = this->level_dat_editor_->getPaletteCopy();
    if (nbts.size() == 1 && nbts[0]) {
        this->level_loader_->modifyLeveldat(nbts[0]);
        nbts[0] = nullptr;  // ownership transferred to modifyLeveldat
    } else {
        LOG_F(WARNING, "level.dat is not invalid, skip saving level.dat");
    }

    std::unordered_map<std::string, std::string> allModifies;
    for (auto *editor : {player_editor_, village_editor_, other_nbt_editor_, map_item_editor_->nbtEditor()}) {
        if (editor) {
            for (auto &kv : editor->getModifyCache()) {
                allModifies[kv.first] = kv.second;
            }
        }
    }

    if (!allModifies.empty()) {
        this->level_loader_->modifyDBGlobal(allModifies);
    }
    player_editor_->clearModifyCache();
    village_editor_->clearModifyCache();
    other_nbt_editor_->clearModifyCache();
    map_item_editor_->nbtEditor()->clearModifyCache();
    level_loader_->commit();
    refreshDirty();
}

bool LevelPageWidget::loadLevel(const QString &path) {
    auto ret = level_loader_->open(path.toStdString());
    if (!ret) {
        LOG_F(WARNING, "Can not open level: %s", path.toStdString().c_str());
        return false;
    }
    auto &dat = level_loader_->level().dat();
    LOG_F(INFO, "Open level %s with version %s", dat.level_name().c_str(), dat.min_compat_version().to_string().c_str());
    auto *ld = dynamic_cast<bl::palette::compound_tag *>(dat.root());
    this->level_dat_editor_->loadNewData({NBTListItem::from(dynamic_cast<bl::palette::compound_tag *>(ld->copy()), "level.dat")});
    setLevelStatusBar(path + "  " + dat.min_compat_version().to_string().c_str());
    if (!setting::LOAD_GLOBAL_DATA) {
        return true;
    }
    auto future = QtConcurrent::run(
        [this](const QString &path) {
            try {
                level_loader_->loadGlobalData(std::ref(this->global_data_), std::ref(stop_loading_global_data_));
                LOG_F(INFO, "Load global data finished");
                return true;
            } catch (std::exception &e) {
                LOG_F(WARNING, "Can not fully load global data: %s", e.what());
                return false;
            }
        },
        path);
    this->load_global_data_watcher_.setFuture(future);
    return true;
}

void LevelPageWidget::closeLevel() {
    this->stop_loading_global_data_ = true;
    load_global_data_watcher_.waitForFinished();
    if (level_loader_->isOpen()) level_loader_->close();
}

void LevelPageWidget::toggleGlobalDataWidget() {
    if (!nbtTabWidget_) return;
    auto vis = nbtTabWidget_->isVisible();
    nbtTabWidget_->setVisible(!vis);
}

void LevelPageWidget::openFilterDialog() {
    auto *loader = level_loader_.get();
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

void LevelPageWidget::showChunkEditor(const bl::chunk_pos &pos) {
    auto opt = level_loader_->getRawChunk(pos);
    if (!opt || !opt->loaded()) {
        WARN(msg::NO_CHUNK_FOUND());
        return;
    }

    // if the chunk editor has unsaved changes, prompt the user
    if (chunkWidget_->isVisible() && chunkWidget_->isDirty()) {
        auto btn = QMessageBox::question(this, msg::UNSAVED_CHANGES(), msg::UNSAVED_CHANGES_PROMPT(),
                                         QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (btn == QMessageBox::Cancel) return;
        if (btn == QMessageBox::Yes) chunkWidget_->saveChunk();
    }

    chunkWidget_->loadChunkData(std::move(*opt));
    mapWidget_->selectChunk(pos);
    int totalW = mainSplitter_->width();
    int chunkW = totalW / 3;
    if (chunkW < 300) chunkW = 300;
    mainSplitter_->setSizes({totalW - chunkW, chunkW});
    chunkWidget_->show();
}

void LevelPageWidget::collectVillagesGuiData(const bl::village_data::village_table_type &vs) {
    for (int i = 0; i < vs.size(); i++) {
        auto &villsInDim = vs[i];
        for (auto kv : villsInDim) {
            auto *nbt = kv.second[static_cast<int>(bl::village_key::key_type::INFO)];
            if (!nbt) continue;
            auto x0 = dynamic_cast<bl::palette::int_tag *>(nbt->get("X0"));
            auto z0 = dynamic_cast<bl::palette::int_tag *>(nbt->get("Z0"));
            auto x1 = dynamic_cast<bl::palette::int_tag *>(nbt->get("X1"));
            auto z1 = dynamic_cast<bl::palette::int_tag *>(nbt->get("Z1"));
            if (!x0 || !z0 || !x1 || !z1) continue;
            auto pos0 = bl::block_pos(x0->value, 0, z0->value);
            auto pos1 = bl::block_pos(x1->value, 0, z1->value);
            this->villages_.insert(kv.first.c_str(), VillageDrawInfo{pos0, pos1, i});
        }
    }
}

void LevelPageWidget::fillGlobalData(GlobalNBTLoadResult &res) {
    LOG_F(INFO, "Filling player data (%zu)...", res.playerData.data().size());
    auto &playerData = res.playerData.data();
    std::vector<NBTListItem *> playerNBTList;
    for (auto &kv : playerData) {
        auto *item = NBTListItem::from(dynamic_cast<compound_tag *>(kv.second->copy()), kv.first.c_str(), kv.first.c_str());
        item->setIcon(QIcon(QPixmap::fromImage(*PlayerNBTIcon())));
        playerNBTList.push_back(item);
    }
    this->player_editor_->loadNewData(playerNBTList);

    LOG_F(INFO, "Filling other data (%zu)...", res.otherData.data().size());
    auto &otherData = res.otherData.data();
    std::vector<NBTListItem *> otherNBTList;
    for (auto &kv : otherData) {
        auto *item = NBTListItem::from(dynamic_cast<compound_tag *>(kv.second->copy()), kv.first.c_str(), kv.first.c_str());
        item->setIcon(QIcon(QPixmap::fromImage(*OtherNBTIcon())));
        otherNBTList.push_back(item);
    }
    this->other_nbt_editor_->loadNewData(otherNBTList);

    LOG_F(INFO, "Filling village data (%zu)...", res.villageData.data().size());
    auto &villData = res.villageData.data();
    this->collectVillagesGuiData(villData);
    std::vector<NBTListItem *> villNBTList;
    for (const auto &dim : villData) {
        for (const auto &kv : dim) {
            int index = 0;
            for (auto &p : kv.second) {
                if (p) {
                    auto key = kv.first + "_" + bl::village_key::village_key_type_to_str(static_cast<bl::village_key::key_type>(index));
                    auto *item = NBTListItem::from(dynamic_cast<compound_tag *>(p->copy()), key.c_str(), ("VILLAGE_" + key).c_str());
                    item->setIcon(QIcon(QPixmap::fromImage(*VillageNBTIcon(static_cast<bl::village_key::key_type>(index)))));
                    villNBTList.push_back(item);
                }
                index++;
            }
        }
    }
    this->village_editor_->loadNewData(villNBTList);

    LOG_F(INFO, "Filling map data (%zu)...", res.mapData.data().size());
    this->map_item_editor_->load_map_data(res.mapData);
}

void LevelPageWidget::onLoadGlobalDataFinished() {
    const auto res = this->load_global_data_watcher_.result();
    auto msg = res ? msg::OPEN_LEVEL_SUCC() : msg::LOAD_GLOBAL_DATA_FAILED();
    fillGlobalData(this->global_data_);
    this->global_data_.clear();
}
