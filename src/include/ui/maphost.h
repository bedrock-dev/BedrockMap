#ifndef BEDROCKMAP_MAPHOST_H
#define BEDROCKMAP_MAPHOST_H

#include <qmap.h>
#include <qobject.h>
#include <qtmetamacros.h>
#include <qwidget.h>

#include <QByteArray>
#include <QString>
#include <memory>
#include <optional>
#include <tuple>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "gotopositiondialog.h"
#include "guitaskrunner.h"
#include "importoverlay.h"
#include "mapoverlays.h"
#include "mapview.h"
#include "render_options.h"
#include "selectioncontroller.h"

class ExportedRegion;
class VoxelPreviewWidget;

/// The owner of a map's shared state and of the level operations behind it.
///
/// It is not a widget. The map is *drawn* by a renderer - CpuMapWidget for the CPU
/// painter, GpuMapWidget for the GL one - and the two are alternatives, so
/// everything that has to survive a renderer switch, or that two renderers of
/// one view would otherwise have to keep in sync, lives here instead: the
/// MapView, the MapOverlays, the ImportOverlay and the editing actions.
///
/// The renderers are peers: both take this object plus the same shared pieces,
/// and neither owns the other. The right-click menu and the import overlay act
/// on this class, so each of their operations has exactly one implementation
/// no matter which renderer is on screen.
class MapHost : public QObject {
    Q_OBJECT

   public:
    MapHost(QWidget* page, AsyncLevelLoader* loader);

    ~MapHost() override;

    // --- the shared pieces the renderers draw from ---

    [[nodiscard]] MapView* mapView() { return &view_; }
    [[nodiscard]] MapOverlays& overlays() { return overlays_; }
    [[nodiscard]] ImportOverlay* importOverlay() { return import_overlay_.get(); }
    [[nodiscard]] AsyncLevelLoader* levelLoader() { return level_loader_; }
    [[nodiscard]] SelectionController& selection() { return view_.selection(); }
    [[nodiscard]] RenderOption renderOption() const { return view_.options(); }

    /// The pane on screen. Dialogs, warnings and screenshots belong to it, and
    /// it is the only widget there is: the page constructs one renderer.
    void setPaneWidget(QWidget* pane) { pane_ = pane; }
    [[nodiscard]] QWidget* paneWidget() const { return pane_; }

    // --- view state ---

    void setDim(int dim) { view_.setDim(dim); }
    void setLayer(RenderOption::LayerType layer) { view_.setLayer(layer); }
    void setOther(RenderOption::OtherType other, bool value) { view_.setOther(other, value); }
    bool toggleOther(RenderOption::OtherType other);

    // --- overlay and loader toggles ---

    void setDrawDebug(bool enable);
    [[nodiscard]] bool isDebugEnabled() const { return draw_debug_window_; }
    void setCoordsMiniMap(bool enable);
    bool toggleCoordsMiniMap();
    void setTransparentVoid(bool v);
    bool toggleTransparentVoid();

    void setVillages(const QMap<QString, VillageDrawInfo>& villages);
    void setPortals(const QVector<PortalDrawInfo>& portals);

    // --- the chunk the chunk editor is showing ---

    void selectChunk(const bl::chunk_pos& pos) { view_.setOpenedChunk(pos); }
    void unselectChunk() { view_.clearOpenedChunk(); }

    // --- selection ---

    void setSelectionMode(SelectionController::Mode mode) { view_.setSelectionMode(mode); }

    // --- navigation ---

    void gotoBlockPos(int x, int z);
    void gotoPositionAction();

    // --- selection and level operations ---

    void clearSelection();
    void copySelectionToClipboard(int dim);
    void pasteFromClipboard(int dim);
    void exportSelectionToFile(int dim);
    void exportSelectionToMcstructure(int dim, bool compress = false, bool exportEntities = false,
                                      const std::optional<bl::block_box>& blockBounds = std::nullopt, int32_t version = 1);
    void importFromFile(int dim);
    void deleteSelection(int dim);
    void createVoidSelection(int dim);
    void setSelectionBiome(int biome, int dim);
    void applyImportedRegion(ExportedRegion region);

    void saveSelectionImage(QWidget* source);
    void saveFullscreenImage(QWidget* source);
    bool beginPaste(const QByteArray& data, int dim, const bl::chunk_pos& at);
    bool beginImport(const QString& path, int dim, const bl::chunk_pos& at);
    void showVoxelPreview(const bl::chunk_pos& min, const bl::chunk_pos& max);
    void showChunkEditor(const bl::chunk_pos& pos);
    void show3DView(int dim);

    /// Ask the page to re-sync its toolbars from the shared view.
    void syncToolbars() { emit syncToolbarsRequested(); }

    [[nodiscard]] bool chunkEditRunning() const { return chunk_edit_task_.isRunning(); }
    void waitForChunkEditTask() { chunk_edit_task_.waitForFinished(); }

   signals:
    /// The chrome around the map should follow this.
    void toolbarsVisibleRequested(bool visible);
    void syncToolbarsRequested();

    /// Ask the page to open the chunk editor.
    void requestOpenChunkEditor(const bl::chunk_pos& pos);

    /// The shared selection changed; the status bar shows the count.
    void selectionChanged();

   private:
    bool startChunkTask(GuiTaskRunner::Worker worker);
    AsyncLevelLoader* level_loader_{nullptr};
    QWidget* pane_{nullptr};

    MapView view_;
    MapOverlays overlays_;
    std::unique_ptr<ImportOverlay> import_overlay_;

    GoToPositionDialog* goto_dialog_{nullptr};
    VoxelPreviewWidget* voxel_preview_window_{nullptr};

    GuiTaskRunner chunk_edit_task_;
    // true while an edit that changed the 3D preview's chunks is in flight
    bool reload_voxel_preview_pending_{false};

    bool draw_debug_window_{false};
    bool draw_coords_minimap_{false};
    bool transparent_void_{false};
};

#endif  // BEDROCKMAP_MAPHOST_H
