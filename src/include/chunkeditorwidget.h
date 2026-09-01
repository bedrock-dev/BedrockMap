#ifndef CHUNKEDITORWIDGET_H
#define CHUNKEDITORWIDGET_H

#include <QWidget>
#include <cstddef>
#include <functional>

#include "asynclevelloader.h"
#include "chunk.h"
#include "voxelwidget.h"

namespace Ui {

    class ChunkEditorWidget;

}  // namespace Ui
class ChunkSectionWidget;

class NbtWidget;

class HsaEditorWidget;

class MainWindow;

class QStackedWidget;

class ChunkEditorWidget : public QWidget {
    Q_OBJECT
   public:
    explicit ChunkEditorWidget(QWidget *parent = nullptr, AsyncLevelLoader *levelLoader = nullptr);

    ~ChunkEditorWidget() override;

    void loadChunkData(bl::raw_chunk raw);

    void mousePressEvent(QMouseEvent *event) override;

    void clearData();

    bool isDirty() const { return dirty_; }
    bool saveChunk();

    void hideEvent(QHideEvent *event) override;

   signals:
    void locateChunk(int x, int z, int dim);
    void editorClosed();

   private slots:
    void on_terrain_level_edit_valueChanged(int arg1);

   private slots:
    void on_terrain_show_grid_cb_stateChanged(int arg1);

   private slots:

    void on_close_btn_clicked();

    void on_terrain_level_slider_valueChanged(int value);

    void on_locate_btn_clicked();

    void on_export_btn_clicked();

    void on_import_btn_clicked();

    void on_view_3d_btn_clicked();

    void on_save_btn_clicked();

   private:
    void refreshBasicData();

    void setDirty(bool bo) { dirty_ = bo; }

    QWidget *makeOversizePlaceholder(const QString &msg, const std::function<void()> &onDelete);

    void setTabDirtyText(QWidget *tab);

    void deleteBlockEntityData();

    void deletePendingTickData();

    void deleteActorData();

   private:
    ChunkSectionWidget *chunk_section_{nullptr};

    NbtWidget *actor_editor_{nullptr};
    NbtWidget *block_entity_editor_{nullptr};
    NbtWidget *pending_tick_editor_{nullptr};
    QStackedWidget *actor_stack_{nullptr};
    QStackedWidget *block_entity_stack_{nullptr};
    QStackedWidget *pending_tick_stack_{nullptr};
    HsaEditorWidget *hsa_editor_{nullptr};
    VoxelWidget *terrain_render_widget_{nullptr};

    Ui::ChunkEditorWidget *ui;
    AsyncLevelLoader *level_loader_{nullptr};
    int y_level{0};
    bl::ChunkVersion cv{bl::Old};
    bl::chunk_pos cp_;
    bl::raw_chunk raw_chunk_;
    bool has_chunk_{false};
    bool dirty_{false};
};

#endif  // CHUNKEDITORWIDGET_H
