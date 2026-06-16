#include "contextmenubuilder.h"

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QMessageBox>
#include <QMimeData>

#include "biomepickerdialog.h"
#include "chunkio.h"
#include "chunkoperator.h"
#include "color.h"
#include "importoverlay.h"
#include "loguru/loguru.hpp"
#include "mapwidget.h"
#include "msg.h"

void ContextMenuBuilder::show(QWidget *parent, MapWidget *w, const QPoint &globalPos) {
    auto *cb = QApplication::clipboard();
    QMenu menu(parent);

    auto localPos = w->mapFromGlobal(globalPos);
    auto clickChunk = w->viewPosToChunkPos(localPos);
    auto cursorPos = w->getCursorBlockPos();
    auto blockInfo = w->level_loader_->getBlockTips(cursorPos, w->option_.dim);
    bool insideSelection = w->selection_.contains(QPoint(clickChunk.x, clickChunk.z));
    uint8_t dim = static_cast<uint8_t>(w->option_.dim);

    // === Group 1: Goto ===
    menu.addAction(QObject::tr("mapWidget.rightMenu.gotoPosition"), [w] { w->gotoPositionAction(); });
    menu.addSeparator();

    // === Group 2: Selection operations ===
    if (insideSelection) {
        menu.addAction(QObject::tr("mapWidget.rightMenu.unselect"), [w] {
            w->selection_.clear();
            w->update();
        });

        auto *selMenu = menu.addMenu(QObject::tr("mapWidget.rightMenu.selectionOps"));
        selMenu->addAction(QObject::tr("mapWidget.rightMenu.delete"),
                           [w] { ChunkOperator::deleteRegion(w->selection_.region(), *w->level_loader_); });
        selMenu->addAction(QObject::tr("mapWidget.rightMenu.createVoid"),
                           [w] { ChunkOperator::createVoid(w->selection_.region(), *w->level_loader_); });
        selMenu->addAction(QObject::tr("mapWidget.rightMenu.setBiome"), [w] {
            BiomePickerDialog dlg(w);
            if (dlg.exec() == QDialog::Accepted) {
                ChunkOperator::setRegionBiome(w->selection_.region(), *w->level_loader_, dlg.selectedBiome());
            }
        });
        selMenu->addAction(QObject::tr("mapWidget.rightMenu.copy"), [w, dim] {
            auto *clip = QApplication::clipboard();
            ExportedRegion region;
            auto sel = w->selection_.region();
            for (const auto &r : sel) {
                for (int x = r.x(); x < r.x() + r.width(); x++) {
                    for (int z = r.y(); z < r.y() + r.height(); z++) {
                        bl::raw_chunk raw(bl::chunk_pos(x, z, dim));
                        if (raw.read(w->level_loader_->level())) region.addChunk(raw);
                    }
                }
            }
            if (region.isEmpty()) return;
            auto data = region.serialize();
            auto *md = new QMimeData();
            md->setData("application/x-bedrockmap-region", QByteArray(data.data(), static_cast<int>(data.size())));
            clip->clear(QClipboard::Clipboard);
            clip->setMimeData(md, QClipboard::Clipboard);
            LOG_F(INFO, "MapWidget: copied %d chunks to clipboard", static_cast<int>(region.chunkCount()));
        });
        selMenu->addAction(QObject::tr("mapWidget.rightMenu.export"), [w] {
            auto fp = QFileDialog::getSaveFileName(nullptr, QObject::tr("mapWidget.rightMenu.exportRegion"), {}, msg::ALL_FILES());
            if (fp.isEmpty()) return;
            ChunkOperator::exportRegion(w->selection_.region(), fp, *w->level_loader_);
            INFO(msg::EXPORT_COMPLETE());
        });
        menu.addSeparator();
    }

    // Paste — always check clipboard, show only if data available
    const auto *pasteMd = cb->mimeData();
    if (pasteMd && pasteMd->hasFormat("application/x-bedrockmap-region") && !pasteMd->data("application/x-bedrockmap-region").isEmpty()) {
        menu.addAction(QObject::tr("mapWidget.rightMenu.paste"), [w, clickChunk, dim] {
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
            if (!w->import_overlay_->startPaste(rawData, dim, clickChunk)) {
                INFO(msg::PASTE_DATA_INVALID());
                return;
            }
            w->update();
        });
    }

    // Import
    menu.addAction(QObject::tr("mapWidget.rightMenu.import"), [w, clickChunk, dim] {
        auto fp = QFileDialog::getOpenFileName(nullptr, QObject::tr("mapWidget.rightMenu.importRegion"), {}, msg::BCHKS_FILES());
        if (fp.isEmpty()) return;
        w->import_overlay_->startImport(fp, dim, clickChunk);
        w->update();
    });
    menu.addSeparator();

    // === Group 3: Copy info ===
    auto *copyMenu = menu.addMenu(QObject::tr("mapWidget.rightMenu.copyInfo"));
    copyMenu->addAction(QObject::tr("mapWdiget.rightMenu.copyBlockName") + QString(blockInfo.block_name.c_str()),
                        [cb, &blockInfo] { cb->setText(blockInfo.block_name.c_str()); });
    copyMenu->addAction(QObject::tr("mapWidget.rightMenu.copyBiomeName") + QString::fromStdString(bl::get_biome_name(blockInfo.biome)),
                        [cb, &blockInfo] { /* cb->setText(bl::get_biome_name(blockInfo.biome).c_str()); */ });
    copyMenu->addAction(QObject::tr("mapWidget.rightMenu.copyAltitude") + QString::number(blockInfo.height),
                        [cb, &blockInfo] { cb->setText(QString::number(blockInfo.height)); });
    auto tpCmd = QString("tp @s %1 ~ %2").arg(QString::number(cursorPos.x), QString::number(cursorPos.z));
    copyMenu->addAction(QObject::tr("mapWidget.rightMenu.copyTPCommand") + tpCmd, [cb, tpCmd] { cb->setText(tpCmd); });
    menu.addSeparator();

    // === Group 4: Screenshot ===
    if (insideSelection) {
        menu.addAction(QObject::tr("mapWidget.rightMenu.saveSelectionScreenshot"), [w] { w->saveSelectionImage(); });
    } else {
        menu.addAction(QObject::tr("mapWidget.rightMenu.saveScreenshot"), [w] { w->saveFullscreenImage(); });
    }
    menu.addSeparator();

    // === Group 5: 3D ===
    if (insideSelection && w->selection_.rectCount() == 1) {
        menu.addAction(QObject::tr("mapWidget.rightMenu.view3D"), [w] {
            auto rect = w->selection_.region().boundingRect();
            bl::chunk_pos minPos(rect.x(), rect.y(), w->option_.dim);
            bl::chunk_pos maxPos(rect.x() + rect.width() - 1, rect.y() + rect.height() - 1, w->option_.dim);
            w->chunk_render_window_->showChunks(minPos, maxPos, *w->level_loader_);
        });
    }

    menu.addAction(QObject::tr("mapWidget.rightMenu.openchunkEditor"), [w, &cursorPos] {
        auto cp = cursorPos.to_chunk_pos();
        cp.dim = w->renderOption().dim;
        emit w->requestOpenChunkEditor(cp);
    });
    menu.exec(globalPos);
}
