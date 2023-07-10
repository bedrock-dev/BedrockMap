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

    QImage *addMask(const QImage &img) {
        if (img.size() != QSize(16, 16)) {
            return nullptr;
        }

        auto *masked = new QImage(20, 20, QImage::Format_RGBA8888);
        auto conv = [](int x, int z, QImage *mask, const QImage *origin) {
            int cx = x - 2;
            int cz = z - 2;


            if (cx >= 0 && cx < 16 && cz >= 0 && cz < 16 && origin->pixelColor(cx, cz).alpha() != 0) {
                mask->setPixelColor(x, z, origin->pixelColor(cx, cz).rgba());
            } else {
                int alpha{0};
                for (int i = -2; i <= 2; i++) {
                    for (int j = -2; j <= 2; j++) {
//                        范围内有像素
                        int nx = cx + i;
                        int nz = cz + j;
                        if (nx >= 0 && nx < 16 && nz >= 0 && nz < 16 && origin->pixelColor(nx, nz).alpha() != 0) {
                            alpha = 220;
                        }
                    }
                }
                mask->setPixelColor(x, z, QColor(255, 255, 255, alpha));
            }
        };
        for (int i = 0; i < 20; i++) {
            for (int j = 0; j < 20; j++) {
                conv(i, j, masked, &img);
            }
        }

        return masked;
    }

}


void InitIcons() {
    QDirIterator it(":/res/entity", QDirIterator::Subdirectories);
    while (it.hasNext()) {
        auto img = QImage(it.next());
        auto key = it.fileName().replace(".png", "");
        auto masked = addMask(img);
        if (masked) {
            actor_img_pool[key] = masked;
        }

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

