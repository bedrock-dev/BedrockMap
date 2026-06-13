#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QDialog>
#include <QFutureWatcher>
#include <QKeyEvent>
#include <QLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>
#include <vector>

#include "aboutdialog.h"
#include "cachemgr.h"
#include "chunkeditorwidget.h"
#include "leveltabwidget.h"
#include "mapitemeditor.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

   public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

   private:
    void setupUI();
    void setupMenuBar();
    void setupMenuActions();
    void setupToolBar();
    void rebuildRecentMenu();

   public slots:
    inline bool enable_write() const { return this->write_mode_; }
    void openNBTEditor();

   private slots:
    void openLevel();
    void close_and_exit();
    void setupShortcuts();

   private:
    QString getStaticTitle();

    // UI
    QMenuBar *menu_bar_;
    QMenu *file_menu_;
    QMenu *tool_menu_;
    QMenu *help_menu_;
    QMenu *dev_menu_;
    QMenu *recent_menu_{nullptr};

    QAction *action_open_;
    QAction *action_new_;
    QAction *action_close_;
    QAction *action_exit_;
    QAction *action_transparent_void_;
    QAction *action_map_item_;
    QAction *action_NBT_;
    QAction *action_settings_;
    QAction *action_help_;
    QAction *action_opensource_;
    QAction *action_about_;
    QAction *action_levelDB_;
    QAction *action_debug_;
    QAction *action_abort_global_data_loading_;

    LevelTabWidget *level_tab_widget_;

    bool write_mode_{false};

    AboutDialog *about_dialog_{nullptr};
    MapItemEditor *map_item_editor_;
    std::vector<QShortcut *> shortcuts_;
    CacheManager cache_mgr_;
};

#endif  // MAINWINDOW_H
