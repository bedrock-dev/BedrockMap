#include "mainwindow.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenuBar>
#include <QMessageBox>
#include <QOpenGLWidget>
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
#include "loguru/loguru.hpp"
#include "mainwindow.h"
#include "settingsdialog.h"

namespace {

    [[nodiscard]] QRect centerMainWindowGeometry(double rate) {
        auto const rec = QApplication::primaryScreen()->geometry();
        auto const height = static_cast<int>(rec.height() * rate);
        auto const width = static_cast<int>(rec.width() * rate);
        return {(rec.width() - width) / 2, (rec.height() - height) / 2, width, height};
    }

}  // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setAcceptDrops(true);
    qApp->installEventFilter(this);

    // HACK (Qt 6.4+): when the first QOpenGLWidget appears inside an already
    // visible window, the top-level window switches its surface type from
    // RasterSurface to OpenGLSurface, so Qt destroys and recreates the native
    // window (visible as the main window closing and reopening). Keep a hidden
    // QOpenGLWidget child that is created before the window is shown, so the
    // main window is born as OpenGLSurface and later embedding a voxel view in
    // a tab does not trigger a window recreation.
    auto *gl_warmup = new QOpenGLWidget(this);
    gl_warmup->hide();

    setGeometry(centerMainWindowGeometry(0.6));
    setWindowIcon(QIcon(":/res/ui/icon.png"));

    setupUI();
    setupMenuBar();
    setupMenuActions();
    level_path_mgr_.init();
    level_path_mgr_.scanNormalPaths();
    level_path_mgr_.scanModernPaths();
    // level_path_mgr_.dumpPaths();
    setupWelcomeTabActions();
    this->about_dialog_ = new AboutDialog(this);
}

