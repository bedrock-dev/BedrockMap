//
// Created by xhy on 2023/7/8.
//

#include "iconmanager.h"
#include <unordered_map>
#include <QDir>
#include <QDirIterator>
#include <QtDebug>
#include <QString>
#include <QMap>

namespace {
    QMap<QString, QImage *> actor_img_pool;
    QMap<QString, QImage *> block_actor_icon_pool;
    QImage *unknown_img;
    QImage *player_icon;
    QImage *village_icon;

}


void InitIcons() {
    QDirIterator it(":/res/entity", QDirIterator::Subdirectories);
    while (it.hasNext()) {
        auto *img = new QImage(it.next());
        auto key = it.fileName().replace(".png", "");
        qDebug() << key;
        actor_img_pool[key] = img;
    }
    unknown_img = new QImage(":/res/entity/what.png");


}


QImage *PlayerIcon() {
    return nullptr;
}

QImage *VillagerIcon() {
    return nullptr;
}

QImage *BlockActorIcon(const std::string &key) {
    return nullptr;
}

QImage *ActorImage(const QString &key) {
    auto it = actor_img_pool.find(key);
    if (it == actor_img_pool.end()) {
        qDebug() << " unknown key " << key;
    }

    return it == actor_img_pool.end() ? unknown_img : it.value();
}

