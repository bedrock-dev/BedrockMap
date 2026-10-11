#include "maphost.h"

#include <qlogging.h>
#include <qnamespace.h>
#include <qtypes.h>

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QMimeData>
#include <algorithm>
#include <cmath>
#include <utility>

#include "blockregionoperator.h"
#include "chunkoperator.h"
#include "clipboarddata.h"
#include "config.h"
#include "loguru/loguru.hpp"
#include "mcstructure.h"
#include "msg.h"
#include "pleasewaitdialog.h"
#include "voxelwidget.h"

MapHost::MapHost(QWidget* page, AsyncLevelLoader* loader)
    : QObject(page), level_loader_(loader), pane_(page), view_(this), overlays_(&view_, loader), chunk_edit_task_(this) {
    // trigger redraw when an async region finishes loading, replacing the old 100ms timer polling
    if (level_loader_) {
        connect(level_loader_, &AsyncLevelLoader::regionReady, this, [this] {
            // A bulk edit invalidates many region tiles at once. Defer map
            // repainting until the batch completes to avoid a full redraw for
            // every region that comes back.
            if (chunk_edit_task_.isRunning()) return;
            view_.notifyChanged();
        });
    }

    // dialog
    goto_dialog_ = new GoToPositionDialog(page);
    voxel_preview_window_ = new VoxelPreviewWidget();
    connect(&chunk_edit_task_, &GuiTaskRunner::finished, this, [this]() {
        PleaseWaitDialog::instance().hideBusy();
        if (reload_voxel_preview_pending_) {
            reload_voxel_preview_pending_ = false;
            voxel_preview_window_->reloadChunks();
        }
        view_.notifyChanged();
    });
    connect(&chunk_edit_task_, &GuiTaskRunner::failed, this, [this](const QString& error) {
        PleaseWaitDialog::instance().hideBusy();
        reload_voxel_preview_pending_ = false;
        QMessageBox::warning(paneWidget(), tr("mapHost.editFailed"), error);
    });
    connect(voxel_preview_window_, &VoxelPreviewWidget::exportMcstructureRequested, this,
            [this](bl::block_box selection, bool compress, bool exportEntities, bool useNewFormat) {
                const bl::block_box blockBounds = selection.translated(voxel_preview_window_->voxelOrigin());
                exportSelectionToMcstructure(view_.dim(), blockBounds, compress, exportEntities, useNewFormat ? 2 : 1);
            });
    connect(voxel_preview_window_, &VoxelPreviewWidget::copyMcstructureRequested, this,
            [this](bl::block_box selection, bool exportEntities, bool useNewFormat) {
                copyVoxelSelectionToMcstructure(selection, exportEntities, useNewFormat);
            });
    connect(voxel_preview_window_, &VoxelPreviewWidget::deleteSelectionRequested, this,
            [this](bl::block_box selection, bool deleteEntities) { deleteVoxelSelection(selection, deleteEntities); });
    connect(voxel_preview_window_, &VoxelPreviewWidget::cutSelectionRequested, this,
            [this](bl::block_box selection, bool deleteEntities, bool useNewFormat) {
                if (copyVoxelSelectionToMcstructure(selection, deleteEntities, useNewFormat))
                    deleteVoxelSelection(selection, deleteEntities);
            });
    connect(voxel_preview_window_, &VoxelPreviewWidget::importConfirmed, this,
            [this](bl::block_box placement, std::shared_ptr<const bl::mcstructure> imported) {
                if (!imported) return;
                const bl::block_pos origin = placement.min_pos;
                const int dim = view_.dim();
                if (!startChunkTask([this, origin, dim, imported = std::move(imported)](GuiTaskRunner*) {
                        // Replace-air stays on until a GUI option exists for it.
                        BlockRegionOperator::importMcstructure(*imported, origin, *level_loader_, dim, true);
                    })) {
                    return;
                }
                reload_voxel_preview_pending_ = true;
            });
    // Center and resize to ~80% of the page's window once
    if (auto* win = page ? page->window() : nullptr) {
        QSize sz = win->size() * 0.8;
        voxel_preview_window_->resize(sz);
        voxel_preview_window_->move(win->geometry().center() - QPoint(sz.width() / 2, sz.height() / 2));
    }

    // The shared view starts zoomed in enough to see individual chunks.
    view_.setScale(MapView::DEFAULT_SCALE, QPointF());

    // import overlay
    import_overlay_ = std::make_unique<ImportOverlay>(loader, this);
    connect(import_overlay_.get(), &ImportOverlay::confirmed, this, [this] { view_.notifyChanged(); });
    connect(import_overlay_.get(), &ImportOverlay::toolbarsVisibleRequested, this, &MapHost::toolbarsVisibleRequested);

    // The selection is shared, so a status bar that follows it follows both
    // renderers; this forwards the view's own notification.
    connect(&view_, &MapView::selectionChanged, this, &MapHost::selectionChanged);
}

