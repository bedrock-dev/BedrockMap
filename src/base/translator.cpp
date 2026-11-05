#include "base/translator.h"

#include <QCoreApplication>
#include <QLocale>

#include "base/config.h"
#include "loguru/loguru.hpp"

void TranslatorManager::initialize(QCoreApplication& application) {
    if (translations_.isEmpty()) {
        for (const auto& language : {QStringLiteral("zh_CN"), QStringLiteral("en")}) {
            auto translator = std::make_shared<QTranslator>();
            const QString path = QStringLiteral(":/bedrockmap/i18n/") + language + QStringLiteral(".qm");
            if (translator->load(path)) {
                LOG_F(INFO, "Load translation resource %s succeed", path.toStdString().c_str());
                translations_[language] = std::move(translator);
            } else {
                LOG_F(WARNING, "Load translation resource %s failed", path.toStdString().c_str());
            }
        }
    }
    setupTranslation(application, resolveLanguage());
}

QString TranslatorManager::systemLanguage() const {
    return QLocale::system().language() == QLocale::Chinese ? QStringLiteral("zh_CN") : QStringLiteral("en");
}

QString TranslatorManager::resolveLanguage() const {
    const QString configured = setting::current().language;
    if (!configured.isEmpty() && configured != QStringLiteral("system")) return configured;
    const QString detected = systemLanguage();
    setting::setResolvedLanguage(detected);
    return detected;
}

void TranslatorManager::setupTranslation(QCoreApplication& application, const QString& language) {
    for (const auto& translator : translations_) application.removeTranslator(translator.get());
    const auto translator = translations_.find(language);
    if (translator == translations_.end()) {
        LOG_F(WARNING, "No translation found for language %s", language.toStdString().c_str());
        return;
    }
    application.installTranslator(translator.value().get());
    LOG_F(INFO, "Install translation for language %s succeed", language.toStdString().c_str());
}
