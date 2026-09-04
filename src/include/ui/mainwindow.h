#ifndef BEDROCKMAP_MAINWINDOW_H
#define BEDROCKMAP_MAINWINDOW_H

#include <QCloseEvent>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFutureWatcher>
#include <QKeyEvent>
#include <QLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>
#include <vector>

#include "aboutdialog.h"
#include "chunkeditorwidget.h"
#include "levelpathmanager.h"
#include "leveltabwidget.h"

class UpdateChecker;

class MainWindow : public QMainWindow {
    Q_OBJECT

   public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

   protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

   private:
    void setupUI();
    void setupMenuBar();
    void setupMenuActions();
    void setupWelcomeTabActions();
    void setupToolBar();
    void rebuildRecentMenu();

   public slots:
    inline bool enable_write() const { return this->write_mode_; }

   private slots:
    void openLevel(const QString &startPath = QString());
    void openFile();
    void close_and_exit();
    void setupShortcuts();
    void onUpdateAvailable(const QString &newVersion, const QString &releaseNotes, const QString &htmlUrl);

   private:
    QString getStaticTitle();
    MapWidget *getCurrentMapWidget();
    bool openDataFile(const QString &path);
    bool canOpenDroppedPath(const QString &path) const;
    void openDroppedPath(const QString &path);
    bool hasOpenableDrop(const QMimeData *mimeData) const;
    bool handleDragEnter(QDragEnterEvent *event);
    bool handleDrop(QDropEvent *event);

    // UI
    QMenuBar *menu_bar_;
    QMenu *file_menu_;
    QMenu *tool_menu_;
    QMenu *layers_menu_;
    QMenu *selection_menu_;
    QMenu *chunk_menu_;
    QMenu *help_menu_;
    QMenu *recent_menu_{nullptr};

    QAction *action_open_;
    QAction *action_open_file_;
    QMenu *new_menu_{nullptr};
    QAction *action_new_nbt_{nullptr};
    QAction *action_save_;
    QAction *action_close_;
    QAction *action_exit_;
    QAction *action_transparent_void_;
    QAction *action_settings_;
    QAction *action_help_;
    QAction *action_opensource_;
    QAction *action_about_;
    QAction *action_levelDB_;
    QAction *action_debug_;
    QAction *action_coords_minimap_;
    QAction *action_layers_grid_;
    QAction *action_layers_coords_;
    QAction *action_layers_overworld_;
    QAction *action_layers_nether_;
    QAction *action_layers_end_;
    QAction *action_layers_custom_dim_;
    QAction *action_layers_terrain_;
    QAction *action_layers_biome_;
    QAction *action_layers_slime_;
    QAction *action_layers_actors_;
    QAction *action_layers_village_;
    QAction *action_layers_hsa_;
    QAction *action_layers_filter_;
    QAction *action_sel_replace_;
    QAction *action_sel_add_;
    QAction *action_sel_subtract_;
    QAction *action_sel_clear_;
    QAction *action_ch_copy_;
    QAction *action_ch_paste_;
    QAction *action_ch_export_;
    QAction *action_ch_import_;
    QAction *action_ch_delete_;
    QAction *action_ch_void_;
    QAction *action_ch_biome_;
    QAction *action_ch_screenshot_;
    QAction *action_ch_3d_;
    QAction *action_goto_;

    LevelTabWidget *level_tab_widget_;

    bool write_mode_{false};

    AboutDialog *about_dialog_{nullptr};
    UpdateChecker *updater_{nullptr};
    std::vector<QShortcut *> shortcuts_;
    LevelPathManager level_path_mgr_;
};

#endif  // BEDROCKMAP_MAINWINDOW_H
