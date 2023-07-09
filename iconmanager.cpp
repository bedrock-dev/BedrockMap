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


    //villages
    QImage *village_players;
    QImage *village_poi;
    QImage *village_info;
    QImage *village_dwellers;

}


void InitIcons() {
    QDirIterator it(":/res/entity", QDirIterator::Subdirectories);
    while (it.hasNext()) {
        auto *img = new QImage(it.next());
        auto key = it.fileName().replace(".png", "");
        actor_img_pool[key] = img;
    }

    //villages
    /*
     *    <file>res/village_dwellers.png</file>
        <file>res/village_info.png</file>
        <file>res/village_player.png</file>
        <file>res/village_poi.png</file>

     */
    village_dwellers = new QImage(":/res/village_dwellers.png");
    village_players = new QImage(":/res/village_player.png");
    village_info = new QImage(":/res/village_info.png");
    village_poi = new QImage(":/res/village_poi.png");
    unknown_img = new QImage(":/res/what.png");

    //    QDirIterator it2(":/res/block", QDirIterator::Subdirectories);
    //
    //    while (it.hasNext()) {
    //        auto *img = new QImage(it.next());
    //        auto key = it.fileName().replace(".png", "");
    //        qDebug() << key;
    //        actor_img_pool[key] = img;
    //    }
    //
    //    unknown_img = new QImage(":/res/what.png");
}

QImage *PlayerIcon() {
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

QImage *VillagerIcon(bl::village_key::key_type t) {
    switch (t) {
        case bl::village_key::INFO:
            return village_info;
        case bl::village_key::DWELLERS:
            return village_dwellers;
        case bl::village_key::PLAYERS:
            return village_players;
        case bl::village_key::POI:
            return village_poi;
        case bl::village_key::Unknown:
            return unknown_img;
    }
}

