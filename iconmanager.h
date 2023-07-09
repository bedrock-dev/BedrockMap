//
// Created by xhy on 2023/7/8.
//

#ifndef BEDROCKMAP_ICONMANAGER_H
#define BEDROCKMAP_ICONMANAGER_H

#include <QImage>

void InitIcons();

QImage *GetActorIcon(const std::string &key);

QImage* ActorImage(const QString &key);


QImage *PlayerIcon();

QImage *VillagerIcon();

QImage *PoiIcon();

QImage *BlockActorIcon(const std::string &key);


#endif //BEDROCKMAP_ICONMANAGER_H

