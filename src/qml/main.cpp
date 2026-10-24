#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include "base/config.h"
#include "base/platform.h"
#include "base/theme.h"
#include "controller/appcontroller.h"
#include "controller/chunkeditorcontroller.h"
#include "controller/mcstructurecontroller.h"
#include "controller/globaldatacontroller.h"
#include "controller/nbtdocumentcontroller.h"
#include "controller/settingsviewmodel.h"
#include "loguru/loguru.hpp"
#include "mapoverlayquickitem.h"
#include "model/logmodel.h"
#include "model/nbttreemodel.h"
#include "model/notifications.h"
#include "model/worldlistmodel.h"
#include "navigation.h"
#include "render/resourcemanager.h"
#include "terrainquickitem.h"
#include "voxelquickitem.h"

int main(int argc, char* argv[]) {
    platform::installCrashHandler();
    if (!qEnvironmentVariableIsSet("QSG_RHI_PROFILE")) qputenv("QSG_RHI_PROFILE", "1");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/BedrockMap.svg")));
    QCoreApplication::setOrganizationName(QStringLiteral("BedrockMap"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("bedrock-dev.github.io"));
    QCoreApplication::setApplicationName(QString::fromUtf8(constant::SOFTWARE_NAME));
    QCoreApplication::setApplicationVersion(constant::softwareVersion().toString().mid(1));
    // Keep logging alive until all models, sessions and the QML engine exit.
    LogModel logs(argc, argv);
    LOG_F(INFO, "BedrockMap %s | Qt %s | Controls style %s", BEDROCKMAP_VERSION, qVersion(),
          qgetenv("QT_QUICK_CONTROLS_STYLE").constData());
    setting::init();
    if (!qEnvironmentVariableIsSet("QSG_RHI_BACKEND")) {
        QString backend = setting::current().renderBackend;
#if defined(Q_OS_ANDROID)
        // "Automatic" selects the stable backend for this platform. Vulkan is
        // available as an explicit override because some Android drivers fail
        // to create the surface used by QQuickRhiItem.
        if (backend == QStringLiteral("automatic") || backend == QStringLiteral("d3d11"))
            backend = QStringLiteral("opengl");
#elif !defined(Q_OS_WIN)
        if (backend == QStringLiteral("d3d11")) backend = QStringLiteral("automatic");
#endif
        if (backend == QStringLiteral("opengl") || backend == QStringLiteral("vulkan")
            || backend == QStringLiteral("d3d11")) {
            qputenv("QSG_RHI_BACKEND", backend.toLatin1());
        }
        LOG_F(INFO, "Qt Quick render backend setting: %s", backend.toStdString().c_str());
    } else {
        LOG_F(INFO, "Qt Quick render backend environment override: %s", qgetenv("QSG_RHI_BACKEND").constData());
    }
    constant::initColorTable();
    // Initialize entity icons before any world scan can resolve actors.
    initResources();
    setting::setupUiFont();
    // This executable has its own resource bundle, so it must load the shared
    // translation catalogs before the QML engine builds any user-visible text.
    TranslatorMgr::init();
    TranslatorMgr::setupTranslation(app, TranslatorMgr::resolveLanguage());

    WorldListModel worldModel;
    // One message centre for the whole front end: pages, the worlds workspace
    // and the core's own reporting all post into the same queue.
    // Declared before AppController so it outlives the sessions that report to it.
    NotificationCenter notifications;
    AppController appController;
    appController.setNotificationCenter(&notifications);
    appController.setWorldListModel(&worldModel);
    // Construct the settings facade after language resolution so it starts with
    // the effective auto-detected language rather than the empty sentinel.
    SettingsViewModel settingsModel;
    Navigation navigation;
    // Let Qt's platform palette follow the same explicit/system theme mode that
    // the QML Theme object uses. Changing back to "system" unsets the hint, so
    // the platform light/dark scheme becomes visible again.
    applyThemeHints(app.styleHints(), settingsModel.colorTheme());
    QObject::connect(&settingsModel, &SettingsViewModel::colorThemeChanged, &app,
                     [&app, &settingsModel] { applyThemeHints(app.styleHints(), settingsModel.colorTheme()); });

    // The QML engine owns the window and every page instance, so it must be
    // destroyed before the models and controllers its bindings reference.
    QQmlApplicationEngine engine;
    qmlRegisterType<TerrainQuickItem>("BedrockMap", 1, 0, "TerrainQuickItem");
    qmlRegisterType<MapOverlayQuickItem>("BedrockMap", 1, 0, "MapOverlayQuickItem");
    qmlRegisterUncreatableType<RegionImportController>("BedrockMap", 1, 0, "RegionImportController",
                                                      QStringLiteral("Owned by the map controller"));
    qmlRegisterUncreatableType<VoxelSceneController>("BedrockMap", 1, 0, "VoxelSceneController",
                                                    QStringLiteral("Provided by a preview session"));
    qmlRegisterUncreatableType<McstructureController>("BedrockMap", 1, 0, "McstructureController",
                                                    QStringLiteral("Provided by McstructureSession"));
    qmlRegisterType<VoxelQuickItem>("BedrockMap", 1, 0, "VoxelQuickItem");
    qmlRegisterUncreatableType<ChunkEditorController>("BedrockMap", 1, 0, "ChunkEditorController",
                                                      QStringLiteral("Provided by WorldSession"));
    qmlRegisterType<NbtDocumentController>("BedrockMap", 1, 0, "NbtDocumentController");
    qmlRegisterType<NbtTreeModel>("BedrockMap", 1, 0, "NbtTreeModel");
    qmlRegisterUncreatableType<GlobalDataController>("BedrockMap", 1, 0, "GlobalDataController",
                                                     QStringLiteral("Provided by WorldSession"));
    qmlRegisterUncreatableType<GlobalNbtSection>("BedrockMap", 1, 0, "GlobalNbtSection",
                                                 QStringLiteral("Created by GlobalDataController"));
    qmlRegisterSingletonInstance("BedrockMap", 1, 0, "App", &appController);
    qmlRegisterSingletonInstance("BedrockMap", 1, 0, "Settings", &settingsModel);
    qmlRegisterSingletonInstance("BedrockMap", 1, 0, "Worlds", &worldModel);
    qmlRegisterSingletonInstance("BedrockMap", 1, 0, "Notifications", &notifications);
    qmlRegisterSingletonInstance("BedrockMap", 1, 0, "Logs", &logs);
    qmlRegisterSingletonInstance("BedrockMap", 1, 0, "Navigation", &navigation);
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml/Theme.qml")), "BedrockMap", 1, 0, "Theme");
    // The standalone QML target is also used outside Qt Creator, so make the
    // module lookup explicit for both the developer Qt install and deployment.
    engine.addImportPath(QStringLiteral(":/qt-project.org/imports"));
    engine.addImportPath(QStringLiteral("qrc:/qt-project.org/imports"));
    engine.addImportPath(QLibraryInfo::path(QLibraryInfo::QmlImportsPath));
    engine.addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));

    // Settings changes are previewed immediately. Installing a new translator
    // makes the QML engine re-evaluate every qsTr()/qsTranslate() binding.
    QObject::connect(&settingsModel, &SettingsViewModel::languageChanged, &engine,
                     [&app, &engine, &settingsModel] {
                         const QString configured = settingsModel.language();
                         const QString language =
                             configured.isEmpty() || configured == QStringLiteral("system")
                                 ? TranslatorMgr::systemLanguage()
                                 : configured;
                         TranslatorMgr::setupTranslation(app, language);
                         engine.retranslate();
                     });
    QObject::connect(&settingsModel, &SettingsViewModel::languageChanged, &navigation, &Navigation::retranslate);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;

    // Development hook: open a world straight away so the map can be inspected
    // (or driven by a script) without walking the world list first.
    const QString startupWorld = qEnvironmentVariable("BEDROCKMAP_OPEN_WORLD");
    if (!startupWorld.isEmpty()) appController.openWorld(startupWorld, QString());
    // Development hook: land on a specific app page (0 worlds, 1 open worlds,
    // 2 tools, 3 settings, 4 logs) for inspection.
    const QString startupPage = qEnvironmentVariable("BEDROCKMAP_START_PAGE");
    if (!startupPage.isEmpty()) appController.setCurrentPage(startupPage.toInt());

    return app.exec();
}
