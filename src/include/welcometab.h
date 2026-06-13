#pragma once

#include <QWidget>

class QLabel;
class QVBoxLayout;

class WelcomeTab : public QWidget {
    Q_OBJECT
   public:
    explicit WelcomeTab(QWidget *parent = nullptr);

    void setRecentPaths(const QStringList &paths);

   signals:
    void newLevelRequested();
    void openLevelRequested();
    void openNbtEditorRequested();
    void openRecentLevel(const QString &path);

   private:
    void setupUI();
    void rebuildRecentList();

    QVBoxLayout *recent_layout_ = nullptr;
    QStringList recent_paths_;
};
