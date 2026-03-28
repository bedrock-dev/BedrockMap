#include <qapplication.h>

#include <QApplication>
#include <QCache>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopWidget>
#include <QFontDatabase>
#include <QIcon>
#include <QImage>
#include <QTextCodec>
#include <chrono>
#include <filesystem>

#include "config.h"
#include "mainwindow.h"
#include "progressbarwidget.h"
#include "resourcemanager.h"

QString LOG_FILE_NAME;

void setupLog() {
    namespace fs = std::filesystem;
    if (!fs::exists("./logs")) {
        fs::create_directory("./logs");
    }
    const auto p1 = std::chrono::system_clock::now();
    LOG_FILE_NAME = "./logs/" + QString::number(std::chrono::duration_cast<std::chrono::seconds>(p1.time_since_epoch()).count()) + ".log";
}

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
        case QtInfoMsg:
            txt = QString("Info: %1").arg(msg);
            break;
    }
    QFile outFile(LOG_FILE_NAME);
    outFile.open(QIODevice::WriteOnly | QIODevice::Append);
    QTextStream ts(&outFile);
    ts << txt << endl;
    ts.flush();
}

void setupFont(QApplication &a) {
    auto id = QFontDatabase::addApplicationFont(":/res/fonts/JetBrainsMono-Regular.ttf");
    if (id == -1) {
        qWarning() << "Can not load font";
    }

    QFont font;
    auto sz = cfg::FONT_SIZE > 0 ? cfg::FONT_SIZE : 10;
    auto family = !cfg::FONT_FAMILY.isEmpty() ? cfg::FONT_FAMILY : "微软雅黑";
    font.setPointSize(sz);
    font.setFamily(family);
    QApplication::setFont(font);
}

int main(int argc, char *argv[]) {
#ifndef QT_DEBUG
    qInstallMessageHandler(myMessageHandler);
#endif
    cfg::initConfig();
    initResources();
    QApplication a(argc, argv);
    setupTheme(a);
    setupFont(a);
    if (!cfg::OPEN_NBT_EDITOR_ONLY) {
        MainWindow w;
        w.setWindowTitle(cfg::VERSION_STRING());
        w.show();
        return QApplication::exec();
    } else {
        a.setAttribute(Qt::AA_DontCreateNativeWidgetSiblings, true);
        auto *w = new NbtWidget();
        const int ext = 100;
        w->setWindowTitle("NBT Editor");
        auto const rec = QApplication::desktop()->screenGeometry();
        auto const height = static_cast<int>(rec.height() * 0.6);
        auto const width = static_cast<int>(rec.width() * 0.6);
        w->setGeometry({(rec.width() - width) / 2, (rec.height() - height) / 2, width, height});
        w->show();
        return QApplication::exec();
    };
}