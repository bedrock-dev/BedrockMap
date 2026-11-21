#include "navigation.h"

Navigation::Navigation(QObject* parent) : QObject(parent) {}

QVariantList Navigation::pages() const {
    return {
        QVariantMap{{QStringLiteral("page"), Worlds},
                    {QStringLiteral("title"), tr("Welcome")},
                    {QStringLiteral("shortTitle"), tr("Welcome")},
                    {QStringLiteral("icon"), QStringLiteral("home")}},
        QVariantMap{{QStringLiteral("page"), Sessions},
                    {QStringLiteral("title"), tr("Open sessions")},
                    {QStringLiteral("shortTitle"), tr("Sessions")},
                    {QStringLiteral("icon"), QStringLiteral("sessions")}},
        QVariantMap{{QStringLiteral("page"), Tools},
                    {QStringLiteral("title"), tr("Tools")},
                    {QStringLiteral("shortTitle"), tr("Tools")},
                    {QStringLiteral("icon"), QStringLiteral("tools")}},
        QVariantMap{{QStringLiteral("page"), Settings},
                    {QStringLiteral("title"), tr("Settings")},
                    {QStringLiteral("shortTitle"), tr("Settings")},
                    {QStringLiteral("icon"), QStringLiteral("settings")}},
    };
}

void Navigation::retranslate() { emit pagesChanged(); }