MainWindow::~MainWindow() { qApp->removeEventFilter(this); }

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

    action_open_file_ = file_menu_->addAction(tr("mainWindow.menu.openFile"));
    action_open_file_->setShortcut(QKeySequence("Ctrl+Shift+O"));

    new_menu_ = file_menu_->addMenu(tr("mainWindow.menu.new"));
    action_new_nbt_ = new_menu_->addAction(tr("mainWindow.menu.newNbt"));
    action_new_nbt_->setShortcut(QKeySequence("Ctrl+N"));

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
    action_layers_custom_dim_ = layers_menu_->addAction(tr("levelPageWidget.toolBar.customDim"));
    action_layers_custom_dim_->setShortcut(QKeySequence("Alt+4"));
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

    action_levelDB_ = tool_menu_->addAction(tr("mainWindow.menu.showLevelDBData"));
    action_settings_ = tool_menu_->addAction(tr("mainWindow.menu.openCfgFile"));
    action_settings_->setShortcut(QKeySequence("Ctrl+,"));

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
    file_menu_->insertMenu(new_menu_->menuAction(), recent_menu_);
    rebuildRecentMenu();

    //    if (auto *m = buildLeviMenu()) file_menu_->insertMenu(new_menu_, m);

    connect(action_open_, &QAction::triggered, this, [this]() { openLevel(); });

    connect(action_open_file_, &QAction::triggered, this, &MainWindow::openFile);

    connect(action_new_nbt_, &QAction::triggered, this, [this]() { level_tab_widget_->openNewNbtFile(); });
    connect(level_tab_widget_, &LevelTabWidget::dataFileSaved, this, [this](const QString &path) {
        level_path_mgr_.addRecentPath(path);
        rebuildRecentMenu();
    });
    connect(action_save_, &QAction::triggered, this, [this]() {
        // Save the current tab (world page or data-file page such as .nbt).
        auto *page = qobject_cast<TabPageWidget *>(level_tab_widget_->currentWidget());
        if (page && page->isDirty()) {
            page->commit();
        }
    });
    connect(action_close_, &QAction::triggered, this, [this]() { level_tab_widget_->closeCurrentLevel(); });
    connect(action_exit_, &QAction::triggered, this, &MainWindow::close_and_exit);

    // Tools
    connect(action_goto_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) w->gotoPositionAction();
    });
    connect(action_levelDB_, &QAction::triggered, this, [this]() { this->level_tab_widget_->openLevelDBDebugDialog(); });
    connect(action_settings_, &QAction::triggered, this, [this]() {
        SettingsDialog dialog(this);
        dialog.exec();
    });

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
    connect(action_layers_custom_dim_, &QAction::triggered, this, [this]() {
        auto *w = getCurrentMapWidget();
        if (!w) return;
        auto *loader = w->getLevelLoader();
        if (!loader) return;
        const auto &dimTable = loader->level().custom_dimension_table();
        if (dimTable.empty()) {
            QMessageBox::information(this, tr("mainWindow.customDim"), tr("mainWindow.customDim.empty"));
            return;
        }
        QDialog dlg(this);
        dlg.setWindowTitle(tr("mainWindow.customDim"));
        auto *layout = new QVBoxLayout(&dlg);
        auto *list = new QListWidget(&dlg);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        std::vector<int> dimIds;
        for (const auto &[name, id] : dimTable) {
            list->addItem(QString("%1 (ID: %2)").arg(QString::fromStdString(name)).arg(id));
            dimIds.push_back(id);
        }
        list->setCurrentRow(0);
        layout->addWidget(list);
        layout->setContentsMargins({2, 2, 2, 2});
        auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        layout->addWidget(btnBox);
        dlg.resize(320, 240);
        if (dlg.exec() == QDialog::Accepted && list->currentRow() >= 0) {
            w->setDim(dimIds[list->currentRow()]);
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
        if (auto *w = getCurrentMapWidget()) w->copySelectionToClipboard(w->renderOption().dim);
    });
    connect(action_ch_paste_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) w->pasteFromClipboard(w->renderOption().dim);
    });
    connect(action_ch_export_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) w->exportSelectionToFile(w->renderOption().dim);
    });
    connect(action_ch_import_, &QAction::triggered, this, [this]() {
        if (auto *w = getCurrentMapWidget()) w->importFromFile(w->renderOption().dim);
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
    LOG_F(INFO, "MainWindow::setupWelcomeTabActions start");
    auto *wt = level_tab_widget_->welcomeTab();

    // clicking any world item (recent / release / preview) opens the level
    connect(wt, &WorldListTab::openLevelRequested, this, [this](const QString &path) {
        LOG_F(INFO, "WorldListTab openLevelRequested path=%s", path.toStdString().c_str());
        if (openDataFile(path)) {
            level_path_mgr_.addRecentPath(path);
            rebuildRecentMenu();  // also updates welcome tab
        } else {
            level_tab_widget_->openNewLevel(path);
            level_path_mgr_.addRecentPath(path);
            rebuildRecentMenu();  // also updates welcome tab
        }
    });

    // populate data
    LOG_F(INFO, "MainWindow setting recent paths");
    wt->setRecentPaths(level_path_mgr_.recentPaths());
    LOG_F(INFO, "MainWindow setting discovered levels");
    wt->setDiscoveredLevels(level_path_mgr_.discoveredLevels());
    LOG_F(INFO, "MainWindow::setupWelcomeTabActions end");
}

void MainWindow::setupShortcuts() {}

void MainWindow::rebuildRecentMenu() {
    recent_menu_->clear();
    auto paths = level_path_mgr_.recentPaths();
    if (paths.isEmpty()) {
        auto *a = recent_menu_->addAction(tr("mainWindow.menu.emptyRecent"));
        a->setEnabled(false);
    } else {
        for (const auto &path : paths) {
            auto *a = recent_menu_->addAction(path);
            connect(a, &QAction::triggered, this, [this, path]() {
                if (openDataFile(path)) {
                    level_path_mgr_.addRecentPath(path);
                    rebuildRecentMenu();
                } else {
                    this->level_tab_widget_->openNewLevel(path);
                    level_path_mgr_.addRecentPath(path);
                    rebuildRecentMenu();
                }
            });
        }
        // refresh the welcome tab once
        level_tab_widget_->welcomeTab()->setRecentPaths(paths);
    }
}

void MainWindow::openLevel(const QString &startPath) {
    auto path = startPath;
    if (path.isEmpty()) path = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)[0] + constant::MCBE_LEVEL_PATH;
    QString root = QFileDialog::getExistingDirectory(this, "", path, QFileDialog::ShowDirsOnly);
    if (root.isEmpty()) return;
    this->level_tab_widget_->openNewLevel(root);
    level_path_mgr_.addRecentPath(root);
    rebuildRecentMenu();
}

