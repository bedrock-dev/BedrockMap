#ifndef BEDROCKMAP_HSAEDITORWIDGET_H
#define BEDROCKMAP_HSAEDITORWIDGET_H

#include <QWidget>
#include <vector>

#include "bedrock_key.h"

class QTableWidget;
class QComboBox;

// 16x16 top-down view of the hardcoded spawn areas of one chunk
class HsaGridWidget : public QWidget {
    Q_OBJECT
   public:
    explicit HsaGridWidget(QWidget *parent = nullptr);

    void setData(const std::vector<bl::hardcoded_spawn_area> &areas);
    void setChunkOrigin(int cx, int cz);
    void setSelectedIndex(int idx);

   protected:
    void paintEvent(QPaintEvent *event) override;

   private:
    std::vector<bl::hardcoded_spawn_area> areas_;
    int cx_{0};
    int cz_{0};
    int selected_{-1};
};

// editor for the hardcoded spawn areas of a chunk: grid preview + editable table
class HsaEditorWidget : public QWidget {
    Q_OBJECT
   public:
    explicit HsaEditorWidget(QWidget *parent = nullptr);

    void setChunk(const bl::chunk_pos &cp);
    void setData(const std::vector<bl::hardcoded_spawn_area> &areas);
    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] bool dirty() const { return dirty_; }
    void markClean();
    void clearData();

   signals:
    void hsaModified();

   private slots:
    void on_add_btn_clicked();
    void on_remove_btn_clicked();
    void on_table_cellChanged(int row, int column);
    void on_type_changed(int index);

   private:
    void rebuildTable();
    void syncFromTable();
    void markDirty();

    bl::chunk_pos cp_;
    bl::hardcoded_spawn_area_list list_;
    bool dirty_{false};
    bool filling_{false};
    QTableWidget *table_{nullptr};
    HsaGridWidget *grid_{nullptr};
};

#endif  // BEDROCKMAP_HSAEDITORWIDGET_H
