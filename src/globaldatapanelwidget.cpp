#include "globaldatapanelwidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <algorithm>

#include "color.h"
#include "loguru/loguru.hpp"
#include "mcstructure.h"
#include "resourcemanager.h"
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

    QIcon imageIcon(QImage* image) { return image ? QIcon(QPixmap::fromImage(*image)) : QIcon(); }

}  // namespace

StructureEditorWidget::StructureEditorWidget(QWidget* parent) : QWidget(parent) {
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    nbt_editor_ = new NbtWidget(splitter);
    nbt_editor_->setMode(NbtMode::Memory);
    nbt_editor_->setReadOnly(true);
    nbt_editor_->setMinimumWidth(0);

    preview_stack_ = new QStackedWidget(splitter);
    preview_stack_->setMinimumWidth(0);
    preview_status_ = new QLabel(preview_stack_);
    preview_status_->setAlignment(Qt::AlignCenter);
    preview_status_->setWordWrap(true);
    preview_status_->setText(tr("levelPageWidget.structurePreview.noData"));
    voxel_widget_ = new VoxelWidget(preview_stack_);
    voxel_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    preview_stack_->addWidget(preview_status_);
    preview_stack_->addWidget(voxel_widget_);
    preview_stack_->setCurrentWidget(preview_status_);

    splitter->addWidget(nbt_editor_);
    splitter->addWidget(preview_stack_);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);
    // QSplitter initially uses child size hints, which makes the NBT editor
    // consume almost all space because its toolbar is wide. Establish the
    // intended 2:1 ratio explicitly; the stretch factors preserve it while
    // the panel is resized.
    splitter->setSizes({600, 300});

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    connect(nbt_editor_, &NbtWidget::itemOpened, this, &StructureEditorWidget::onItemOpened);
}

void StructureEditorWidget::loadStructureData(const bl::general_kv_nbts& data) {
    std::vector<NBTListItem*> items;
    items.reserve(data.data().size());
    for (const auto& kv : data.data()) {
        auto* item = NBTListItem::from(dynamic_cast<bl::nbt::compound_tag*>(kv.second->copy()), kv.first.c_str(), kv.first.c_str());
        item->setIcon(imageIcon(OtherNBTIcon()));
        items.push_back(item);
    }

    nbt_editor_->loadNewData(items);
    preview_stack_->setCurrentWidget(preview_status_);
    preview_status_->setText(items.empty() ? tr("levelPageWidget.structurePreview.noData")
                                           : tr("levelPageWidget.structurePreview.selectStructure"));
    if (!items.empty()) nbt_editor_->openItem(0);
}

void StructureEditorWidget::clearData() {
    nbt_editor_->clearData();
    preview_stack_->setCurrentWidget(preview_status_);
    preview_status_->setText(tr("levelPageWidget.structurePreview.noData"));
}

void StructureEditorWidget::onItemOpened(NBTListItem* item) {
    if (!item || !item->root_) {
        preview_stack_->setCurrentWidget(preview_status_);
        preview_status_->setText(tr("levelPageWidget.structurePreview.invalid"));
        return;
    }

    const auto raw = item->root_->to_raw();
    const auto structure = bl::parse_mcstructure(reinterpret_cast<const byte_t*>(raw.data()), raw.size());
    if (structure.size_x() <= 0 || structure.size_y() <= 0 || structure.size_z() <= 0) {
        preview_stack_->setCurrentWidget(preview_status_);
        preview_status_->setText(tr("levelPageWidget.structurePreview.invalid"));
        return;
    }

    preview_stack_->setCurrentWidget(voxel_widget_);
    voxel_widget_->updateVoxelData(buildVoxelDataFromMcstructure(structure));
}