MapHost::~MapHost() {
    chunk_edit_task_.waitForFinished();
    delete voxel_preview_window_;
}

bool MapHost::toggleOther(RenderOption::OtherType other) {
    const bool now = !view_.options().getOther(other);
    view_.setOther(other, now);
    return now;
}

void MapHost::setDrawDebug(bool enable) {
    draw_debug_window_ = enable;
    overlays_.setDrawDebug(enable);
    view_.notifyChanged();
}

void MapHost::setCoordsMiniMap(bool enable) {
    if (draw_coords_minimap_ == enable) return;
    draw_coords_minimap_ = enable;
    overlays_.setCoordsMiniMap(enable);
    view_.notifyChanged();
}

bool MapHost::toggleCoordsMiniMap() {
    setCoordsMiniMap(!draw_coords_minimap_);
    return draw_coords_minimap_;
}

void MapHost::setTransparentVoid(bool v) {
    if (transparent_void_ == v) return;
    transparent_void_ = v;
    if (level_loader_) {
        level_loader_->setTransparentVoid(v);
        level_loader_->clearAllCache();
    }
    view_.notifyChanged();
}

bool MapHost::toggleTransparentVoid() {
    setTransparentVoid(!transparent_void_);
    return transparent_void_;
}

void MapHost::setVillages(const QMap<QString, VillageDrawInfo>& villages) {
    overlays_.setVillages(villages);
    view_.notifyChanged();
}

void MapHost::setPortals(const QVector<PortalDrawInfo>& portals) {
    overlays_.setPortals(portals);
    view_.notifyChanged();
}

void MapHost::gotoBlockPos(int x, int z) {
    const QPointF view_pos = view_.blockPosToViewPos(bl::block_pos(x, 0, z));
    const QPointF delta = (QPointF(view_.camera().center()) - view_pos) / std::abs(view_.scaleLevel());
    view_.translate(delta);
}

void MapHost::gotoPositionAction() {
    if (goto_dialog_->exec() == QDialog::Accepted) {
        if (goto_dialog_->positionValid()) {
            gotoBlockPos(goto_dialog_->x(), goto_dialog_->z());
        } else {
            WARN(msg::INVALID_COORDINATE());
        }
    }
}

void MapHost::clearSelection() { view_.clearSelection(); }

void MapHost::copySelectionToClipboard(int dim) {
    const auto& selection = view_.selection();
    if (selection.isEmpty()) return;
    PleaseWaitScope wait;

    ExportedRegion region;
    auto sel = selection.region();
    for (const auto& r : sel) {
        for (int x = r.x(); x < r.x() + r.width(); x++) {
            for (int z = r.y(); z < r.y() + r.height(); z++) {
                auto raw = level_loader_->getRawChunk(bl::chunk_pos(x, z, dim));
                if (raw.has_value()) region.addChunk(raw.value());
            }
        }
    }
    if (region.isEmpty()) return;
    auto data = region.serialize();
    auto* md = new QMimeData();
    clipboard_data::write(*md, clipboard_data::CHUNK_REGION_MIME_TYPE, QByteArray(data.data(), static_cast<int>(data.size())));
    auto* clip = QApplication::clipboard();
    clip->clear(QClipboard::Clipboard);
    clip->setMimeData(md, QClipboard::Clipboard);
    LOG_F(INFO, "MapHost: copied %d chunks to clipboard", static_cast<int>(region.chunkCount()));
}

void MapHost::pasteFromClipboard(int dim) {
    PleaseWaitScope wait;
    auto* clip = QApplication::clipboard();
    const QByteArray raw_data =
        clipboard_data::read(clip ? clip->mimeData() : nullptr, clipboard_data::CHUNK_REGION_MIME_TYPE, {QStringLiteral("bchks")});
    if (raw_data.isEmpty()) {
        WARN(msg::PASTE_NO_DATA());
        return;
    }
    bl::chunk_pos anchor(0, 0, dim);
    if (!import_overlay_->startPaste(raw_data, dim, anchor)) {
        INFO(msg::PASTE_DATA_INVALID());
        return;
    }
    view_.notifyChanged();
}

