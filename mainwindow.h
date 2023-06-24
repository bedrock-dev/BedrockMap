#include <vector>

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <unordered_map>

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

       public slots:
        void updateXZEdit(int x, int z);

       private slots:
        void on_goto_btn_clicked();
        void on_grid_checkbox_stateChanged(int arg1);

        void on_text_checkbox_stateChanged(int arg1);

       private:
        Ui::MainWindow *ui;
        std::unordered_map<MapWidget::LayerType, QPushButton *> layer_btns;
        std::unordered_map<MapWidget::DimType, QPushButton *> dim_btns;

        MapWidget *map;
        world world;
    };

#endif  // MAINWINDOW_H
