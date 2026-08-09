#include <qvector.h>

#include <QApplication>
#include <QCache>
#include <QCoreApplication>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QImage>
#include <QLocale>
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
    auto *hints = a.styleHints();
    if (setting::COLOR_THEME == "dark") {
        hints->setColorScheme(Qt::ColorScheme::Dark);
    } else if (setting::COLOR_THEME == "light") {
        hints->setColorScheme(Qt::ColorScheme::Light);
    } else {
        hints->setColorScheme(Qt::ColorScheme::Unknown);
    }
}

void setupFont(QApplication &a) {
    auto id = QFontDatabase::addApplicationFont(":/res/fonts/JetBrainsMono-Regular.ttf");
    if (id == -1) {
        LOG_F(WARNING, "Can not load font");
    }
    QFont font;
    auto sz = setting::FONT_SIZE > 0 ? setting::FONT_SIZE : 10;
    auto family = !setting::FONT_FAMILY.isEmpty() ? setting::FONT_FAMILY : "微软雅黑";
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setPointSize(sz);
    font.setFamily(family);
    QApplication::setFont(font);
}

// empty setting -> auto-detect from the system locale (Chinese -> zh_CN, otherwise en)
QString resolveLanguage() {
    if (!setting::LANGUAGE.isEmpty()) return setting::LANGUAGE;
    setting::LANGUAGE = QLocale::system().language() == QLocale::Chinese ? QString("zh_CN") : QString("en");
    return setting::LANGUAGE;
}

int main(int argc, char *argv[]) {
    setupLog(argc, argv);
    LOG_F(INFO, "Start %s", constant::VERSION_STRING().toStdString().c_str());
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication a(argc, argv);
    setting::init();
    constant::initColorTable();
    initResources();
    setupTheme(a);
    setupFont(a);
    TranslatorMgr::init();
    TranslatorMgr::setupTranslation(a, resolveLanguage());
    QTabWidget *w;
    if (!setting::OPEN_NBT_EDITOR_ONLY) {
        MainWindow w;
        w.setWindowTitle(constant::VERSION_STRING());
        w.show();
        return QApplication::exec();
    } else {
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