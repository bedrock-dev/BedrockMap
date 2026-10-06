#ifndef BEDROCKMAP_GLOBALDATAPANELWIDGET_H
#define BEDROCKMAP_GLOBALDATAPANELWIDGET_H

#include <QIcon>
#include <QMap>
#include <QTabWidget>
#include <QVector>
#include <QWidget>

#include <string>
#include <unordered_map>
#include <vector>

#include "asynclevelloader.h"
#include "mapitemeditor.h"
#include "mapoverlays.h"
#include "nbtwidget.h"

class QLabel;
class QStackedWidget;
class VoxelWidget;

/// Read-only global structure viewer: NBT on the left and a voxel preview on
/// the right. The NBT editor remains exposed so GlobalDataPanelWidget can keep
/// one consistent dirty/commit path for all global data.
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

/// The global-data area of one LevelPageWidget.
///
/// LevelPageWidget remains the top-level document for one save. This widget
/// only encapsulates the growing collection of global-data editors, their tab
/// chrome, dirty state, and conversion of loaded NBT into editor/map data.
class GlobalDataPanelWidget : public QWidget {
    Q_OBJECT

   public:
    explicit GlobalDataPanelWidget(QWidget* parent = nullptr);

    void loadLevelDat(const bl::nbt::compound_tag* root);
    void loadGlobalData(GlobalNBTLoadResult& result);

    [[nodiscard]] bool isDirty() const;
    [[nodiscard]] std::unordered_map<std::string, std::string> collectModifyCache() const;
    void clearModifyCache();

    [[nodiscard]] const QMap<QString, VillageDrawInfo>& villages() const { return villages_; }
    [[nodiscard]] const QVector<PortalDrawInfo>& portals() const { return portals_; }
    [[nodiscard]] NbtWidget* levelDatEditor() const { return level_dat_editor_; }

   signals:
    void dirtyChanged();

   private:
    struct EditorTab {
        QWidget* tab{nullptr};
        NbtWidget* editor{nullptr};
        QString tooltip;
    };

    void addEditorTab(QWidget* tab, NbtWidget* editor, const QString& label);
    void updateTabIcon(const EditorTab& entry);
    void collectVillages(const bl::village_data::village_table_type& data);
    void collectPortals(const bl::nbt::compound_tag* portals);

    QTabWidget* tab_widget_{nullptr};
    NbtWidget* level_dat_editor_{nullptr};
    NbtWidget* player_editor_{nullptr};
    NbtWidget* village_editor_{nullptr};
    NbtWidget* portal_editor_{nullptr};
    NbtWidget* other_nbt_editor_{nullptr};
    StructureEditorWidget* structures_editor_{nullptr};
    MapItemEditor* map_item_editor_{nullptr};
    std::vector<EditorTab> editor_tabs_;
    QMap<QString, VillageDrawInfo> villages_;
    QVector<PortalDrawInfo> portals_;
};

#endif  // BEDROCKMAP_GLOBALDATAPANELWIDGET_H
