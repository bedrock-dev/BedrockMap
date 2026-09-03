#ifndef BEDROCKMAP_PROGRESSBARWIDGET_H
#define BEDROCKMAP_PROGRESSBARWIDGET_H

#include <QLabel>
#include <QObject>
#include <QProgressBar>
#include <QWidget>

class ProgressBarWidget : public QWidget {
    Q_OBJECT
   public:
    ProgressBarWidget(QWidget* parent);
    ProgressBarWidget();

    // setup
    void setup(const QString& title, int max);

   public slots:
    void setValue(int value);

   private:
    QProgressBar* bar_{nullptr};
    QLabel* title_{nullptr};
    QLabel* progress_{nullptr};
};

#endif