void MapHost::exportSelectionToFile(int dim) {
    if (view_.selection().isEmpty()) return;
    const QRect bounds = view_.selection().region().boundingRect();
    const QString defaultName = QStringLiteral("region_x%1_z%2_x%3_z%4_dim%5.bchks")
                                    .arg(bounds.left())
                                    .arg(bounds.top())
                                    .arg(bounds.right())
                                    .arg(bounds.bottom())
                                    .arg(dim);
    auto fp = QFileDialog::getSaveFileName(paneWidget(), QObject::tr("mapHost.rightMenu.exportRegion"), defaultName, msg::BCHKS_FILES());
    if (fp.isEmpty()) return;
    if (QFileInfo(fp).suffix().compare(QStringLiteral("bchks"), Qt::CaseInsensitive) != 0) fp += QStringLiteral(".bchks");
    ChunkOperator::exportRegion(view_.selection().region(), fp, *level_loader_, dim);
    INFO(msg::EXPORT_COMPLETE());
}

void MapHost::exportSelectionToMcstructure(int dim, const bl::block_box& blockBounds, bool compress, bool exportEntities, int32_t version) {
    if (view_.selection().isEmpty() || !level_loader_ || !blockBounds.is_valid()) return;

    const auto filePath =
        QFileDialog::getSaveFileName(paneWidget(), tr("mapHost.rightMenu.exportMcstructure"), {}, tr("MCStructure files (*.mcstructure)"));
    if (filePath.isEmpty()) return;

    QString outputPath = filePath;
    if (QFileInfo(outputPath).suffix().isEmpty()) outputPath += QStringLiteral(".mcstructure");

    PleaseWaitScope wait;
    if (!BlockRegionOperator::exportMcstructure(outputPath, *level_loader_, dim, blockBounds, compress, version, exportEntities)) {
        QMessageBox::warning(paneWidget(), tr("mapHost.rightMenu.exportMcstructure"), tr("mapHost.rightMenu.exportMcstructureFailed"));
        return;
    }
    INFO(msg::EXPORT_COMPLETE());
}

void MapHost::importFromFile(int dim) {
    auto fp = QFileDialog::getOpenFileName(paneWidget(), QObject::tr("mapHost.rightMenu.importRegion"), {}, msg::BCHKS_FILES());
    if (fp.isEmpty()) return;
    bl::chunk_pos anchor(0, 0, dim);
    import_overlay_->startImport(fp, dim, anchor);
    view_.notifyChanged();
}

void MapHost::deleteSelection(int dim) {
    if (view_.selection().isEmpty()) return;
    const auto region = view_.selection().region();
    startChunkTask([this, region, dim](GuiTaskRunner* /*task*/) { ChunkOperator::deleteRegion(region, *level_loader_, dim); });
}

void MapHost::createVoidSelection(int dim) {
    if (view_.selection().isEmpty()) return;
    const auto region = view_.selection().region();
    startChunkTask([this, region, dim](GuiTaskRunner* /*task*/) { ChunkOperator::createVoid(region, *level_loader_, dim); });
}

void MapHost::setSelectionBiome(int biome, int dim) {
    if (view_.selection().isEmpty()) return;
    const auto region = view_.selection().region();
    startChunkTask([this, region, biome, dim](GuiTaskRunner* /*task*/) {
        ChunkOperator::setRegionBiome(region, *level_loader_, static_cast<bl::biome>(biome), dim);
    });
}

bool MapHost::startChunkTask(GuiTaskRunner::Worker worker) {
    if (!level_loader_ || chunk_edit_task_.isRunning()) return false;
    PleaseWaitDialog::instance().showBusy();
    return chunk_edit_task_.start(std::move(worker));
}

bool MapHost::copyVoxelSelectionToMcstructure(const bl::block_box& selection, bool exportEntities, bool useNewFormat) {
    if (view_.selection().isEmpty() || !level_loader_ || !selection.is_valid() || !QApplication::clipboard()) return false;

    const bl::block_box blockBounds = selection.translated(voxel_preview_window_->voxelOrigin());
    PleaseWaitScope wait;
    const auto raw =
        BlockRegionOperator::exportMcstructureData(*level_loader_, view_.dim(), blockBounds, false, useNewFormat ? 2 : 1, exportEntities);
    if (raw.empty()) {
        QMessageBox::warning(paneWidget(), tr("mapHost.rightMenu.exportMcstructure"), tr("mapHost.rightMenu.exportMcstructureFailed"));
        return false;
    }

    auto* mimeData = new QMimeData();
    clipboard_data::write(*mimeData, clipboard_data::MCSTRUCTURE_MIME_TYPE, QByteArray(raw.data(), static_cast<qsizetype>(raw.size())));
    QApplication::clipboard()->setMimeData(mimeData);
    INFO(msg::EXPORT_COMPLETE());
    return true;
}