GlobalDataPanelWidget::GlobalDataPanelWidget(QWidget* parent) : QWidget(parent) {
    tab_widget_ = new QTabWidget(this);
    tab_widget_->setContentsMargins(0, 0, 0, 0);
    tab_widget_->setTabPosition(QTabWidget::West);
    tab_widget_->setIconSize(QSize(22, 22));
    tab_widget_->tabBar()->setExpanding(false);
    tab_widget_->tabBar()->setUsesScrollButtons(true);

    level_dat_editor_ = new NbtWidget(tab_widget_);
    player_editor_ = new NbtWidget(tab_widget_);
    village_editor_ = new NbtWidget(tab_widget_);
    portal_editor_ = new NbtWidget(tab_widget_);
    other_nbt_editor_ = new NbtWidget(tab_widget_);
    structures_editor_ = new StructureEditorWidget(tab_widget_);
    map_item_editor_ = new MapItemEditor(tab_widget_);
    for (auto* editor : {level_dat_editor_, player_editor_, village_editor_, portal_editor_, other_nbt_editor_}) {
        editor->setMode(NbtMode::Memory);
    }
    level_dat_editor_->setListVisible(false);
    portal_editor_->setListVisible(false);

    addEditorTab(level_dat_editor_, level_dat_editor_, tr("levelPageWidget.globalData.levelDat"));
    addEditorTab(player_editor_, player_editor_, tr("levelPageWidget.globalData.players"));
    addEditorTab(village_editor_, village_editor_, tr("levelPageWidget.globalData.villages"));
    addEditorTab(portal_editor_, portal_editor_, tr("levelPageWidget.globalData.portals"));
    addEditorTab(structures_editor_, structures_editor_->nbtEditor(), tr("levelPageWidget.globalData.structures"));
    addEditorTab(map_item_editor_, map_item_editor_->nbtEditor(), tr("levelPageWidget.globalData.maps"));
    addEditorTab(other_nbt_editor_, other_nbt_editor_, tr("levelPageWidget.globalData.other"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tab_widget_);
    hide();
}

void GlobalDataPanelWidget::addEditorTab(QWidget* tab, NbtWidget* editor, const QString& label) {
    tab_widget_->addTab(tab, label);
    const int index = tab_widget_->indexOf(tab);
    tab_widget_->setTabToolTip(index, label);
    editor_tabs_.push_back(EditorTab{tab, editor, label});
    connect(editor, &NbtWidget::nbtModified, this, [this, editor] {
        for (const auto& entry : editor_tabs_) {
            if (entry.editor == editor) {
                updateTabIcon(entry);
                break;
            }
        }
        emit dirtyChanged();
    });
}

void GlobalDataPanelWidget::updateTabIcon(const EditorTab& entry) {
    const int index = tab_widget_->indexOf(entry.tab);
    if (index < 0) return;
    tab_widget_->setTabIcon(index, QIcon());
    tab_widget_->setTabText(index, entry.tooltip + (entry.editor->dirty() ? " *" : QString()));
}

void GlobalDataPanelWidget::loadLevelDat(const bl::nbt::compound_tag* root) {
    if (!root) return;
    level_dat_editor_->loadNewData({NBTListItem::from(dynamic_cast<bl::nbt::compound_tag*>(root->copy()), "level.dat")});
    level_dat_editor_->openItem(0);
    for (const auto& entry : editor_tabs_) updateTabIcon(entry);
}

void GlobalDataPanelWidget::collectVillages(const bl::village_data::village_table_type& data) {
    villages_.clear();
    for (int dim = 0; dim < static_cast<int>(data.size()); ++dim) {
        for (const auto& [key, records] : data[dim]) {
            const auto* info = records[static_cast<size_t>(bl::village_key::INFO)];
            if (!info) continue;
            const auto* x0 = dynamic_cast<const bl::nbt::int_tag*>(info->get("X0"));
            const auto* z0 = dynamic_cast<const bl::nbt::int_tag*>(info->get("Z0"));
            const auto* x1 = dynamic_cast<const bl::nbt::int_tag*>(info->get("X1"));
            const auto* z1 = dynamic_cast<const bl::nbt::int_tag*>(info->get("Z1"));
            if (!x0 || !z0 || !x1 || !z1) continue;
            villages_.insert(QString::fromStdString(key), VillageDrawInfo{{x0->value, 0, z0->value}, {x1->value, 0, z1->value}, dim});
        }
    }
}

void GlobalDataPanelWidget::collectPortals(const bl::nbt::compound_tag* portals) {
    portals_.clear();
    if (!portals) return;
    const auto* data = portals->get("data");
    const auto* data_compound = data ? data->as<const bl::nbt::compound_tag*>() : nullptr;
    const auto* records = data_compound ? data_compound->get("PortalRecords") : nullptr;
    const auto* record_list = records ? records->as<const bl::nbt::list_tag*>() : nullptr;
    if (!record_list) return;

    for (const auto* item : record_list->value) {
        if (!item) continue;
        const auto* record = item->as<const bl::nbt::compound_tag*>();
        if (!record) continue;
        const auto* dim_tag = record->get("DimId");
        const auto* x_tag = record->get("TpX");
        const auto* y_tag = record->get("TpY");
        const auto* z_tag = record->get("TpZ");
        const auto* dim = dim_tag ? dim_tag->as<const bl::nbt::int_tag*>() : nullptr;
        const auto* x = x_tag ? x_tag->as<const bl::nbt::int_tag*>() : nullptr;
        const auto* y = y_tag ? y_tag->as<const bl::nbt::int_tag*>() : nullptr;
        const auto* z = z_tag ? z_tag->as<const bl::nbt::int_tag*>() : nullptr;
        if (!dim || !x || !y || !z) continue;
        portals_.append(PortalDrawInfo{{x->value, y->value, z->value}, dim->value});
    }
}

void GlobalDataPanelWidget::loadGlobalData(GlobalNBTLoadResult& result) {
    LOG_F(INFO, "Filling player data (%llu)...", static_cast<unsigned long long>(result.playerData.data().size()));
    std::vector<NBTListItem*> player_items;
    for (const auto& [key, value] : result.playerData.data()) {
        auto* item = NBTListItem::from(dynamic_cast<bl::nbt::compound_tag*>(value->copy()), key.c_str(), key.c_str());
        item->setIcon(imageIcon(PlayerNBTIcon()));
        player_items.push_back(item);
    }
    player_editor_->loadNewData(player_items);

    LOG_F(INFO, "Filling other data (%llu)...", static_cast<unsigned long long>(result.otherData.data().size()));
    std::vector<NBTListItem*> other_items;
    for (const auto& [key, value] : result.otherData.data()) {
        if (key == "portals") continue;
        auto* item = NBTListItem::from(dynamic_cast<bl::nbt::compound_tag*>(value->copy()), key.c_str(), key.c_str());
        item->setIcon(imageIcon(OtherNBTIcon()));
        other_items.push_back(item);
    }
    other_nbt_editor_->loadNewData(other_items);

    LOG_F(INFO, "Filling village data (%llu)...", static_cast<unsigned long long>(result.villageData.data().size()));
    collectVillages(result.villageData.data());
    std::vector<NBTListItem*> village_items;
    for (const auto& dimension : result.villageData.data()) {
        for (const auto& [key, records] : dimension) {
            for (size_t index = 0; index < records.size(); ++index) {
                auto* record = records[index];
                if (!record) continue;
                const auto type = static_cast<bl::village_key::key_type>(index);
                const auto item_key = key + "_" + bl::village_key::village_key_type_to_str(type);
                auto* item = NBTListItem::from(dynamic_cast<bl::nbt::compound_tag*>(record->copy()), item_key.c_str(),
                                               ("VILLAGE_" + item_key).c_str());
                item->setIcon(imageIcon(VillageNBTIcon(type)));
                village_items.push_back(item);
            }
        }
    }
    village_editor_->loadNewData(village_items);

    LOG_F(INFO, "Filling portal data (%llu)...", static_cast<unsigned long long>(result.portalData.data().size()));
    const auto portal_it = result.portalData.data().find("portals");
    const auto* portal_data = portal_it == result.portalData.data().end() ? nullptr : portal_it->second;
    collectPortals(portal_data);
    std::vector<NBTListItem*> portal_items;
    if (portal_data) {
        auto* item = NBTListItem::from(dynamic_cast<bl::nbt::compound_tag*>(portal_data->copy()), "portals", "portals");
        item->setIcon(imageIcon(PortalImage()));
        portal_items.push_back(item);
    }
    portal_editor_->loadNewData(portal_items);
    if (!portal_items.empty()) portal_editor_->openItem(0);

    LOG_F(INFO, "Filling map data (%llu)...", static_cast<unsigned long long>(result.mapData.data().size()));
    map_item_editor_->load_map_data(result.mapData);

    LOG_F(INFO, "Filling structures data (%llu)...", static_cast<unsigned long long>(result.structuresData.data().size()));
    structures_editor_->loadStructureData(result.structuresData);
    for (const auto& entry : editor_tabs_) updateTabIcon(entry);
}

bool GlobalDataPanelWidget::isDirty() const {
    return std::any_of(editor_tabs_.begin(), editor_tabs_.end(),
                       [](const EditorTab& entry) { return entry.editor && entry.editor->dirty(); });
}

std::unordered_map<std::string, std::string> GlobalDataPanelWidget::collectModifyCache() const {
    std::unordered_map<std::string, std::string> result;
    for (const auto& entry : editor_tabs_) {
        if (!entry.editor || entry.editor == level_dat_editor_) continue;
        for (const auto& [key, value] : entry.editor->getModifyCache()) result[key] = value;
    }
    return result;
}

void GlobalDataPanelWidget::clearModifyCache() {
    for (const auto& entry : editor_tabs_) {
        if (entry.editor) entry.editor->clearModifyCache();
        updateTabIcon(entry);
    }
}
