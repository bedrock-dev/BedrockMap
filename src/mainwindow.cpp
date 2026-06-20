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
#include "config.h"
#include "include/leveltabwidget.h"
#include "leveloperator.h"
#include "loguru/loguru.hpp"
#include "mainwindow.h"
#include "mapitemeditor.h"
#include "msg.h"
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
    menu_bar_->setVisible(false);

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

    action_close_ = file_menu_->addAction(tr("mainWindow.menu.close"));
    action_exit_ = file_menu_->addAction(tr("mainWindow.menu.exit"));
    action_exit_->setShortcut(QKeySequence("Ctrl+Q"));

    // --- Tool menu ---
    tool_menu_ = menu_bar_->addMenu(tr("mainWindow.menu.tool"));

    action_transparent_void_ = tool_menu_->addAction(tr("mainWindow.menu.transparentVoid"));
    action_transparent_void_->setCheckable(true);
    tool_menu_->addSeparator();
    action_NBT_ = tool_menu_->addAction(tr("mainWindow.menu.nbtEditor"));
    tool_menu_->addSeparator();
    action_settings_ = tool_menu_->addAction(tr("mainWindow.menu.openCfgFile"));

    // --- Developer menu ---
    dev_menu_ = menu_bar_->addMenu(tr("mainWindow.menu.developer"));

    action_levelDB_ = dev_menu_->addAction(tr("mainWindow.menu.showLevelDBData"));
    action_debug_ = dev_menu_->addAction(tr("mainWindow.menu.debugWindow"));
    action_debug_->setCheckable(true);
    action_abort_global_data_loading_ = dev_menu_->addAction(tr("mainWindow.menu.abortGlobalDataLoading"));

    // --- Help menu ---
    help_menu_ = menu_bar_->addMenu(tr("mainWindow.menu.help"));

    action_help_ = help_menu_->addAction(tr("mainWindow.menu.helpAction"));
    action_opensource_ = help_menu_->addAction(tr("mainWindow.menu.opensource"));
    help_menu_->addSeparator();
    action_about_ = help_menu_->addAction(tr("mainWindow.menu.about"));
}

void MainWindow::setupMenuActions() {
    this->map_item_editor_ = new MapItemEditor();

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
    connect(action_exit_, &QAction::triggered, this, &MainWindow::close_and_exit);

    // welcome tab actions
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

    // Tools
    connect(action_transparent_void_, &QAction::triggered, this, [this]() {
        auto checked = action_transparent_void_->isChecked();
        action_transparent_void_->setChecked(checked);
        cfg::transparent_void = checked;
    });
    connect(action_NBT_, &QAction::triggered, this, &MainWindow::openNBTEditor);
    connect(action_settings_, &QAction::triggered, this,
            []() { QDesktopServices::openUrl(QUrl::fromLocalFile(cfg::CONFIG_FILE_PATH.c_str())); });

    // Developer
    connect(action_debug_, &QAction::triggered, this, [this]() {
        auto checked = action_debug_->isChecked();
        action_debug_->setChecked(checked);
        this->level_tab_widget_->setEnableDebugWindow(checked);
    });
    connect(action_abort_global_data_loading_, &QAction::triggered, [this] { LOG_F(INFO, "Canceling global data loading"); });
    connect(action_levelDB_, &QAction::triggered, this, [this]() { this->level_tab_widget_->openLevelDBDebugDialog(); });

    // Help
    connect(action_help_, &QAction::triggered, this,
            []() { QDesktopServices::openUrl(QUrl("https://github.com/bedrock-dev/BedrockMap")); });
    connect(action_opensource_, &QAction::triggered, this,
            []() { QDesktopServices::openUrl(QUrl("https://github.com/bedrock-dev/BedrockMap")); });
    connect(action_about_, &QAction::triggered, this, [&]() { about_dialog_->exec(); });
}

void MainWindow::setupShortcuts() {}

void MainWindow::setupToolBar() {}

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
