//
// Created by xhy on 2023/7/8.
//

#ifndef BEDROCKMAP_RESOURCEMANAGER_H
#define BEDROCKMAP_RESOURCEMANAGER_H

#include <QImage>

#include "bedrock_key.h"
#include "palette.h"

void initResources();

QImage *ActorImage(const QString &key);

QImage *OtherNBTIcon();

QImage *PlayerNBTIcon();

QImage *TagIcon(bl::palette::tag_type t);

QImage *VillageNBTIcon(bl::village_key::key_type t);

QImage *BlockActorNBTIcon(const QString &key);

QImage *EntityNBTIcon(const QString &key);

// 下面是新的API

struct IconManager {
    IconManager() = delete;

    static void init();
};

#endif  // BEDROCKMAP_RESOURCEMANAGER_H
