#include "mainwindow.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QIcon>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QPixmap>
#include <QScreen>
#include <QSplitter>
#include <QStandardPaths>
#include <QtConcurrent>
#include <string>

#include "aboutdialog.h"
#include "biomepickerdialog.h"
#include "config.h"
#include "include/leveltabwidget.h"
#include "leveloperator.h"
#include "loguru/loguru.hpp"
#include "mainwindow.h"
#include "mapitemeditor.h"
#include "nbtwidget.h"
#include "newlevelform.h"

namespace {

    [[nodiscard]] QRect centerMainWindowGeometry(double rate) {
        auto const rec = QApplication::primaryScreen()->geometry();
        auto const height = static_cast<int>(rec.height() * rate);
        auto const width = static_cast<int>(rec.width() * rate);
        return {(rec.width() - width) / 2, (rec.height() - height) / 2, width, height};
    }

}  // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setGeometry(centerMainWindowGeometry(0.6));
    setWindowIcon(QIcon(":/res/ui/icon.png"));

    setupUI();
    setupMenuBar();
    setupMenuActions();
    setupWelcomeTabActions();
    this->about_dialog_ = new AboutDialog(this);
}

MainWindow::~MainWindow() {}

void MainWindow::setupUI() {
    auto *cw = new QWidget(this);
    auto *layout = new QVBoxLayout(cw);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    cw->setLayout(layout);
    setCentralWidget(cw);

    this->level_tab_widget_ = new LevelTabWidget(this);
    layout->addWidget(this->level_tab_widget_);
}

