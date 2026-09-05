#include <qvector.h>

#include <QApplication>
#include <QCache>
#include <QCoreApplication>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QImage>
#include <QLocale>
#include <QSurfaceFormat>
#include <QTextStream>
#include <Qchar>
#include <Qtranslator>
#include <chrono>
#include <filesystem>
#include <string>

#include "config.h"
#include "crashhandler.h"
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
    if (setting::current().COLOR_THEME == "dark") {
        hints->setColorScheme(Qt::ColorScheme::Dark);
    } else if (setting::current().COLOR_THEME == "light") {
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
    auto sz = setting::current().FONT_SIZE > 0 ? setting::current().FONT_SIZE : 10;
    auto family = !setting::current().FONT_FAMILY.isEmpty() ? setting::current().FONT_FAMILY : "微软雅黑";
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setPointSize(sz);
    font.setFamily(family);
    QApplication::setFont(font);
}

// empty setting -> auto-detect from the system locale (Chinese -> zh_CN, otherwise en)
QString resolveLanguage() {
    auto s = setting::current();
    if (!s.LANGUAGE.isEmpty()) return s.LANGUAGE;
    s.LANGUAGE = QLocale::system().language() == QLocale::Chinese ? QString("zh_CN") : QString("en");
    setting::apply(s);
    return s.LANGUAGE;
}

int main(int argc, char *argv[]) {
    setupLog(argc, argv);
    LOG_F(INFO, "Start %s", constant::VERSION_STRING().toStdString().c_str());
    crashhandler::install();

    // QOpenGLWidget is a native child window on Windows. The first time it is shown
    // inside an already-visible top-level window whose pixel format does not match,
    // Qt has to destroy and recreate the top-level HWND — visible as the main window
    // closing and reopening. Pre-set the default surface format (matching VoxelWidget)
    // so every top-level window is created with the right format from the start.
    QSurfaceFormat gl_format;
    gl_format.setVersion(3, 3);
    gl_format.setProfile(QSurfaceFormat::CoreProfile);
    gl_format.setDepthBufferSize(24);
    gl_format.setStencilBufferSize(8);
    gl_format.setSamples(8);
    QSurfaceFormat::setDefaultFormat(gl_format);

    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication a(argc, argv);
    setting::init();
    constant::initColorTable();
    initResources();
    setupTheme(a);
    setupFont(a);
    TranslatorMgr::init();
    TranslatorMgr::setupTranslation(a, resolveLanguage());
    MainWindow w;
    w.setWindowTitle(constant::VERSION_STRING());
    w.show();
    return QApplication::exec();
}
