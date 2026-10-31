#include "qmltypes.h"

#include <QPointer>
#include <QQmlEngine>

AppController* AppQml::create(QQmlEngine* engine, QJSEngine*) {
    auto* app = AppController::instance();
    if (!app) qFatal("AppController must be created before loading QML");
    if (app->thread() != engine->thread()) qFatal("AppController must share the QML engine thread");
    static QPointer<QQmlEngine> activeEngine;
    if (activeEngine && activeEngine != engine) qFatal("AppController cannot be shared between active QML engines");
    activeEngine = engine;
    QJSEngine::setObjectOwnership(app, QJSEngine::CppOwnership);
    return app;
}
