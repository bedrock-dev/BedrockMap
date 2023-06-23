#include <QApplication>
#include <QDebug>
#include <QDesktopWidget>

#include "asynclevelloader.h"
#include "lrucache.h"
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

    //    AsyncLevelLoader loader;
    //    loader.init(R"(C:\Users\xhy\Desktop\t)");
    //    qDebug() << "Loader!!";
    //    for (int i = 0; i < 3; i++) {
    //        qDebug() << "========================Round==================";
    //        for (int j = 0; j < 5; j++) {
    //            bl::chunk_pos p{0, j, 0};
    //            auto *chunk = loader.get(p);
    //            qDebug() << "Chunk  Read status" << p.to_string().c_str() <<
    //            ": "
    //                     << (chunk ? 1 : 0);
    //        }
    //    }

    return 0;
}
