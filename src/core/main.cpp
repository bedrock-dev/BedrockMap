#include <qvector.h>

#include <QApplication>
#include <QCache>
#include <QCoreApplication>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QImage>
#include <QTextStream>
#include <Qapplication>
#include <Qchar>
#include <Qtranslator>
#include <chrono>
#include <filesystem>
#include <string>

#include "config.h"
#include "loguru/loguru.hpp"
#include "mainwindow.h"
#include "resourcemanager.h"

void setupLog(int argc, char *argv[]) {
    namespace fs = std::filesystem;
    if (!fs::exists("./logs")) fs::create_directory("./logs");

    const auto p1 = std::chrono::system_clock::now();
    auto log_file = "./logs/" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(p1.time_since_epoch()).count()) + ".log";
    loguru::g_preamble_date = false;
    loguru::g_preamble_thread = false;
    loguru::g_colorlogtostderr = true;
    loguru::init(argc, argv);
    loguru::add_file(log_file.c_str(), loguru::Truncate, loguru::Verbosity_INFO);
}

void setupTheme(QApplication &a) {
    // 强制 Qt 重新读取 Windows 系统主题色
    a.setStyle(QApplication::style()->objectName());
}

void setupFont(QApplication &a) {
    auto id = QFontDatabase::addApplicationFont(":/res/fonts/JetBrainsMono-Regular.ttf");
    if (id == -1) {
        LOG_F(WARNING, "Can not load font");
    }
    QFont font;
    auto sz = cfg::FONT_SIZE > 0 ? cfg::FONT_SIZE : 10;
    auto family = !cfg::FONT_FAMILY.isEmpty() ? cfg::FONT_FAMILY : "微软雅黑";
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setPointSize(sz);
    font.setFamily(family);
    QApplication::setFont(font);
}

int main(int argc, char *argv[]) {
    setupLog(argc, argv);
    LOG_F(INFO, "Start %s", cfg::VERSION_STRING().toStdString().c_str());
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication a(argc, argv);
    cfg::initConfig();
    initResources();
    setupTheme(a);
    setupFont(a);
    TranslatorMgr::init();
    TranslatorMgr::setupTranslation(a, cfg::LANGUAGE);
    QTabWidget *w;
    if (!cfg::OPEN_NBT_EDITOR_ONLY) {
        MainWindow w;
        w.setWindowTitle(cfg::VERSION_STRING());
        w.show();
        return QApplication::exec();
    } else {
        //    a.setAttribute(Qt::AA_DontCreateNativeWidgetSiblings, true);
        auto *w = new NbtWidget();
        const int ext = 100;
        w->setWindowTitle(QObject::tr("nbtEditor.title.nbtEditor"));
        auto const rec = QApplication::primaryScreen()->geometry();
        auto const height = static_cast<int>(rec.height() * 0.6);
        auto const width = static_cast<int>(rec.width() * 0.6);
        w->setGeometry({(rec.width() - width) / 2, (rec.height() - height) / 2, width, height});
        w->show();
        return QApplication::exec();
    };
}