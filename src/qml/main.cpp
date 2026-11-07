#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStyleHints>
#include <memory>

#include "base/appcontext.h"
#include "base/config.h"
#include "base/platform.h"
#include "base/resourcemanager.h"
#include "base/translator.h"
#include "controller/appcontroller.h"
#include "fileopenhandler.h"
#include "loguru/loguru.hpp"
#include "model/logmodel.h"

namespace {
    // Unknown modes let the platform choose the effective color scheme.
    void applyThemeHints(QStyleHints* hints, const QString& colorTheme) {
        if (!hints) return;
        if (colorTheme == QStringLiteral("dark")) {
            hints->setColorScheme(Qt::ColorScheme::Dark);
        } else if (colorTheme == QStringLiteral("light")) {
            hints->setColorScheme(Qt::ColorScheme::Light);
        } else {
            hints->setColorScheme(Qt::ColorScheme::Unknown);
        }
    }

    void setupGraphicsBackend() {
        if (qEnvironmentVariableIsSet("QSG_RHI_BACKEND")) {
            LOG_F(INFO, "Qt Quick render backend environment override: %s", qgetenv("QSG_RHI_BACKEND").constData());
            return;
        }

        QString backend = setting::current().renderBackend;
#if defined(Q_OS_ANDROID)
        // "Automatic" selects the stable backend for this platform. Vulkan is
        // available as an explicit override because some Android drivers fail
        // to create the surface used by QQuickRhiItem.
        if (backend == QStringLiteral("automatic") || backend == QStringLiteral("d3d11")) backend = QStringLiteral("opengl");
#elif !defined(Q_OS_WIN)
        if (backend == QStringLiteral("d3d11")) backend = QStringLiteral("automatic");
#endif
        if (backend == QStringLiteral("opengl") || backend == QStringLiteral("vulkan") || backend == QStringLiteral("d3d11")) {
            qputenv("QSG_RHI_BACKEND", backend.toLatin1());
        }
        LOG_F(INFO, "Qt Quick render backend setting: %s", backend.toStdString().c_str());
    }
}  // namespace

int main(int argc, char* argv[]) {
    platform::installCrashHandler();
#if defined(BEDROCKMAP_ENABLE_RENDER_TIMINGS)
    if (!qEnvironmentVariableIsSet("QSG_RHI_PROFILE")) qputenv("QSG_RHI_PROFILE", "1");
#endif
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BedrockMap"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("bedrock-dev.github.io"));
    QCoreApplication::setApplicationName(QString::fromUtf8(constant::SOFTWARE_NAME));
    QCoreApplication::setApplicationVersion(
        QStringLiteral("%1+%2").arg(constant::softwareVersion().toString().mid(1), QString::fromUtf8(GIT_COMMIT_HASH)));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/bedrockmap/icons/app.svg")));
    FileOpenHandler fileOpenHandler(app);
    // Keep logging alive until all models, sessions and the QML engine exit.
    auto logs = std::make_unique<LogModel>(argc, argv);
    LOG_F(INFO, "BedrockMap %s | Qt %s | Controls style %s", BEDROCKMAP_VERSION, qVersion(),
          qgetenv("QT_QUICK_CONTROLS_STYLE").constData());
    setting::init();
    setupGraphicsBackend();
    constant::initColorTable();
    AppContext appContext;
    appContext.initialize(app);

    // Create application services after settings and language resolution.
    // Transfer the early log model so AppController owns its full lifecycle.
    AppController appController(std::move(logs), appContext);
    QObject::connect(&fileOpenHandler, &FileOpenHandler::fileOpenRequested, &appController, &AppController::openFile);
    auto& settingsModel = *appController.settings();
    // Let Qt's platform palette follow the same explicit/system theme mode that
    // the QML Theme object uses.
    applyThemeHints(app.styleHints(), settingsModel.colorTheme());
    QObject::connect(&settingsModel, &SettingsViewModel::colorThemeChanged, &app,
                     [&app, &settingsModel] { applyThemeHints(app.styleHints(), settingsModel.colorTheme()); });
    QObject::connect(&settingsModel, &SettingsViewModel::fontSizeChanged, &app,
                     [&appContext, &settingsModel] { appContext.fonts().applySize(settingsModel.fontSize()); });

    // The QML engine owns the window and every page instance, so it must be
    // destroyed before the models and controllers its bindings reference.
    QQmlApplicationEngine engine;
    // CMake also copies Qt modules next to the executable for local runs.
    engine.addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));

    QObject::connect(&settingsModel, &SettingsViewModel::languageChanged, &engine, [&app, &engine, &settingsModel, &appContext] {
        const QString configured = settingsModel.language();
        const QString language =
            configured.isEmpty() || configured == QStringLiteral("system") ? appContext.translator().systemLanguage() : configured;
        appContext.translator().setupTranslation(app, language);
        engine.retranslate();
    });
    engine.loadFromModule("BedrockMap", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
#ifdef Q_OS_ANDROID
    // Keep the native startup cover until Qt has submitted its first frame.
    if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst())) {
        QObject::connect(
            window, &QQuickWindow::frameSwapped, window, [] { QNativeInterface::QAndroidApplication::hideSplashScreen(); },
            Qt::SingleShotConnection);
    }
#endif
    fileOpenHandler.start();

    // Development hook: open a world straight away so the map can be inspected
    // (or driven by a script) without walking the world list first.
    const QString startupWorld = qEnvironmentVariable("BEDROCKMAP_OPEN_WORLD");
    if (!startupWorld.isEmpty()) appController.openWorld(startupWorld, QString());
    // Development hook: land on a specific app page (0 worlds, 1 open worlds,
    // 2 tools, 3 settings) for inspection.
    const QString startupPage = qEnvironmentVariable("BEDROCKMAP_START_PAGE");
    if (!startupPage.isEmpty()) appController.setCurrentPage(startupPage.toInt());

    return app.exec();
}
