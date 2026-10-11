#include <QGuiApplication>
#include <QLibraryInfo>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>

#include "config.h"
#include "worldlistmodel.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    setting::init();
    constant::initColorTable();

    QQmlApplicationEngine engine;
    // The standalone QML target is also used outside Qt Creator, so make the
    // module lookup explicit for both the developer Qt install and deployment.
    engine.addImportPath(QStringLiteral(":/qt-project.org/imports"));
    engine.addImportPath(QStringLiteral("qrc:/qt-project.org/imports"));
    engine.addImportPath(QLibraryInfo::path(QLibraryInfo::QmlImportsPath));
    engine.addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));
    WorldListModel worldModel;
    engine.rootContext()->setContextProperty(QStringLiteral("worldModel"), &worldModel);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;

    return app.exec();
}
