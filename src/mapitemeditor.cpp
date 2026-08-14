#include "mapitemeditor.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QVBoxLayout>

#include "loguru/loguru.hpp"
#include "msg.h"
#include "nbt.h"
#include "ui_mapitemeditor.h"

// ─── ImageCropDialog implementation ───────────────────────────────────────────

ImageCropDialog::ImageCropDialog(const QImage &source, QWidget *parent) : QDialog(parent), source_image_(source) {
    setWindowTitle(tr("imageCropDialog.title.cropImage"));
    setMinimumSize(400, 350);
    resize(640, 560);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // central preview widget — we paint on the dialog itself
    button_box_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    button_box_->button(QDialogButtonBox::Ok)->setText(tr("imageCropDialog.button.confirm"));
    button_box_->button(QDialogButtonBox::Cancel)->setText(tr("imageCropDialog.button.cancel"));
    connect(button_box_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(button_box_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    layout->addStretch(1);
    layout->addWidget(button_box_);

    fitCropToImage(true);
    setMouseTracking(true);
}

QImage ImageCropDialog::croppedResult() const {
    if (display_image_.isNull() || crop_rect_.isEmpty()) return {};
    double sx = static_cast<double>(source_image_.width()) / display_image_.width();
    double sy = static_cast<double>(source_image_.height()) / display_image_.height();
    QRect src_rect(static_cast<int>(crop_rect_.x() * sx), static_cast<int>(crop_rect_.y() * sy), static_cast<int>(crop_rect_.width() * sx),
                   static_cast<int>(crop_rect_.height() * sy));
    src_rect = src_rect.intersected(source_image_.rect());
    if (src_rect.isEmpty()) return {};
    return source_image_.copy(src_rect)
        .scaled(128, 128, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGBA8888);
}

void ImageCropDialog::fitCropToImage(bool initial) {
    if (source_image_.isNull()) return;

    // content area: full widget minus button box
    int contentW = width();
    int contentH = height() - button_box_->sizeHint().height();

    // scale display image to fit content area, keep aspect ratio
    display_image_ = source_image_.scaled(contentW, contentH, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // center the image in the content area
    img_offset_ = QPointF((contentW - display_image_.width()) / 2.0, (contentH - display_image_.height()) / 2.0);

    if (initial) {
        // first time: start with max square in center
        int side = std::min(display_image_.width(), display_image_.height());
        double cx = (display_image_.width() - side) / 2.0;
        double cy = (display_image_.height() - side) / 2.0;
        crop_rect_ = QRectF(cx, cy, side, side);
    } else {
        // preserve existing crop: clamp to new image bounds
        clampCropRect();
    }
    update();
}

void ImageCropDialog::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // fill background
    p.fillRect(rect(), QColor(40, 40, 40));

    // draw image centered
    p.drawImage(img_offset_, display_image_);

    // dimmed overlay outside crop
    p.setBrush(QColor(0, 0, 0, 120));
    p.setPen(Qt::NoPen);
    auto r = crop_rect_.translated(img_offset_);
    QRectF imgArea(img_offset_, display_image_.size());
    QPainterPath overlay;
    overlay.addRect(imgArea);
    overlay.addRect(r);
    p.drawPath(overlay.simplified());

    // crop border
    p.setBrush(Qt::NoBrush);
    QPen pen(QColor(0, 200, 255), 1.5);
    pen.setCosmetic(true);
    p.setPen(pen);
    p.drawRect(r);
}

ImageCropDialog::HitZone ImageCropDialog::hitTest(const QPointF &pos) const {
    if (crop_rect_.isEmpty()) return HitZone::Interior;
    QRectF r = crop_rect_.translated(img_offset_);
    int m = HANDLE_MARGIN;

    if (!r.adjusted(-m, -m, m, m).contains(pos)) return HitZone::None;
    if (r.adjusted(m, m, -m, -m).contains(pos)) return HitZone::Interior;

    bool l = pos.x() < r.left() + m;
    bool r_ = pos.x() > r.right() - m;
    bool t = pos.y() < r.top() + m;
    bool b = pos.y() > r.bottom() - m;

    if (l && t) return HitZone::TopLeft;
    if (r_ && t) return HitZone::TopRight;
    if (l && b) return HitZone::BottomLeft;
    if (r_ && b) return HitZone::BottomRight;
    if (l) return HitZone::LeftEdge;
    if (r_) return HitZone::RightEdge;
    if (t) return HitZone::TopEdge;
    if (b) return HitZone::BottomEdge;
    return HitZone::None;
}

void ImageCropDialog::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    last_mouse_pos_ = event->position();
    drag_zone_ = hitTest(event->position());
    if (drag_zone_ != HitZone::None) dragging_ = true;
}

void ImageCropDialog::mouseMoveEvent(QMouseEvent *event) {
    QPointF delta = event->position() - last_mouse_pos_;
    last_mouse_pos_ = event->position();

    if (!dragging_) {
        auto hz = hitTest(event->position());
        switch (hz) {
            case HitZone::TopLeft:
            case HitZone::BottomRight:
                setCursor(Qt::SizeFDiagCursor);
                break;
            case HitZone::TopRight:
            case HitZone::BottomLeft:
                setCursor(Qt::SizeBDiagCursor);
                break;
            case HitZone::LeftEdge:
            case HitZone::RightEdge:
                setCursor(Qt::SizeHorCursor);
                break;
            case HitZone::TopEdge:
            case HitZone::BottomEdge:
                setCursor(Qt::SizeVerCursor);
                break;
            case HitZone::Interior:
                setCursor(Qt::SizeAllCursor);
                break;
            default:
                setCursor(Qt::ArrowCursor);
                break;
        }
        return;
    }

    auto clampX = [this](double val) { return std::clamp(val, 0.0, static_cast<double>(display_image_.width())); };
    auto clampY = [this](double val) { return std::clamp(val, 0.0, static_cast<double>(display_image_.height())); };

    auto &r = crop_rect_;

    switch (drag_zone_) {
        case HitZone::Interior:
            r.translate(clampX(r.x() + delta.x()) - r.x(), clampY(r.y() + delta.y()) - r.y());
            r.moveLeft(std::max(0.0, std::min(r.x(), static_cast<double>(display_image_.width()) - r.width())));
            r.moveTop(std::max(0.0, std::min(r.y(), static_cast<double>(display_image_.height()) - r.height())));
            break;
        case HitZone::LeftEdge:
            r.setLeft(clampX(r.left() + delta.x()));
            r.setTop(r.bottom() - r.width());
            break;
        case HitZone::RightEdge:
            r.setRight(clampX(r.right() + delta.x()));
            r.setBottom(r.top() + r.width());
            break;
        case HitZone::TopEdge:
            r.setTop(clampY(r.top() + delta.y()));
            r.setRight(r.left() + r.height());
            break;
        case HitZone::BottomEdge:
            r.setBottom(clampY(r.bottom() + delta.y()));
            r.setRight(r.left() + r.height());
            break;
        case HitZone::TopLeft:
            r.setTopLeft({clampX(r.left() + delta.x()), clampY(r.top() + delta.y())});
            r.setBottom(r.top() + r.width());
            break;
        case HitZone::TopRight:
            r.setTopRight({clampX(r.right() + delta.x()), clampY(r.top() + delta.y())});
            r.setRight(r.left() + r.height());
            break;
        case HitZone::BottomLeft:
            r.setBottomLeft({clampX(r.left() + delta.x()), clampY(r.bottom() + delta.y())});
            r.setRight(r.left() + r.height());
            break;
        case HitZone::BottomRight:
            r.setBottomRight({clampX(r.right() + delta.x()), clampY(r.bottom() + delta.y())});
            r.setTop(r.bottom() - r.width());
            break;
        default:
            break;
    }

    // enforce square + min size + bounds
    clampCropRect();
    update();
}

void ImageCropDialog::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    dragging_ = false;
    drag_zone_ = HitZone::None;
}

