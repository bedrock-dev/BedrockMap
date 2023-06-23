#include <QApplication>
#include <QDesktopWidget>

#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    MainWindow w;
    QFont f = a.font();

    f.setFamily("微软雅黑");
    f.setPointSize(12);
    a.setFont(f);
    auto const rec = QApplication::desktop()->screenGeometry();
    auto const height = std::max(rec.height() / 2, 720);
    auto const width = std::max(rec.width() / 2, 1368);
    w.setGeometry((rec.width() - width) / 2, (rec.height() - height) / 2, width,
                  height);
    w.setWindowTitle("LevelMap v0.1");
    w.show();

    return a.exec();
}