void MainWindow::setupMenuBar() {
    menu_bar_ = menuBar();

    // --- File menu ---
    file_menu_ = menu_bar_->addMenu(tr("mainWindow.menu.file"));

    action_open_ = file_menu_->addAction(tr("mainWindow.menu.open"));
    action_open_->setShortcut(QKeySequence("Ctrl+O"));

    action_new_ = file_menu_->addAction(tr("mainWindow.menu.new"));
    action_new_->setShortcut(QKeySequence("Ctrl+N"));
    action_new_->setVisible(false);  // hidden — has bugs, disabled temporarily

    action_save_ = file_menu_->addAction(tr("mainWindow.menu.save"));
    action_save_->setShortcut(QKeySequence("Ctrl+S"));

    action_close_ = file_menu_->addAction(tr("mainWindow.menu.close"));
    action_close_->setShortcut(QKeySequence("Ctrl+W"));
    action_exit_ = file_menu_->addAction(tr("mainWindow.menu.exit"));
    action_exit_->setShortcut(QKeySequence("Ctrl+Q"));

    // --- Layers menu ---
    layers_menu_ = menu_bar_->addMenu(tr("mainWindow.menu.layers"));

    action_layers_grid_ = layers_menu_->addAction(tr("levelPageWidget.toolBar.showGrid"));
    action_layers_grid_->setShortcut(QKeySequence("Alt+G"));
    action_layers_coords_ = layers_menu_->addAction(tr("levelPageWidget.toolBar.showCoord"));
    action_layers_coords_->setShortcut(QKeySequence("Alt+C"));
    layers_menu_->addSeparator();

    action_layers_overworld_ = layers_menu_->addAction(tr("levelPageWidget.toolBar.overworld"));
    action_layers_overworld_->setShortcut(QKeySequence("Alt+1"));
    action_layers_nether_ = layers_menu_->addAction(tr("levelPageWidget.toolBar.nether"));
    action_layers_nether_->setShortcut(QKeySequence("Alt+2"));
    action_layers_end_ = layers_menu_->addAction(tr("levelPageWidget.toolBar.theend"));
    action_layers_end_->setShortcut(QKeySequence("Alt+3"));
    layers_menu_->addSeparator();

    action_layers_terrain_ = layers_menu_->addAction(tr("levelPageWidget.toolBar.terrain"));
    action_layers_terrain_->setShortcut(QKeySequence("Alt+T"));
    action_layers_biome_ = layers_menu_->addAction(tr("levelPageWidget.toolBar.biome"));
    action_layers_biome_->setShortcut(QKeySequence("Alt+B"));
    layers_menu_->addSeparator();

    action_layers_slime_ = layers_menu_->addAction(tr("levelPageWidget.toolBar.slimeChunks"));
    action_layers_slime_->setShortcut(QKeySequence("Alt+S"));
    action_layers_actors_ = layers_menu_->addAction(tr("levelPageWidget.toolBar.entities"));
    action_layers_actors_->setShortcut(QKeySequence("Alt+A"));
    action_layers_village_ = layers_menu_->addAction(tr("levelPageWidget.toolBar.villages"));
    action_layers_village_->setShortcut(QKeySequence("Alt+V"));
    action_layers_hsa_ = layers_menu_->addAction(tr("levelPageWidget.toolBar.HSAs"));
    action_layers_hsa_->setShortcut(QKeySequence("Alt+H"));
    action_transparent_void_ = layers_menu_->addAction(tr("mainWindow.menu.transparentVoid"));
    action_transparent_void_->setShortcut(QKeySequence("Alt+0"));
    layers_menu_->addSeparator();

    action_layers_filter_ = layers_menu_->addAction(tr("levelPageWidget.toolBar.filter"));
    action_layers_filter_->setShortcut(QKeySequence("Alt+F"));
    layers_menu_->addSeparator();

    action_debug_ = layers_menu_->addAction(tr("mainWindow.menu.debugWindow"));
    action_debug_->setShortcut(QKeySequence("Alt+D"));

    // --- Selection menu ---
    selection_menu_ = menu_bar_->addMenu(tr("mainWindow.menu.selection"));

    action_sel_replace_ = selection_menu_->addAction(tr("mainWindow.menu.selReplace"));
    action_sel_replace_->setShortcut(QKeySequence("Shift+R"));
    action_sel_add_ = selection_menu_->addAction(tr("mainWindow.menu.selAdd"));
    action_sel_add_->setShortcut(QKeySequence("Shift+A"));
    action_sel_subtract_ = selection_menu_->addAction(tr("mainWindow.menu.selSubtract"));
    action_sel_subtract_->setShortcut(QKeySequence("Shift+S"));
    action_sel_clear_ = selection_menu_->addAction(tr("mainWindow.menu.selClear"));
    action_sel_clear_->setShortcut(QKeySequence("Shift+C"));

    // --- Chunk menu ---
    chunk_menu_ = menu_bar_->addMenu(tr("mainWindow.menu.chunk"));

    action_ch_copy_ = chunk_menu_->addAction(tr("mainWindow.menu.selCopy"));
    action_ch_copy_->setShortcut(QKeySequence("Ctrl+C"));
    action_ch_paste_ = chunk_menu_->addAction(tr("mainWindow.menu.selPaste"));
    action_ch_paste_->setShortcut(QKeySequence("Ctrl+V"));
    action_ch_export_ = chunk_menu_->addAction(tr("mainWindow.menu.selExport"));
    action_ch_export_->setShortcut(QKeySequence("Ctrl+E"));
    action_ch_import_ = chunk_menu_->addAction(tr("mainWindow.menu.selImport"));
    action_ch_import_->setShortcut(QKeySequence("Ctrl+I"));
    chunk_menu_->addSeparator();
    action_ch_delete_ = chunk_menu_->addAction(tr("mainWindow.menu.selDelete"));
    action_ch_delete_->setShortcut(QKeySequence("Ctrl+D"));
    action_ch_void_ = chunk_menu_->addAction(tr("mainWindow.menu.selVoid"));
    action_ch_void_->setShortcut(QKeySequence("Ctrl+K"));
    action_ch_biome_ = chunk_menu_->addAction(tr("mainWindow.menu.selBiome"));
    action_ch_biome_->setShortcut(QKeySequence("Ctrl+B"));
    action_ch_screenshot_ = chunk_menu_->addAction(tr("mainWindow.menu.selScreenshot"));
    action_ch_screenshot_->setShortcut(QKeySequence("Ctrl+T"));
    action_ch_3d_ = chunk_menu_->addAction(tr("mainWindow.menu.sel3D"));
    action_ch_3d_->setShortcut(QKeySequence("Ctrl+H"));

    // --- Tool menu ---
    tool_menu_ = menu_bar_->addMenu(tr("mainWindow.menu.tool"));

    action_NBT_ = tool_menu_->addAction(tr("mainWindow.menu.nbtEditor"));
    action_levelDB_ = tool_menu_->addAction(tr("mainWindow.menu.showLevelDBData"));
    action_settings_ = tool_menu_->addAction(tr("mainWindow.menu.openCfgFile"));

    // --- Global shortcuts (not in any menu) ---
    action_goto_ = new QAction(tr("mainWindow.menu.goto"), this);
    action_goto_->setShortcut(QKeySequence("Ctrl+G"));
    addAction(action_goto_);

    // --- Help menu ---
    help_menu_ = menu_bar_->addMenu(tr("mainWindow.menu.help"));

    action_help_ = help_menu_->addAction(tr("mainWindow.menu.helpAction"));
    action_opensource_ = help_menu_->addAction(tr("mainWindow.menu.opensource"));
    help_menu_->addSeparator();
    action_about_ = help_menu_->addAction(tr("mainWindow.menu.about"));
}

