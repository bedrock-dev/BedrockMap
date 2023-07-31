#include <QApplication>
#include <QCache>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopWidget>
#include <QFontDatabase>
#include <QIcon>
#include <QImage>

#include "asynclevelloader.h"
#include "config.h"
#include "iconmanager.h"
#include "mainwindow.h"

void setupTheme(QApplication &a) {
    auto theme_path = ":/light/stylesheet.qss";
    QFile f(theme_path);
    if (!f.exists()) {
        qWarning("Unable to set stylesheet, file not found");
    } else {
        f.open(QFile::ReadOnly | QFile::Text);
        QTextStream ts(&f);
        a.setStyleSheet(ts.readAll());
    }
}

void setupFont(QApplication &a) {
//    int id = QFontDatabase::addApplicationFont(":/res/fonts/SourceHanSansCN-Normal.otf");
//    if (id == -1) {
//        qWarning() << "Can not load font";
//    }

    auto id = QFontDatabase::addApplicationFont(":/res/fonts/JetBrainsMono-Regular.ttf");
    if (id == -1) {
        qWarning() << "Can not load font";
    }

    QFontDatabase db;
    //  foreach (const QString &s, db.families()) { qDebug() << s; }
    QFont font;
    // font.setFamily("Source Han Sans CN Normal");
    font.setFamily("微软雅黑");
    //    font.setPointSize(8);
    a.setFont(font);
}


int main(int argc, char *argv[]) {
    InitIcons();
    cfg::initConfig();
    cfg::initColorTable();
    QApplication a(argc, argv);
    setupTheme(a);
    setupFont(a);

    MainWindow w;
    w.setWindowTitle("LevelMap v0.1");
    QIcon icon(":/res/icon.png");  // 图标文件的资源路径
    w.setWindowIcon(icon);        // 设置窗口图标
    w.show();
    return a.exec();
}
