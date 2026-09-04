#include "mapwidget.h"

#include <qcolor.h>
#include <qlogging.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpoint.h>
#include <qtypes.h>
#include <qvectornd.h>

#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QColor>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFormLayout>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QRgb>
#include <Qmainwindow>
#include <QtOpenGLWidgets/QtOpenGLWidgets>
#include <Qwidget>
#include <cmath>
#include <utility>
#include <vector>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "chunkoperator.h"
#include "config.h"
#include "gotopositiondialog.h"
#include "loguru/loguru.hpp"
#include "msg.h"
#include "processmonitor.h"
#include "voxelwidget.h"


QImage MapWidget::captureSelectionToImage(double scale) {
    if (selection_.isEmpty()) {
        return {};
    }

    auto brect = selection_.region().boundingRect();

    // Map the bounding box from chunk-space to view (pixel) space.
    // Each chunk at integer (x, z) spans world-space [x, x+1) × [z, z+1).
    QPointF tl = world_to_view_xf_.map(QPointF(brect.left(), brect.top()));
    QPointF br = world_to_view_xf_.map(QPointF(brect.right() + 1, brect.bottom() + 1));

    QRectF viewRect = QRectF(tl, br).normalized();
    QRect captureRect = viewRect.toAlignedRect().intersected(rect());

    if (captureRect.isEmpty()) {
        return {};
    }

    // Temporarily hide selection overlay and floating toolbars
    capturing_ = true;
    emit toolbarsVisibleRequested(false);
    update();
    QApplication::processEvents();

    QImage img = grab(captureRect).toImage();

    // Restore
    capturing_ = false;
    emit toolbarsVisibleRequested(true);
    update();

    if (!qFuzzyCompare(scale, 1.0)) {
        img = img.scaled(img.size() * scale, Qt::KeepAspectRatio, Qt::FastTransformation);
    }

    return img;
}

void MapWidget::saveSelectionImage() {
    bool ok;
    int scale = QInputDialog::getInt(this, msg::SAEVE_AS(), msg::SET_SCALE_LEVEL(), 1, 1, 16, 1, &ok);
    if (!ok) return;
    QImage img = captureSelectionToImage(static_cast<double>(scale));
    if (img.isNull()) return;
    auto fileName = QFileDialog::getSaveFileName(this, tr("mapWidget.fileDialog.save"), {}, "Images (*.png *.jpg)");
    if (fileName.isEmpty()) return;
    img.save(fileName);
}

