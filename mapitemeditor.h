#ifndef MAPITEMEDITOR_H
#define MAPITEMEDITOR_H

#include <QWidget>
#include "palette.h"
#include "nbtwidget.h"
#include "global.h"

namespace Ui {
    class MapItemEditor;
}

class MapItemEditor : public QWidget {
Q_OBJECT

public:
    explicit MapItemEditor(QWidget *parent = nullptr);

    ~MapItemEditor() override;

    void load_map_data(const bl::general_kv_nbts &data);

    void paintEvent(QPaintEvent *event) override;

private slots:

    void on_export_map_btn_clicked();

    void on_change_map_btn_clicked();

    void on_save_map_btn_clicked();

private:
    NbtWidget *map_nbt_editor_{nullptr};
    Ui::MapItemEditor *ui;
    QImage img;
};


#endif // MAPITEMEDITOR_H