void MainWindow::openFile() {
    const auto path = QFileDialog::getOpenFileName(this, tr("mainWindow.menu.openFile"), {}, tr("mainWindow.openFile.filter"));
    if (path.isEmpty()) return;
    if (openDataFile(path)) {
        level_path_mgr_.addRecentPath(path);
        rebuildRecentMenu();
    }
}

bool MainWindow::openDataFile(const QString &path) {
    if (path.endsWith(".mcstructure", Qt::CaseInsensitive)) {
        return level_tab_widget_->openMcstructure(path);
    }
    if (path.endsWith(".nbt", Qt::CaseInsensitive) || path.endsWith(".nbts", Qt::CaseInsensitive)) {
        return level_tab_widget_->openNbtFile(path);
    }
    return false;
}

bool MainWindow::canOpenDroppedPath(const QString &path) const {
    const QFileInfo info(path);
    if (!info.exists()) return false;
    if (info.isDir()) return true;

    return path.endsWith(QStringLiteral(".mcstructure"), Qt::CaseInsensitive) ||
           path.endsWith(QStringLiteral(".nbt"), Qt::CaseInsensitive) || path.endsWith(QStringLiteral(".nbts"), Qt::CaseInsensitive);
}

void MainWindow::openDroppedPath(const QString &path) {
    const QFileInfo info(path);
    if (info.isDir()) {
        level_tab_widget_->openNewLevel(path);
        level_path_mgr_.addRecentPath(path);
        rebuildRecentMenu();
        return;
    }

    if (openDataFile(path)) {
        level_path_mgr_.addRecentPath(path);
        rebuildRecentMenu();
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (!handleDragEnter(event)) event->ignore();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event) {
    if (hasOpenableDrop(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void MainWindow::dropEvent(QDropEvent *event) {
    if (!handleDrop(event)) event->ignore();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || (widget != this && !isAncestorOf(widget))) return QMainWindow::eventFilter(watched, event);

    if (event->type() == QEvent::DragEnter) {
        auto *dragEvent = static_cast<QDragEnterEvent *>(event);
        if (handleDragEnter(dragEvent)) return true;
    } else if (event->type() == QEvent::DragMove) {
        auto *dragEvent = static_cast<QDragMoveEvent *>(event);
        if (hasOpenableDrop(dragEvent->mimeData())) {
            dragEvent->acceptProposedAction();
            return true;
        }
    } else if (event->type() == QEvent::Drop) {
        auto *dropEvent = static_cast<QDropEvent *>(event);
        if (handleDrop(dropEvent)) return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::hasOpenableDrop(const QMimeData *mimeData) const {
    if (!mimeData || !mimeData->hasUrls()) return false;
    for (const auto &url : mimeData->urls()) {
        if (url.isLocalFile() && canOpenDroppedPath(url.toLocalFile())) return true;
    }
    return false;
}

bool MainWindow::handleDragEnter(QDragEnterEvent *event) {
    if (!hasOpenableDrop(event->mimeData())) return false;
    event->acceptProposedAction();
    return true;
}

bool MainWindow::handleDrop(QDropEvent *event) {
    if (!event->mimeData()->hasUrls()) {
        return false;
    }

    bool openedAny = false;
    for (const auto &url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) continue;
        const auto path = url.toLocalFile();
        if (!canOpenDroppedPath(path)) continue;
        openDroppedPath(path);
        openedAny = true;
    }

    if (openedAny) {
        event->acceptProposedAction();
        return true;
    } else {
        return false;
    }
}

void MainWindow::close_and_exit() { this->close(); }

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!level_tab_widget_->confirmCloseAllLevels()) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

QString MainWindow::getStaticTitle() { return constant::VERSION_STRING(); }

MapWidget *MainWindow::getCurrentMapWidget() {
    auto *page = level_tab_widget_->currentLevelPage();
    if (!page) return nullptr;
    return page->getMapWidget();
}