void MapWidget::saveFullscreenImage() {
    bool ok;
    int i = QInputDialog::getInt(this, msg::SAEVE_AS(), msg::SET_SCALE_LEVEL(), 1, 1, 16, 1, &ok);
    if (!ok) return;

    // Temporarily hide toolbars
    capturing_ = true;
    emit toolbarsVisibleRequested(false);
    update();
    QApplication::processEvents();

    QImage img = this->grab().toImage();

    // Restore
    capturing_ = false;
    emit toolbarsVisibleRequested(true);
    update();

    if (i != 1) {
        img = img.scaled(img.size() * i, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    auto fileName = QFileDialog::getSaveFileName(this, tr("mapWidget.fileDialog.save"), {}, "Images (*.png *.jpg)");
    if (fileName.isEmpty()) return;
    img.save(fileName);
}

void MapWidget::gotoPositionAction() {
    if (this->goto_dialog_->exec() == QDialog::Accepted) {
        if (this->goto_dialog_->positionValid()) {
            gotoBlockPos(goto_dialog_->x(), goto_dialog_->z());
        } else {
            WARN(msg::INVALID_COORDINATE());
        }
    }
}

void MapWidget::clearSelection() {
    selection_.clear();
    update();
    emit selectionChanged();
}

void MapWidget::copySelectionToClipboard(int dim) {
    if (selection_.isEmpty()) return;
    ExportedRegion region;
    auto sel = selection_.region();
    for (const auto &r : sel) {
        for (int x = r.x(); x < r.x() + r.width(); x++) {
            for (int z = r.y(); z < r.y() + r.height(); z++) {
                auto raw = level_loader_->getRawChunk(bl::chunk_pos(x, z, dim));
                if (raw.has_value()) region.addChunk(raw.value());
            }
        }
    }
    if (region.isEmpty()) return;
    auto data = region.serialize();
    auto *md = new QMimeData();
    md->setData("application/x-bedrockmap-region", QByteArray(data.data(), static_cast<int>(data.size())));
    auto *clip = QApplication::clipboard();
    clip->clear(QClipboard::Clipboard);
    clip->setMimeData(md, QClipboard::Clipboard);
    LOG_F(INFO, "MapWidget: copied %d chunks to clipboard", static_cast<int>(region.chunkCount()));
}

void MapWidget::pasteFromClipboard(int dim) {
    if (modificationBlocked()) return;
    auto *clip = QApplication::clipboard();
    const auto *md = clip->mimeData();
    if (!md || !md->hasFormat("application/x-bedrockmap-region")) {
        WARN(msg::PASTE_NO_DATA());
        return;
    }
    QByteArray rawData = md->data("application/x-bedrockmap-region");
    if (rawData.isEmpty()) {
        INFO(msg::PASTE_DATA_EMPTY());
        return;
    }
    bl::chunk_pos anchor(0, 0, dim);
    if (!import_overlay_->startPaste(rawData, dim, anchor)) {
        INFO(msg::PASTE_DATA_INVALID());
        return;
    }
    update();
}

void MapWidget::exportSelectionToFile(int dim) {
    if (selection_.isEmpty()) return;
    auto fp = QFileDialog::getSaveFileName(nullptr, QObject::tr("mapWidget.rightMenu.exportRegion"), {}, msg::ALL_FILES());
    if (fp.isEmpty()) return;
    ChunkOperator::exportRegion(selection_.region(), fp, *level_loader_, dim);
    INFO(msg::EXPORT_COMPLETE());
}

void MapWidget::exportSelectionToMcstructure(int dim, bool compress, bool exportEntities, const std::optional<bl::block_box> &blockBounds,
                                             int32_t version) {
    if (selection_.isEmpty() || !level_loader_) return;

    const auto filePath =
        QFileDialog::getSaveFileName(this, tr("mapWidget.rightMenu.exportMcstructure"), {}, tr("MCStructure files (*.mcstructure)"));
    if (filePath.isEmpty()) return;

    QString outputPath = filePath;
    if (QFileInfo(outputPath).suffix().isEmpty()) outputPath += QStringLiteral(".mcstructure");

    if (!BlockRegionOperator::exportMcstructure(selection_.region(), outputPath, *level_loader_, dim, compress, blockBounds, version,
                                                exportEntities)) {
        QMessageBox::warning(this, tr("mapWidget.rightMenu.exportMcstructure"), tr("mapWidget.rightMenu.exportMcstructureFailed"));
        return;
    }
    INFO(msg::EXPORT_COMPLETE());
}

void MapWidget::importFromFile(int dim) {
    if (modificationBlocked()) return;
    auto fp = QFileDialog::getOpenFileName(nullptr, QObject::tr("mapWidget.rightMenu.importRegion"), {}, msg::BCHKS_FILES());
    if (fp.isEmpty()) return;
    bl::chunk_pos anchor(0, 0, dim);
    import_overlay_->startImport(fp, dim, anchor);
    update();
}

void MapWidget::deleteSelection(int dim) {
    if (selection_.isEmpty() || modificationBlocked()) return;
    ChunkOperator::deleteRegion(selection_.region(), *level_loader_, dim);
    update();
}

void MapWidget::createVoidSelection(int dim) {
    if (selection_.isEmpty() || modificationBlocked()) return;
    ChunkOperator::createVoid(selection_.region(), *level_loader_, dim);
    update();
}

void MapWidget::setSelectionBiome(int biome, int dim) {
    if (selection_.isEmpty() || modificationBlocked()) return;
    ChunkOperator::setRegionBiome(selection_.region(), *level_loader_, static_cast<bl::biome>(biome), dim);
    update();
}

bool MapWidget::modificationBlocked() {
    if (!level_loader_ || !level_loader_->chunkCoordsLoading()) return false;
    QMessageBox::warning(this, msg::READ_ONLY(), msg::EDITING_DISABLED_DURING_COORDS_LOADING());
    return true;
}

void MapWidget::show3DView(int dim) {
    if (selection_.isEmpty() || selection_.rectCount() != 1) return;
    auto rect = selection_.region().boundingRect();
    bl::chunk_pos minPos(rect.x(), rect.y(), dim);
    bl::chunk_pos maxPos(rect.x() + rect.width() - 1, rect.y() + rect.height() - 1, dim);
    voxel_preview_window_->loadChunksAsync(minPos, maxPos, *level_loader_);
}

void MapWidget::syncToolbars() {
    emit syncToolbarsRequested();
}

// show right-click context menu
void MapWidget::showContextMenu(const QPoint &p) { ContextMenuBuilder::show(this, this, mapToGlobal(p)); }
