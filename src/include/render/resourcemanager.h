//
// Created by xhy on 2023/7/8.
//

#ifndef BEDROCKMAP_RESOURCEMANAGER_H
#define BEDROCKMAP_RESOURCEMANAGER_H

#include <qmap.h>
#include <qtranslator.h>

#include <QImage>
#include <memory>
#include <vector>

class QCoreApplication;

void initResources();

QImage* ActorImage(const QString& key);

/// Entity images and the unknown fallback, packed by the sprite overlay.
std::vector<QImage*> MarkerIcons();

class TranslatorMgr {
   public:
    TranslatorMgr() = delete;

    static void init();

    static void setupTranslation(QCoreApplication& a, const QString& langName);

    /// Resolve the configured UI language. An empty (or "system") setting falls
    /// back to the system locale (Chinese -> zh_CN, otherwise en) and publishes
    /// the detected value through setting::apply().
    static QString resolveLanguage();

    /// System locale fallback without touching the stored settings. This is used
    /// while previewing settings changes that have not been saved yet.
    static QString systemLanguage();

   private:
    static QMap<QString, std::shared_ptr<QTranslator>>& translations();
};

#endif  // BEDROCKMAP_RESOURCEMANAGER_H