void MainWindow::setupMenuActions() {
    // File — insert recent menu between open and new
    recent_menu_ = new QMenu(tr("mainWindow.menu.openRecent"), this);
    file_menu_->insertMenu(action_new_, recent_menu_);
    rebuildRecentMenu();

    connect(action_open_, &QAction::triggered, this, &MainWindow::openLevel);
    connect(action_new_, &QAction::triggered, this, [this]() {
        NewLevelForm frm(this);
        if (frm.exec() == QDialog::Accepted) {
            LevelOperator::newLevel(frm.params());
        }
    });
    connect(action_save_, &QAction::triggered, this, [this]() {
        auto *page = level_tab_widget_->currentLevelPage();
        if (page && page->isDirty()) {
            page->commit();
        }
    });
    connect(action_close_, &QAction::triggered, this, [this]() { level_tab_widget_->closeCurrentLevel(); });
    connect(action_exit_, &QAction::triggered, this, &MainWindow::close_and_exit);

    // Tools
    connect(action_NBT_, &QAction::triggered, this, &MainWindow::openNBTEditor);
    connect(action_goto_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) w->gotoPositionAction();
    });
    connect(action_levelDB_, &QAction::triggered, this, [this]() { this->level_tab_widget_->openLevelDBDebugDialog(); });
    connect(action_settings_, &QAction::triggered, this,
            []() { QDesktopServices::openUrl(QUrl::fromLocalFile(cfg::CONFIG_FILE_PATH.c_str())); });

    // Layers
    using Mr = MapWidget::RenderOption;
    connect(action_layers_grid_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) {
            w->toggleOther(Mr::Grid);
            w->syncToolbars();
        }
    });
    connect(action_layers_coords_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) {
            w->toggleOther(Mr::Coords);
            w->syncToolbars();
        }
    });
    connect(action_layers_overworld_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) {
            w->setDim(Mr::OverWorld);
            w->syncToolbars();
        }
    });
    connect(action_layers_nether_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) {
            w->setDim(Mr::Nether);
            w->syncToolbars();
        }
    });
    connect(action_layers_end_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) {
            w->setDim(Mr::TheEnd);
            w->syncToolbars();
        }
    });
    connect(action_layers_terrain_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) {
            w->setLayer(Mr::Terrain);
            w->syncToolbars();
        }
    });
    connect(action_layers_biome_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) {
            w->setLayer(Mr::Biome);
            w->syncToolbars();
        }
    });
    connect(action_layers_slime_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) {
            w->toggleOther(Mr::SlimeChunk);
            w->syncToolbars();
        }
    });
    connect(action_layers_actors_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) {
            w->toggleOther(Mr::Actors);
            w->syncToolbars();
        }
    });
    connect(action_layers_village_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) {
            w->toggleOther(Mr::Village);
            w->syncToolbars();
        }
    });
    connect(action_layers_hsa_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) {
            w->toggleOther(Mr::HSA);
            w->syncToolbars();
        }
    });
    connect(action_transparent_void_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) {
            w->toggleTransparentVoid();
            w->syncToolbars();
        }
    });
    connect(action_layers_filter_, &QAction::triggered, this, [this]() { level_tab_widget_->onMapOpenFilterDialog(); });
    connect(action_debug_, &QAction::triggered, this, [this]() {
        auto *w = getCurrentMapWidget();
        if (!w) return;
        w->setDrawDebug(!w->isDebugEnabled());
        w->syncToolbars();
    });

    // Selection
    connect(action_sel_replace_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) {
            w->setSelectionMode(SelectionController::Mode::Replace);
            w->syncToolbars();
        }
    });
    connect(action_sel_add_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) {
            w->setSelectionMode(SelectionController::Mode::Add);
            w->syncToolbars();
        }
    });
    connect(action_sel_subtract_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) {
            w->setSelectionMode(SelectionController::Mode::Subtract);
            w->syncToolbars();
        }
    });
    connect(action_sel_clear_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) w->clearSelection();
    });

    // Chunk
    connect(action_ch_copy_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) w->copySelectionToClipboard(static_cast<uint8_t>(w->renderOption().dim));
    });
    connect(action_ch_paste_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) w->pasteFromClipboard(static_cast<uint8_t>(w->renderOption().dim));
    });
    connect(action_ch_export_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) w->exportSelectionToFile(static_cast<uint8_t>(w->renderOption().dim));
    });
    connect(action_ch_import_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) w->importFromFile(static_cast<uint8_t>(w->renderOption().dim));
    });
    connect(action_ch_delete_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) w->deleteSelection(static_cast<uint8_t>(w->renderOption().dim));
    });
    connect(action_ch_void_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) w->createVoidSelection(static_cast<uint8_t>(w->renderOption().dim));
    });
    connect(action_ch_biome_, &QAction::triggered, this, [this]() {
        auto *w = getCurrentMapWidget();
        if (!w || w->selection().isEmpty()) return;
        BiomePickerDialog dlg(w);
        if (dlg.exec() == QDialog::Accepted) {
            w->setSelectionBiome(dlg.selectedBiome(), static_cast<uint8_t>(w->renderOption().dim));
        }
    });
    connect(action_ch_screenshot_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) w->saveSelectionImage();
    });
    connect(action_ch_3d_, &QAction::triggered, this, [this]() {
        auto *w = getCurrentMapWidget();
        if (!w) return;
        w->show3DView(static_cast<uint8_t>(w->renderOption().dim));
    });

    // Help
    connect(action_help_, &QAction::triggered, this,
            []() { QDesktopServices::openUrl(QUrl("https://github.com/bedrock-dev/BedrockMap")); });
    connect(action_opensource_, &QAction::triggered, this,
            []() { QDesktopServices::openUrl(QUrl("https://github.com/bedrock-dev/BedrockMap")); });
    connect(action_about_, &QAction::triggered, this, [&]() { about_dialog_->exec(); });
}