void ImageCropDialog::wheelEvent(QWheelEvent *event) {
    double scale = (event->angleDelta().y() > 0) ? 1.05 : 0.95;
    double cx = crop_rect_.center().x();
    double cy = crop_rect_.center().y();
    double newSide = crop_rect_.width() * scale;
    double newLeft = cx - newSide / 2.0;
    double newTop = cy - newSide / 2.0;
    crop_rect_ = QRectF(newLeft, newTop, newSide, newSide);
    clampCropRect();
    update();
}

void ImageCropDialog::resizeEvent(QResizeEvent *) { fitCropToImage(false); }

// Keep crop rect within display image bounds and enforce square + min size.
// Call after display_image_ size changes (resize, fit) or after crop editing.
void ImageCropDialog::clampCropRect() {
    double maxW = display_image_.width();
    double maxH = display_image_.height();
    double maxSide = std::min(maxW, maxH);
    double side = std::max(static_cast<double>(MIN_CROP_SIZE), std::min(crop_rect_.width(), maxSide));
    crop_rect_.setSize({side, side});  // enforce square
    crop_rect_.moveLeft(std::max(0.0, std::min(crop_rect_.left(), maxW - side)));
    crop_rect_.moveTop(std::max(0.0, std::min(crop_rect_.top(), maxH - side)));
}

