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
#include  <QTextCodec>

void setupTheme(QApplication &a) {
//    auto theme_path = ":/light/stylesheet.qss";
//    QFile f(theme_path);
//    if (!f.exists()) {
//        qWarning("Unable to set stylesheet, file not found");
//    } else {
//        f.open(QFile::ReadOnly | QFile::Text);
//        QTextStream ts(&f);
////        a.setStyleSheet(ts.readAll());
//    }
}

void myMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    QString txt;
    switch (type) {
        case QtDebugMsg:
            txt = QString("Debug: %1").arg(msg);
            break;
        case QtWarningMsg:
            txt = QString("Warning: %1").arg(msg);
            break;
        case QtCriticalMsg:
            txt = QString("Critical: %1").arg(msg);
            break;
        case QtFatalMsg:
            txt = QString("Fatal: %1").arg(msg);
            abort();
    }
    QFile outFile("log");
    outFile.open(QIODevice::WriteOnly | QIODevice::Append);
    QTextStream ts(&outFile);
    ts << txt << endl;
}

void setupFont(QApplication &a) {
    auto id = QFontDatabase::addApplicationFont(":/res/fonts/JetBrainsMono-Regular.ttf");
    if (id == -1) {
        qWarning() << "Can not load font";
    }

    QFont font;
    font.setPointSize(cfg::FONT_SIZE);
    font.setFamily("微软雅黑");
    a.setFont(font);
}

int main(int argc, char *argv[]) {
    qInstallMessageHandler(myMessageHandler);
    InitIcons();
    cfg::initConfig();
    cfg::initColorTable();
    QApplication a(argc, argv);
    setupTheme(a);
    setupFont(a);
    MainWindow w;
    w.setWindowTitle(cfg::VERSION_STRING());
    w.show();
    return a.exec();
}
