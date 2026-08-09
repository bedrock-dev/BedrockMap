#ifndef LEVE_TAB_WIDGET_H
#define LEVE_TAB_WIDGET_H

#include <qfuturewatcher.h>
#include <qmessagebox.h>
#include <qtmetamacros.h>
#include <qwidget.h>

#include <QTableWidget>

#include "levelpagewidget.h"
#include "mapwidget.h"
#include "renderfilterdialog.h"
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
    void openLevelDBDebugDialog();
    bool confirmCloseAllLevels();

    WorldListTab *welcomeTab() const { return welcome_tab_; }

   public:
    void setEnableDebugWindow(bool enable);
    LevelPageWidget *currentLevelPage();

   signals:
    void currentLevelChanged(LevelPageWidget *levePage);

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
    QFutureWatcher<void> close_level_watcher_;
    LevelPageWidget *closing_page_{nullptr};
    QDialog *close_level_mss_box_;
};
#endif