MapItemEditor::MapItemEditor(QWidget *parent) : QWidget(parent), ui(new Ui::MapItemEditor) {
    ui->setupUi(this);
    this->setWindowTitle(tr("mapItemEditor.title.mapItemEditor"));
    this->map_nbt_editor_ = new NbtWidget(this);
    this->map_nbt_editor_->setMode(NbtMode::Memory);
    ui->splitter->insertWidget(0, this->map_nbt_editor_);
    ui->splitter->setStretchFactor(0, 2);
    ui->splitter->setStretchFactor(1, 1);
    this->img = QImage(128, 128, QImage::Format_RGBA8888);
    img.fill(QColor(0, 0, 0, 0));
    this->map_nbt_editor_->setExtraLoadEvent([this](bl::nbt::compound_tag *root) {
        if (!root) return;
        auto *color_tag = dynamic_cast<bl::nbt::byte_array_tag *>(root->get("colors"));
        if (!color_tag || color_tag->value.size() != 65536) return;
        this->img = QImage(128, 128, QImage::Format_RGBA8888);
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                const int n = (y * 128 + x) * 4;
                this->img.setPixelColor(
                    x, y, QColor((uint8_t)color_tag->value[n], (uint8_t)color_tag->value[n + 1], (uint8_t)color_tag->value[n + 2]));
            }
        }
        this->update();
    });
}

MapItemEditor::~MapItemEditor() {
    this->clearData();
    delete ui;
}

void MapItemEditor::load_map_data(const bl::general_kv_nbts &data) {
    std::vector<NBTListItem *> items;
    for (auto &kv : data.data()) {
        auto *it = NBTListItem::from(dynamic_cast<bl::nbt::compound_tag *>(kv.second->copy()), kv.first.c_str(), kv.first.c_str());
        items.push_back(it);
    }
    this->map_nbt_editor_->loadNewData(items);
}

void MapItemEditor::paintEvent(QPaintEvent *event) {
    int w = ui->image_widget->width();
    int h = ui->image_widget->height();
    int MAP_WIDTH = std::min(w, h) - 20;
    if (MAP_WIDTH < 16) MAP_WIDTH = 16;

    QPixmap pm = QPixmap::fromImage(img.scaled(MAP_WIDTH, MAP_WIDTH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    ui->image_widget->setPixmap(pm);
    ui->image_widget->setAlignment(Qt::AlignCenter);
}

void MapItemEditor::on_export_map_btn_clicked() {
    bool ok;
    int i = QInputDialog::getInt(this, msg::SAEVE_AS(), msg::SET_SCALE_LEVEL(), 1, 1, 16, 1, &ok);

    if (!ok) return;

    auto new_img = img.scaled(img.width() * i, img.height() * i);
    auto fileName = QFileDialog::getSaveFileName(this, tr("mapItemEditor.fileDialog.save"), "map.png", "Images (*.png *.jpg)");
    if (fileName.isEmpty()) return;
    new_img.save(fileName);
}

void MapItemEditor::on_change_map_btn_clicked() {
    QString fileName =
        QFileDialog::getOpenFileName(this, tr("mapItemEditor.fileDialog.open"), "", "Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)");

    if (fileName.isEmpty()) return;

    QPixmap pm;
    if (!pm.load(fileName)) {
        LOG_F(ERROR, "Cannot load image: %s", fileName.toStdString().c_str());
        return;
    }
    QImage source_img = pm.toImage();
    if (source_img.isNull()) return;

    ImageCropDialog dlg(source_img, this);
    if (dlg.exec() != QDialog::Accepted) return;

    QImage cropped = dlg.croppedResult();
    if (cropped.isNull()) return;
    this->img = cropped;

    auto *it = this->map_nbt_editor_->openedItem();
    if (!it || !it->root_) return;
    auto *color_tag = dynamic_cast<bl::nbt::byte_array_tag *>(it->root_->get("colors"));
    if (!color_tag) {
        LOG_F(ERROR, "Can not find colors node, cancelled");
        return;
    }
    color_tag->value.resize(128 * 128 * 4);
    for (int y = 0; y < 128; y++) {
        for (int x = 0; x < 128; x++) {
            const int n = (y * 128 + x) * 4;
            auto c = this->img.pixelColor(x, y);
            color_tag->value[n] = static_cast<uint8_t>(c.red());
            color_tag->value[n + 1] = static_cast<uint8_t>(c.green());
            color_tag->value[n + 2] = static_cast<uint8_t>(c.blue());
            color_tag->value[n + 3] = static_cast<uint8_t>(c.alpha());
        }
    }
    this->map_nbt_editor_->putModifyToCache(it->raw_key.toStdString(), it->root_->to_raw());
    this->update();
}
