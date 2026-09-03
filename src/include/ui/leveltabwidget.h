#ifndef BEDROCKMAP_LEVELTABWIDGET_H
#define BEDROCKMAP_LEVELTABWIDGET_H

#include <qmessagebox.h>
#include <qtmetamacros.h>
#include <qwidget.h>

#include <QTableWidget>

#include "asynctask.h"
#include "levelpagewidget.h"
#include "mapwidget.h"
#include "mcstructurepagewidget.h"
#include "nbtfilepagewidget.h"
#include "renderfilterdialog.h"
#include "tabpagewidget.h"
#include "worldlisttab.h"

class LevelDBDebugDialog : public QDialog {
   public:
    LevelDBDebugDialog(QWidget *widget) : QDialog(widget) {
        setWindowTitle("LevelDB Stats");
        setFont(QFont("Consolas"));
        label = new QLabel(this);
        layout = new QVBoxLayout(this);
        layout->addWidget(label);

        this->setLayout(layout);
    }

    void initData(leveldb::DB *db) {
        QStringList data;
        if (db) {
            data << QString("Last Sequence Number:     %1").arg(db->LastSequence());

            uint64_t disk;
            leveldb::Range range(leveldb::Slice(""), leveldb::Slice("\xff\xff\xff\xff"));
            db->GetApproximateSizes(&range, 1, &disk);
            data << QString("Approximate Disk Size:    %1 bytes (%2 MiB)").arg(disk).arg(disk / 1024.0 / 1024.0);

            std::string memStr;
            db->GetProperty("leveldb.approximate-memory-usage", &memStr);
            auto mem = QString::fromStdString(memStr).toUInt();
            data << QString("Approximate Memory Size:  %1 bytes (%2 MiB)").arg(mem).arg(mem / 1024.0 / 1024.0);

            std::string stats;
            db->GetProperty("leveldb.stats", &stats);
            data << QString::fromStdString(stats);
        } else {
            data << "LevelDB not opened";
        }
        label->setText(data.join("\n"));
    }

   private:
    QLabel *label{nullptr};
    QVBoxLayout *layout{nullptr};
};

class LevelTabWidget : public QTabWidget {
    Q_OBJECT
   public:
    LevelTabWidget(QWidget *parent);

    void openNewLevel(const QString &path);
    bool openMcstructure(const QString &path);
    bool openNbtFile(const QString &path);
    bool openNewNbtFile();
    void openLevelDBDebugDialog();
    bool confirmCloseAllLevels();

    WorldListTab *welcomeTab() const { return welcome_tab_; }

   public:
    void setEnableDebugWindow(bool enable);
    LevelPageWidget *currentLevelPage();

   signals:
    void currentLevelChanged(LevelPageWidget *levePage);
    /// A data-file tab (e.g. .nbt / .nbts) was saved to disk.
    void dataFileSaved(const QString &path);

   public slots:
    void onMapDimensionChanged(int);
    void onMapLayerChanged(int);
    void onMapToggleOtherLayer(int);
    void onMapOpenFilterDialog();
    void onMapToggleGlobalDataWidget();
    void closeCurrentLevel();

   private slots:
    void onTabClosed(int index);
    void onCloseLevelFinished();

   private:
    LevelDBDebugDialog *levedb_debug_widget_;
    RenderFilterDialog *render_filter_dialog_;
    WorldListTab *welcome_tab_;
    QMap<int, LevelPageWidget *> level_pages_;

    int index = 0;
    // close level handler
    AsyncTaskRunner close_level_task_;
    LevelPageWidget *closing_page_{nullptr};
    QDialog *close_level_mss_box_;
};
#endif
