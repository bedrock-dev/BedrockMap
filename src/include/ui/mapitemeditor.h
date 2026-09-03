#ifndef BEDROCKMAP_MAPITEMEDITOR_H
#define BEDROCKMAP_MAPITEMEDITOR_H

#include <QDialog>
#include <QDialogButtonBox>
#include <QImage>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QWidget>
#include <Qt>

#include "global.h"
#include "nbt.h"
#include "nbtwidget.h"


namespace Ui {
    class MapItemEditor;
}

/// A dialog that lets the user drag/resize a square crop region over an image.
class ImageCropDialog : public QDialog {
    Q_OBJECT
   public:
    explicit ImageCropDialog(const QImage &source, QWidget *parent = nullptr);

    QImage croppedResult() const;

   protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

   private:
    enum class HitZone { None, Interior, LeftEdge, RightEdge, TopEdge, BottomEdge, TopLeft, TopRight, BottomLeft, BottomRight };

    HitZone hitTest(const QPointF &pos) const;
    void fitCropToImage(bool initial = false);
    void clampCropRect();

    QImage source_image_;
    QImage display_image_;  // scaled to fit widget
    QPointF img_offset_;    // top-left of display_image_ in widget coords
    QRectF crop_rect_;      // in display coordinates (0..display_image_.width())
    QPointF last_mouse_pos_;
    HitZone drag_zone_{HitZone::None};
    QDialogButtonBox *button_box_;
    bool dragging_{false};

    static constexpr int MIN_CROP_SIZE = 10;
    static constexpr int HANDLE_MARGIN = 8;
};

class MapItemEditor : public QWidget {
    Q_OBJECT

   public:
    explicit MapItemEditor(QWidget *parent = nullptr);

    ~MapItemEditor() override;

    void load_map_data(const bl::general_kv_nbts &data);

    void paintEvent(QPaintEvent *event) override;

    void clearData() { this->map_nbt_editor_->clearData(); }

    NbtWidget *nbtEditor() { return this->map_nbt_editor_; }

   private slots:

    void on_export_map_btn_clicked();

    void on_change_map_btn_clicked();

   private:
    NbtWidget *map_nbt_editor_{nullptr};
    Ui::MapItemEditor *ui;
    QImage img;
};

#endif  // BEDROCKMAP_MAPITEMEDITOR_H
