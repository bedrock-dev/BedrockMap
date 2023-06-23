#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "mapwidget.h"

QT_BEGIN_NAMESPACE
    namespace Ui { class MainWindow; }
QT_END_NAMESPACE

    class MainWindow : public QMainWindow
{
        Q_OBJECT

       public:
        MainWindow(QWidget *parent = nullptr);
        ~MainWindow();

       private:
        Ui::MainWindow *ui;
        MapWidget *map;
        world world;
    };

#endif  // MAINWINDOW_H
