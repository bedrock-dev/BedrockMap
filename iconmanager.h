//
// Created by xhy on 2023/7/8.
//

#ifndef BEDROCKMAP_ICONMANAGER_H
#define BEDROCKMAP_ICONMANAGER_H

#include "bedrock_key.h"
#include <QImage>

void InitIcons();

QImage *ActorImage(const QString &key);


QImage *OtherNBTIcon();

QImage *PlayerNBTIcon();


QImage *VillageNBTIcon(bl::village_key::key_type t);

QImage *BlockActorNBTIcon(const QString &key);

QImage *EntityNBTIcon(const QString &key);

#endif  // BEDROCKMAP_ICONMANAGER_H