void MainWindow::setupWelcomeTabActions() {
    auto *wt = level_tab_widget_->welcomeTab();
    connect(wt, &WelcomeTab::newLevelRequested, this, [this]() {
        NewLevelForm frm(this);
        if (frm.exec() == QDialog::Accepted) {
            LevelOperator::newLevel(frm.params());
        }
    });
    connect(wt, &WelcomeTab::openLevelRequested, this, [this]() { openLevel(); });
    connect(wt, &WelcomeTab::openNbtEditorRequested, this, [this]() { openNBTEditor(); });
    connect(wt, &WelcomeTab::openRecentLevel, this, [this, wt](const QString &path) {
        level_tab_widget_->openNewLevel(path);
        cache_mgr_.addRecentPath(path);
        rebuildRecentMenu();
        wt->setRecentPaths(cache_mgr_.recentPaths());
    });
    wt->setRecentPaths(cache_mgr_.recentPaths());
}

void MainWindow::setupShortcuts() {}

void MainWindow::rebuildRecentMenu() {
    recent_menu_->clear();
    auto paths = cache_mgr_.recentPaths();
    if (paths.isEmpty()) {
        auto *a = recent_menu_->addAction(tr("mainWindow.menu.emptyRecent"));
        a->setEnabled(false);
    } else {
        for (const auto &path : paths) {
            auto *a = recent_menu_->addAction(path);
            connect(a, &QAction::triggered, this, [this, path]() {
                this->level_tab_widget_->openNewLevel(path);
                cache_mgr_.addRecentPath(path);
                rebuildRecentMenu();
            });
        }
    }
    // also refresh the welcome tab
    level_tab_widget_->welcomeTab()->setRecentPaths(paths);
}

void MainWindow::openLevel() {
    auto path = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)[0] + cfg::MCBE_LEVEL_PATH;
    QString root = QFileDialog::getExistingDirectory(this, "", path, QFileDialog::ShowDirsOnly);
    if (root.isEmpty()) return;
    this->level_tab_widget_->openNewLevel(root);
    cache_mgr_.addRecentPath(root);
    rebuildRecentMenu();
}

void MainWindow::close_and_exit() { this->close(); }

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!level_tab_widget_->confirmCloseAllLevels()) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::openNBTEditor() {
    auto *w = new NbtWidget();
    auto g = this->geometry();
    const int ext = 100;
    w->setWindowTitle(tr("nbtEditor.title.nbtEditor"));
    w->setGeometry(QRect(g.x() + ext, g.y() + ext, g.width() - ext * 2, g.height() - ext * 2));
    w->show();
}

QString MainWindow::getStaticTitle() { return cfg::VERSION_STRING(); }

MapWidget *MainWindow::getCurrentMapWidget() {
    auto *page = level_tab_widget_->currentLevelPage();
    if (!page) return nullptr;
    return page->getMapWidget();
}
