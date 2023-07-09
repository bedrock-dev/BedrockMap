#include <QApplication>
#include <QCache>
#include <QDebug>
#include <QDesktopWidget>
#include <QIcon>
#include <QImage>

#include "asynclevelloader.h"
#include "mainwindow.h"
#include "iconmanager.h"

int main(int argc, char *argv[]) {
    InitIcons();
    QApplication a(argc, argv);
    QFont f = a.font();
    f.setFamily("微软雅黑");
    f.setPointSize(10);
    a.setFont(f);
    MainWindow w;
    w.setWindowTitle("LevelMap v0.1");
    QIcon icon(":/res/map.ico");  // 图标文件的资源路径
    w.setWindowIcon(icon);        // 设置窗口图标
    w.show();
    return a.exec();
}
