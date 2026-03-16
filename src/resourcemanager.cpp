//
// Created by xhy on 2023/7/8.
//

#include "resourcemanager.h"

#include <qcolor.h>
#include <qicon.h>
#include <qimage.h>
#include <qpixmap.h>
#include <qrgb.h>

#include <QDir>
#include <QDirIterator>
#include <QIcon>
#include <QMap>
#include <QString>
#include <QtDebug>
#include <iostream>
#include <unordered_map>

#include "bedrock_key.h"
#include "config.h"

namespace {
    QMap<QString, QImage *> actor_img_pool;
    QMap<QString, QImage *> block_actor_icon_pool;
    QMap<QString, QImage *> tag_icon_pool;
    QMap<QString, QImage *> entity_icon_pool;

    QImage *unknown_img;

    // villages
    QImage *village_players_nbt;
    QImage *village_poi_nbt;
    QImage *village_info_nbt;
    QImage *village_dwellers_nbt;
    QImage *other_nbt;
    QImage *player_nbt;

    std::unordered_map<bl::village_key::key_type, QIcon> &village_icon_pool() {
        static std::unordered_map<bl::village_key::key_type, QIcon> icons_;
        return icons_;
    }

    // add a border to the image in the transparent area, the args are the image and the border width, return a new image with the border
    QImage *addBorder(const QImage &img, int bw) {
        auto *masked = new QImage(img.width() + bw * 2, img.height() + bw * 2, QImage::Format_RGBA8888);
        masked->fill(Qt::transparent);  // fill with transparent color
        auto conv = [bw](int x, int z, QImage *mask, const QImage *origin) {
            int cx = x - bw;
            int cz = z - bw;
            if (cx >= 0 && cx < origin->width() && cz >= 0 && cz < origin->height() && origin->pixelColor(cx, cz).alpha() != 0) {
                mask->setPixelColor(x, z, origin->pixelColor(cx, cz).rgba());
            } else {
                // if current pixel's neighbor pixel is the orignal image's pixel, then set the current pixel to semi-transparent black
                for (int i = -bw; i <= bw; ++i) {
                    for (int j = -bw; j <= bw; ++j) {
                        if (cx + i >= 0 && cx + i < origin->width() && cz + j >= 0 && cz + j < origin->height() &&
                            origin->pixelColor(cx + i, cz + j).alpha() != 0) {
                            mask->setPixelColor(x, z, QColor(cfg::ACTOR_BORDER_COLOR));
                            return;
                        }
                    }
                }
            }
        };
        // traverse the image and add a border
        for (int i = 0; i < masked->width(); i++) {
            for (int j = 0; j < masked->height(); j++) {
                conv(i, j, masked, &img);
            }
        }

        return masked;
    }

    QImage *scale2(const QImage &img) {
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
}  // namespace

void initResources() {
    QDirIterator it(":/res/entity", QDirIterator::Subdirectories);
    while (it.hasNext()) {
        auto img = QImage(it.next());
        auto key = it.fileName().replace(".png", "");
        auto masked = addBorder(img, cfg::ACTOR_BORDER_WIDTH);
        entity_icon_pool[key] = masked;
        if (masked) {
            actor_img_pool[key] = masked;
        }
    }

    village_dwellers_nbt = scale2(QImage(":/res/village/dwellers.png"));
    village_players_nbt = scale2(QImage(":/res/village/players.png"));
    village_info_nbt = scale2(QImage(":/res/village/info.png"));
    village_poi_nbt = scale2(QImage(":/res/village/poi.png"));
    player_nbt = scale2(QImage(":/res/village/players.png"));
    other_nbt = scale2(QImage(":/res/village/info.png"));

    unknown_img = new QImage(":/res/what.png");

    QDirIterator it2(":/res/block_actor", QDirIterator::Subdirectories);
    while (it2.hasNext()) {
        auto img = QImage(it2.next());
        auto key = it2.fileName().replace(".png", "");
        block_actor_icon_pool[key] = scale2(img);
    }

    QDirIterator it3(":/res/nbt/", QDirIterator::Subdirectories);
    while (it3.hasNext()) {
        auto img = QImage(it3.next());
        auto key = it3.fileName().replace(".png", "").replace("TAG_", "");
        //        qDebug() << "NBT Icon: " << key;
        tag_icon_pool[key] = scale2(img);
    }
}

void IconManager::init() {}

QImage *OtherNBTIcon() { return other_nbt; }

QImage *BlockActorNBTIcon(const QString &key) {
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

QImage *VillageNBTIcon(bl::village_key::key_type t) {
    switch (t) {
        case bl::village_key::INFO:
            return village_info_nbt;
        case bl::village_key::DWELLERS:
            return village_dwellers_nbt;
        case bl::village_key::PLAYERS:
            return village_players_nbt;
        case bl::village_key::POI:
            return village_poi_nbt;
        case bl::village_key::Unknown:
            return unknown_img;
    }
    return unknown_img;
}

QImage *EntityNBTIcon(const QString &key) {
    auto it = entity_icon_pool.find(key);
    if (it == entity_icon_pool.end()) {
        qDebug() << " unknown key " << key;
    }
    return it == entity_icon_pool.end() ? unknown_img : it.value();
}

QImage *PlayerNBTIcon() { return player_nbt; }

QImage *TagIcon(bl::palette::tag_type t) {
    using namespace bl::palette;
    std::unordered_map<tag_type, std::string> names{
        {tag_type::Int, "Int"},
        {tag_type::Byte, "Byte"},
        {tag_type::Compound, "Compound"},
        {tag_type::Double, "Double"},
        {tag_type::Float, "Float"},
        {tag_type::List, "List"},
        {tag_type::Long, "Long"},
        {tag_type::Short, "Short"},
        {tag_type::String, "String"},
        {tag_type::ByteArray, "Byte_Array"},
        {tag_type::IntArray, "Int_Array"},
        {tag_type::LongArray, "Long_Array"},
        {tag_type::End, "End"},
    };

    auto it = tag_icon_pool.find(QString(names[t].c_str()));
    return it == tag_icon_pool.end() ? unknown_img : it.value();
}