void MapHost::deleteVoxelSelection(const bl::block_box& selection, bool deleteEntities) {
    if (view_.selection().isEmpty() || !level_loader_ || !selection.is_valid()) return;

    const bl::block_box blockBounds = selection.translated(voxel_preview_window_->voxelOrigin());
    const int dim = view_.dim();
    if (!startChunkTask([this, blockBounds, dim, deleteEntities](GuiTaskRunner* /*task*/) {
            BlockRegionOperator::deleteBlocks(*level_loader_, dim, blockBounds, deleteEntities);
        })) {
        return;
    }
    reload_voxel_preview_pending_ = true;
}

void MapHost::applyImportedRegion(ExportedRegion region) {
    if (!level_loader_ || chunk_edit_task_.isRunning() || region.isEmpty()) return;
    startChunkTask(
        [this, region = std::move(region)](GuiTaskRunner* /*task*/) mutable { ChunkOperator::importRegion(region, *level_loader_); });
}

void MapHost::saveSelectionImage(QWidget* source) {
    if (!source) return;
    if (view_.selection().isEmpty()) return;

    bool ok;
    int scale = QInputDialog::getInt(paneWidget(), msg::SAEVE_AS(), msg::SET_SCALE_LEVEL(), 1, 1, 16, 1, &ok);
    if (!ok) return;

    // The screenshot is of whichever map was clicked, so the rect is computed
    // with that widget's transform - it may not be the size the view holds.
    const QRect bounding = view_.selection().region().boundingRect();
    const QTransform to_view = view_.transformForViewport(source->size());
    const QPointF tl = to_view.map(QPointF(bounding.left(), bounding.top()));
    const QPointF br = to_view.map(QPointF(bounding.right() + 1, bounding.bottom() + 1));
    const QRect capture_rect = QRectF(tl, br).normalized().toAlignedRect().intersected(source->rect());
    if (capture_rect.isEmpty()) return;

    // Hide the chrome and the selection itself, so neither ends up in the file.
    emit toolbarsVisibleRequested(false);
    view_.setSelectionVisible(false);
    source->update();
    QApplication::processEvents();
    QImage img = source->grab(capture_rect).toImage();
    view_.setSelectionVisible(true);
    emit toolbarsVisibleRequested(true);
    source->update();

    if (scale != 1) img = img.scaled(img.size() * scale, Qt::KeepAspectRatio, Qt::FastTransformation);
    if (img.isNull()) return;
    const QString file_name = QFileDialog::getSaveFileName(paneWidget(), tr("mapHost.fileDialog.save"), {}, "Images (*.png *.jpg)");
    if (file_name.isEmpty()) return;
    img.save(file_name);
}

void MapHost::saveFullscreenImage(QWidget* source) {
    if (!source) return;
    bool ok;
    int scale = QInputDialog::getInt(paneWidget(), msg::SAEVE_AS(), msg::SET_SCALE_LEVEL(), 1, 1, 16, 1, &ok);
    if (!ok) return;

    emit toolbarsVisibleRequested(false);
    view_.setSelectionVisible(false);
    source->update();
    QApplication::processEvents();
    QImage img = source->grab().toImage();
    view_.setSelectionVisible(true);
    emit toolbarsVisibleRequested(true);
    source->update();

    if (scale != 1) img = img.scaled(img.size() * scale, Qt::KeepAspectRatio, Qt::FastTransformation);
    const QString file_name = QFileDialog::getSaveFileName(paneWidget(), tr("mapHost.fileDialog.save"), {}, "Images (*.png *.jpg)");
    if (file_name.isEmpty()) return;
    img.save(file_name);
}

void MapHost::show3DView(int dim) {
    if (view_.selection().isEmpty() || view_.selection().rectCount() != 1) return;
    auto rect = view_.selection().region().boundingRect();
    bl::chunk_pos min_pos(rect.x(), rect.y(), dim);
    bl::chunk_pos max_pos(rect.x() + rect.width() - 1, rect.y() + rect.height() - 1, dim);
    voxel_preview_window_->loadChunksAsync(min_pos, max_pos, *level_loader_);
}

bool MapHost::beginPaste(const QByteArray& data, int dim, const bl::chunk_pos& at) {
    PleaseWaitScope wait;
    if (!import_overlay_->startPaste(data, dim, at)) return false;
    view_.notifyChanged();
    return true;
}

bool MapHost::beginImport(const QString& path, int dim, const bl::chunk_pos& at) {
    import_overlay_->startImport(path, dim, at);
    view_.notifyChanged();
    return true;
}

void MapHost::showVoxelPreview(const bl::chunk_pos& min, const bl::chunk_pos& max) {
    if (!level_loader_) return;
    voxel_preview_window_->loadChunksAsync(min, max, *level_loader_);
}

void MapHost::showChunkEditor(const bl::chunk_pos& pos) { emit requestOpenChunkEditor(pos); }
