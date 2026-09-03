#ifndef BEDROCKMAP_WORLDLISTTAB_H
#define BEDROCKMAP_WORLDLISTTAB_H
#include <QEnterEvent>
#include <QLabel>
#include <QListWidget>
#include <QWidget>
#include <vector>

#include "levelpathmanager.h"

class QVBoxLayout;
class SectionHeader;

// ---------------------------------------------------------------------------
// WorldListItem — single row widget inside a QListWidget
// ---------------------------------------------------------------------------
class WorldListItem : public QWidget {
    Q_OBJECT
   public:
    explicit WorldListItem(const LevelPathInfo &info, QWidget *parent = nullptr);

    void setInfo(const LevelPathInfo &info);
    const std::string &path() const { return info_.path; }

   signals:
    void clicked(const std::string &path);

   protected:
    void mousePressEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

   private:
    void setupUI();
    QPixmap loadThumbnail() const;
    QString formatSize(int64_t bytes) const;
    QString formatTime(int64_t timestamp) const;

    LevelPathInfo info_;
    QLabel *thumb_label_ = nullptr;
    QLabel *name_label_ = nullptr;
    QLabel *version_label_ = nullptr;
    QLabel *path_label_ = nullptr;
    QLabel *time_label_ = nullptr;
    QLabel *size_label_ = nullptr;
};

// ---------------------------------------------------------------------------
// WorldListTab — tab widget with three world lists
// ---------------------------------------------------------------------------
class WorldListTab : public QWidget {
    Q_OBJECT
   public:
    explicit WorldListTab(QWidget *parent = nullptr);

    void setRecentPaths(const QStringList &paths);
    void setDiscoveredLevels(const std::vector<LevelPathInfo> &levels);

   signals:
    void openLevelRequested(const QString &path);

   private:
    void setupUI();
    void rebuildRecentList();
    void rebuildScannedLists();
    void loadNextRecentInfo();

    QListWidget *createSection(QVBoxLayout *parent, const QString &title, SectionHeader **outHeader = nullptr);

    // -- sections --
    QListWidget *recent_list_ = nullptr;
    QListWidget *release_list_ = nullptr;
    QListWidget *preview_list_ = nullptr;
    SectionHeader *recent_header_ = nullptr;

    // -- data --
    QStringList recent_paths_;
    std::vector<LevelPathInfo> discovered_levels_;

    // lazy async loader for recent items
    int next_recent_idx_ = 0;
    QFutureWatcher<LevelPathInfo> *recent_watcher_ = nullptr;
};

// ---------------------------------------------------------------------------
// helper widget for a clickable section header with toggle
// ---------------------------------------------------------------------------
class SectionHeader : public QWidget {
    Q_OBJECT
   public:
    SectionHeader(const QString &title, int count, QWidget *parent = nullptr);

    void setCount(int count);

   signals:
    void toggled(bool expanded);

   private:
    QLabel *title_label_ = nullptr;
    QLabel *count_label_ = nullptr;
};
#endif  // BEDROCKMAP_WORLDLISTTAB_H
