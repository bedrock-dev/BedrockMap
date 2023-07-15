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
    QMap<QString, QImage *> entity_icon_pool;
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

        auto *masked = new QImage(18, 18, QImage::Format_RGBA8888);
        auto conv = [](int x, int z, QImage *mask, const QImage *origin) {
            int cx = x - 1;
            int cz = z - 1;


            if (cx >= 0 && cx < 16 && cz >= 0 && cz < 16 && origin->pixelColor(cx, cz).alpha() != 0) {
                mask->setPixelColor(x, z, origin->pixelColor(cx, cz).rgba());
            } else {
                int alpha{0};
                for (int i = -1; i <= 1; i++) {
                    for (int j = -1; j <= 1; j++) {
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
        for (int i = 0; i < 18; i++) {
            for (int j = 0; j < 18; j++) {
                conv(i, j, masked, &img);
            }
        }

        return masked;
    }

    QImage *scale(const QImage &img) {
        auto *res = new QImage(img.width() * 2, img.height() * 2, QImage::Format_RGBA8888);
        for (int i = 0; i < img.width(); i++) {
            for (int j = 0; j < img.height(); j++) {
                auto c = img.pixelColor(i, j);
                res->setPixelColor(i * 2, j * 2, c);
                res->setPixelColor(i * 2 + 1, j * 2, c);
                res->setPixelColor(i * 2, j * 2 + 1, c);
                res->setPixelColor(i * 2 + 1, j * 2 + 1, c);
            }
        }
        return res;
    }  // namespace
}

void InitIcons() {
    QDirIterator it(":/res/entity", QDirIterator::Subdirectories);
    while (it.hasNext()) {
        auto img = QImage(it.next());
        auto key = it.fileName().replace(".png", "");
        auto masked = addMask(img);
        auto scaled = scale(img);
        entity_icon_pool[key] = scaled;
        if (masked) {
            actor_img_pool[key] = masked;
        }
    }

    village_dwellers = scale(QImage(":/res/village/dwellers.png"));
    village_players = scale(QImage(":/res/village/players.png"));
    village_info = scale(QImage(":/res/village/info.png"));
    village_poi = scale(QImage(":/res/village/poi.png"));
    unknown_img = new QImage(":/res/what.png");
    QDirIterator it2(":/res/block_actor", QDirIterator::Subdirectories);
    while (it2.hasNext()) {
        auto img = QImage(it2.next());
        auto key = it2.fileName().replace(".png", "");
        block_actor_icon_pool[key] = scale(img);
    }
}

QImage *PlayerIcon() { return nullptr; }

QImage *BlockActorIcon(const QString &key) {
    auto it = block_actor_icon_pool.find(key);
    if (it == block_actor_icon_pool.end()) {
        qDebug() << " unknown block actor key " << key;
    }
    return it == block_actor_icon_pool.end() ? unknown_img : it.value();
}

QImage *ActorImage(const QString &key) {
    auto it = actor_img_pool.find(key);
    if (it == actor_img_pool.end()) {
        qDebug() << " unknown actor image key " << key;
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

QImage *EntityIcon(const QString &key) {
    auto it = entity_icon_pool.find(key);
    if (it == entity_icon_pool.end()) {
        qDebug() << " unknown key " << key;
    }
    return it == entity_icon_pool.end() ? unknown_img : it.value();
}
