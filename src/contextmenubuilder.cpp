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
    int dim = w->option_.dim;

    // === Group 1: Goto ===
    menu.addAction(QObject::tr("mapWidget.rightMenu.gotoPosition"), [w] { w->gotoPositionAction(); });
    menu.addSeparator();

    // === Group 2: Selection operations ===
    if (insideSelection) {
        menu.addAction(QObject::tr("mapWidget.rightMenu.unselect"), [w] { w->clearSelection(); });

        auto *selMenu = menu.addMenu(QObject::tr("mapWidget.rightMenu.selectionOps"));
        selMenu->addAction(QObject::tr("mapWidget.rightMenu.delete"), [w, dim] { w->deleteSelection(dim); });
        selMenu->addAction(QObject::tr("mapWidget.rightMenu.createVoid"), [w, dim] { w->createVoidSelection(dim); });
        selMenu->addAction(QObject::tr("mapWidget.rightMenu.setBiome"), [w, dim] {
            if (w->modificationBlocked()) return;
            BiomePickerDialog dlg(w);
            if (dlg.exec() == QDialog::Accepted) {
                w->setSelectionBiome(dlg.selectedBiome(), dim);
            }
        });
        selMenu->addAction(QObject::tr("mapWidget.rightMenu.copy"), [w, dim] { w->copySelectionToClipboard(dim); });
        selMenu->addAction(QObject::tr("mapWidget.rightMenu.export"), [w, dim] { w->exportSelectionToFile(dim); });
        menu.addSeparator();
    }

    // Paste — always check clipboard, show only if data available
    const auto *pasteMd = cb->mimeData();
    if (pasteMd && pasteMd->hasFormat("application/x-bedrockmap-region") && !pasteMd->data("application/x-bedrockmap-region").isEmpty()) {
        menu.addAction(QObject::tr("mapWidget.rightMenu.paste"), [w, clickChunk, dim] {
            if (w->modificationBlocked()) return;
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
        if (w->modificationBlocked()) return;
        auto fp = QFileDialog::getOpenFileName(nullptr, QObject::tr("mapWidget.rightMenu.importRegion"), {}, msg::BCHKS_FILES());
        if (fp.isEmpty()) return;
        w->import_overlay_->startImport(fp, dim, clickChunk);
        w->update();
    });
    menu.addSeparator();

    // === Group 3: Copy info ===
    auto *copyMenu = menu.addMenu(QObject::tr("mapWidget.rightMenu.copyInfo"));
    auto blockName = w->level_loader_->getBlockName(cursorPos, w->option_.dim);
    copyMenu->addAction(QObject::tr("mapWidget.rightMenu.copyBlockName") + QString(blockName.c_str()),
                        [cb, blockName] { cb->setText(QString::fromStdString(blockName)); });
    copyMenu->addAction(QObject::tr("mapWidget.rightMenu.copyBiomeName") + QString(bl::get_biome_name(blockInfo.biome).c_str()),
                        [cb, &blockInfo] { cb->setText(bl::get_biome_name(blockInfo.biome).c_str()); });
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
            w->voxel_preview_window_->loadChunksAsync(minPos, maxPos, *w->level_loader_);
        });
    }

    menu.addAction(QObject::tr("mapWidget.rightMenu.openchunkEditor"), [w, &cursorPos] {
        auto cp = cursorPos.to_chunk_pos();
        cp.dim = w->renderOption().dim;
        emit w->requestOpenChunkEditor(cp);
    });
    menu.exec(globalPos);
}
