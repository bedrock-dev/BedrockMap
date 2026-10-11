#include <QDir>
#include <QTest>

#include "apppaths.h"

class AppPathsTest final : public QObject {
    Q_OBJECT

   private slots:
    void pathsAreStableAndSeparated() {
        app_paths::initialize();
        QVERIFY(!app_paths::configDirectory().isEmpty());
        QVERIFY(!app_paths::cacheDirectory().isEmpty());
        QVERIFY(!app_paths::importExportDirectory().isEmpty());
        QVERIFY(app_paths::configFile().endsWith(QStringLiteral("/config.ini")) ||
                app_paths::configFile().endsWith(QStringLiteral("\\config.ini")));
        QVERIFY(app_paths::recentFile().endsWith(QStringLiteral("/recent.ini")) ||
                app_paths::recentFile().endsWith(QStringLiteral("\\recent.ini")));
        QVERIFY(QDir::cleanPath(app_paths::configDirectory()) != QDir::cleanPath(app_paths::cacheDirectory()));
        QVERIFY(QDir::cleanPath(app_paths::configDirectory()) != QDir::cleanPath(app_paths::importExportDirectory()));
        QVERIFY(QDir::cleanPath(app_paths::cacheDirectory()) != QDir::cleanPath(app_paths::importExportDirectory()));
    }
};

QTEST_MAIN(AppPathsTest)
#include "apppaths_test.moc"
