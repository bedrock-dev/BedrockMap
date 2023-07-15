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
#include "config.h"


void setupTheme(QApplication &a) {

    if (cfg::COLOR_THEME != "dark" && cfg::COLOR_THEME != "light")return;
    auto theme_path = ":/dark/stylesheet.qss";
    QFile f(theme_path);
    if (!f.exists()) {
        qWarning("Unable to set stylesheet, file not found");
    } else {
        f.open(QFile::ReadOnly | QFile::Text);
        QTextStream ts(&f);
        a.setStyleSheet(ts.readAll());
    }
}

int main(int argc, char *argv[]) {
    InitIcons();
    cfg::initConfig();
    cfg::initColorTable();
    QApplication a(argc, argv);
    setupTheme(a);
    QFont font;
    font.setFamily("微软雅黑");
    font.setPointSize(10);
    a.setFont(font);
    MainWindow w;
    w.setWindowTitle("LevelMap v0.1");
    QIcon icon(":/res/icon.png");  // 图标文件的资源路径
    w.setWindowIcon(icon);        // 设置窗口图标
    w.show();
    return a.exec();
}
