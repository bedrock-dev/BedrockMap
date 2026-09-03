//
// Created by xhy on 2023/7/8.
//

#ifndef BEDROCKMAP_RESOURCEMANAGER_H
#define BEDROCKMAP_RESOURCEMANAGER_H

#include <qchar.h>
#include <qcontainerfwd.h>
#include <qmap.h>
#include <qtranslator.h>

#include <QImage>
#include <memory>

#include "bedrock_key.h"
#include "nbt.h"

void initResources();

QImage *ActorImage(const QString &key);

QImage *OtherNBTIcon();

QImage *PlayerNBTIcon();

QImage *TagIcon(bl::nbt::tag_type t);

QImage *VillageNBTIcon(bl::village_key::key_type t);

QImage *BlockActorNBTIcon(const QString &key);

QImage *EntityNBTIcon(const QString &key);

QString ToolBarIcon(const QString &name);

struct IconManager {
    IconManager() = delete;

    static void init();
};

class TranslatorMgr {
   public:
    TranslatorMgr() = delete;

    static void init();

    static void setupTranslation(QApplication &a, const QString &langName);

   private:
    static QMap<QString, std::shared_ptr<QTranslator>> &translations();
};

#endif  // BEDROCKMAP_RESOURCEMANAGER_H
