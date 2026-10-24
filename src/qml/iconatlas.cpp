#include "iconatlas.h"

#include <QPainter>
#include <QRect>
#include <QSet>
#include <QVector>
#include <algorithm>

namespace {
    constexpr int atlas_padding = 2;

    struct Placement {
        const QImage* image{nullptr};
        QRect rect;
    };

    bool packIcons(const std::vector<const QImage*>& images, int side, QVector<Placement>& placements) {
        placements.clear();
        int x = atlas_padding;
        int y = atlas_padding;
        int shelf_height = 0;
        for (const QImage* image : images) {
            if (!image || image->isNull()) continue;
            const int w = image->width();
            const int h = image->height();
            if (w + atlas_padding * 2 > side || h + atlas_padding * 2 > side) return false;
            if (x + w + atlas_padding > side) {
                x = atlas_padding;
                y += shelf_height + atlas_padding;
                shelf_height = 0;
            }
            if (y + h + atlas_padding > side) return false;
            placements.push_back({image, QRect(x, y, w, h)});
            x += w + atlas_padding;
            shelf_height = std::max(shelf_height, h);
        }
        return true;
    }
}  // namespace

void IconAtlas::build(const std::vector<const QImage*>& images) {
    image_ = {};
    entries_.clear();

    std::vector<const QImage*> unique;
    QSet<const QImage*> seen;
    unique.reserve(images.size());
    for (const QImage* image : images) {
        if (!image || image->isNull() || seen.contains(image)) continue;
        seen.insert(image);
        unique.push_back(image);
    }
    std::sort(unique.begin(), unique.end(), [](const QImage* a, const QImage* b) {
        if (a->height() != b->height()) return a->height() > b->height();
        return a->width() > b->width();
    });

    QVector<Placement> placements;
    int side = 0;
    for (const int candidate : {512, 1024, 2048, 4096}) {
        if (packIcons(unique, candidate, placements)) {
            side = candidate;
            break;
        }
    }
    if (!side || placements.isEmpty()) return;

    image_ = QImage(side, side, QImage::Format_RGBA8888);
    image_.fill(Qt::transparent);
    QPainter painter(&image_);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (const auto& placement : placements) {
        painter.drawImage(placement.rect.topLeft(), placement.image->convertToFormat(QImage::Format_RGBA8888));
        entries_.insert(placement.image,
                        IconAtlasEntry{
                            QRectF(static_cast<qreal>(placement.rect.x()) / side, static_cast<qreal>(placement.rect.y()) / side,
                                   static_cast<qreal>(placement.rect.width()) / side, static_cast<qreal>(placement.rect.height()) / side),
                            QSizeF(placement.rect.size())});
    }
}

const IconAtlasEntry* IconAtlas::entry(const QImage* image) const {
    const auto it = entries_.constFind(image);
    return it == entries_.cend() ? nullptr : &it.value();
}
