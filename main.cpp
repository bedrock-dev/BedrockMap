#include <QApplication>
#include <QCache>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopWidget>
#include <QIcon>
#include <QImage>

#include "asynclevelloader.h"
#include "iconmanager.h"
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    InitIcons();
    QApplication a(argc, argv);
    QFile f(":/qdarkstyle/dark/style.qss");
    if (!f.exists()) {
        printf("Unable to set stylesheet, file not found\n");
    } else {
        f.open(QFile::ReadOnly | QFile::Text);
        QTextStream ts(&f);
        a.setStyleSheet(ts.readAll());
    }
    QFont font = a.font();
    font.setFamily("微软雅黑");
    font.setPointSize(10);
    a.setFont(font);

    MainWindow w;
    w.setWindowTitle("LevelMap v0.1");
    QIcon icon(":/res/map.ico");  // 图标文件的资源路径
    w.setWindowIcon(icon);        // 设置窗口图标
    w.show();
    return a.exec();
}
