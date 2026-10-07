#include "contextmenubuilder.h"

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QMenu>
#include <QMimeData>

#include "biomepickerdialog.h"
#include "clipboarddata.h"
#include "color.h"
#include "maphost.h"
#include "mapview.h"
#include "msg.h"

namespace {
    const QStringList REGION_FILE_EXTENSIONS{QStringLiteral("bchks")};
}

void ContextMenuBuilder::show(MapHost* host, const MapMenuRequest& request) {
    if (!host) return;
    QMenu menu(mapPane(host, request));
    build(menu, host, request);
    if (!menu.isEmpty()) menu.exec(request.global_pos);
}

QWidget* ContextMenuBuilder::mapPane(MapHost* host, const MapMenuRequest& request) {
    if (request.source) return request.source;
    return host ? host->paneWidget() : nullptr;
}

void ContextMenuBuilder::build(QMenu& menu, MapHost* host, const MapMenuRequest& request) {
    if (!host) return;
    auto* view = host->mapView();
    auto* loader = host->levelLoader();
    if (!view || !loader) return;

    // Dialogs and captures belong to the pane the menu was opened on.
    QWidget* parent = mapPane(host, request);
    auto* clipboard = QApplication::clipboard();
    const int dim = request.dim;

    // Membership is tested in chunk coordinates, so an irregular selection
    // answers correctly.
    const QRegion& selection = view->selection().region();
    const bool inside_selection = !selection.isEmpty() && selection.contains(QPoint(request.chunk.x, request.chunk.z));

    menu.addAction(QObject::tr("mapHost.rightMenu.gotoPosition"), [host] { host->gotoPositionAction(); });
    menu.addSeparator();

    if (inside_selection) {
        menu.addAction(QObject::tr("mapHost.rightMenu.unselect"), [host] { host->clearSelection(); });

        auto* sel_menu = menu.addMenu(QObject::tr("mapHost.rightMenu.selectionOps"));
        sel_menu->addAction(QObject::tr("mapHost.rightMenu.delete"), [host, dim] { host->deleteSelection(dim); });
        sel_menu->addAction(QObject::tr("mapHost.rightMenu.createVoid"), [host, dim] { host->createVoidSelection(dim); });
        sel_menu->addAction(QObject::tr("mapHost.rightMenu.setBiome"), [host, dim] {
            BiomePickerDialog dialog(host->paneWidget());
            if (dialog.exec() == QDialog::Accepted) host->setSelectionBiome(dialog.selectedBiome(), dim);
        });
        sel_menu->addAction(QObject::tr("mapHost.rightMenu.copy"), [host, dim] { host->copySelectionToClipboard(dim); });
        sel_menu->addAction(QObject::tr("mapHost.rightMenu.export"), [host, dim] { host->exportSelectionToFile(dim); });
        menu.addSeparator();
    }

    // Paste is offered whenever the clipboard actually holds a region.
    const QMimeData* paste_data = clipboard ? clipboard->mimeData() : nullptr;
    const bool has_region_data = !clipboard_data::read(paste_data, clipboard_data::CHUNK_REGION_MIME_TYPE, REGION_FILE_EXTENSIONS).isEmpty();
    if (has_region_data) {
        menu.addAction(QObject::tr("mapHost.rightMenu.paste"), [host, chunk = request.chunk, dim] {
            auto* currentClipboard = QApplication::clipboard();
            const QByteArray raw = clipboard_data::read(currentClipboard ? currentClipboard->mimeData() : nullptr,
                                                        clipboard_data::CHUNK_REGION_MIME_TYPE, {QStringLiteral("bchks")});
            if (raw.isEmpty()) {
                INFO(msg::PASTE_DATA_EMPTY());
                return;
            }
            if (!host->beginPaste(raw, dim, chunk)) INFO(msg::PASTE_DATA_INVALID());
        });
    }

    menu.addAction(QObject::tr("mapHost.rightMenu.import"), [host, chunk = request.chunk, dim] {
        const QString path =
            QFileDialog::getOpenFileName(host->paneWidget(), QObject::tr("mapHost.rightMenu.importRegion"), {}, msg::BCHKS_FILES());
        if (path.isEmpty()) return;
        host->beginImport(path, dim, chunk);
    });
    menu.addSeparator();

    auto* copy_menu = menu.addMenu(QObject::tr("mapHost.rightMenu.copyInfo"));
    const std::string block_name = loader->getBlockName(request.block, dim);
    const auto info = loader->getBlockTips(request.block, dim);
    const auto to_clipboard = [clipboard](const QString& text) { clipboard->setText(text); };

    const QString block_name_text = QString::fromStdString(block_name);
    copy_menu->addAction(QObject::tr("mapHost.rightMenu.copyBlockName") + block_name_text,
                         [to_clipboard, block_name_text] { to_clipboard(block_name_text); });
    const QString biome_name = QString::fromStdString(bl::get_biome_name(info.biome));
    copy_menu->addAction(QObject::tr("mapHost.rightMenu.copyBiomeName") + biome_name,
                         [to_clipboard, biome_name] { to_clipboard(biome_name); });
    const QString altitude = QString::number(info.height);
    copy_menu->addAction(QObject::tr("mapHost.rightMenu.copyAltitude") + altitude, [to_clipboard, altitude] { to_clipboard(altitude); });
    const QString tp_command = QString("tp @s %1 ~ %2").arg(QString::number(request.block.x), QString::number(request.block.z));
    copy_menu->addAction(QObject::tr("mapHost.rightMenu.copyTPCommand") + tp_command,
                         [to_clipboard, tp_command] { to_clipboard(tp_command); });
    menu.addSeparator();

    // The screenshot comes from whichever map was clicked, so that map is what
    // ends up in the file.
    if (inside_selection) {
        menu.addAction(QObject::tr("mapHost.rightMenu.saveSelectionScreenshot"), [host, parent] { host->saveSelectionImage(parent); });
    } else {
        menu.addAction(QObject::tr("mapHost.rightMenu.saveScreenshot"), [host, parent] { host->saveFullscreenImage(parent); });
    }
    menu.addSeparator();

    if (inside_selection && view->selection().rectCount() == 1) {
        menu.addAction(QObject::tr("mapHost.rightMenu.view3D"), [host, view, dim] {
            const QRect rect = view->selection().region().boundingRect();
            host->showVoxelPreview(bl::chunk_pos(rect.x(), rect.y(), dim),
                                   bl::chunk_pos(rect.x() + rect.width() - 1, rect.y() + rect.height() - 1, dim));
        });
    }

    menu.addAction(QObject::tr("mapHost.rightMenu.openchunkEditor"), [host, chunk = request.chunk, dim] {
        bl::chunk_pos pos = chunk;
        pos.dim = dim;
        host->showChunkEditor(pos);
    });
}
