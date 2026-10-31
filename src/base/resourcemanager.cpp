#include "base/resourcemanager.h"

#include <QColor>
#include <QDirIterator>

#include "base/config.h"

namespace {
    QImage addBorder(const QImage& image, int border_width) {
        QImage result(image.width() + border_width * 2, image.height() + border_width * 2, QImage::Format_RGBA8888);
        result.fill(Qt::transparent);
        for (int x = 0; x < result.width(); ++x) {
            for (int y = 0; y < result.height(); ++y) {
                const int source_x = x - border_width;
                const int source_y = y - border_width;
                if (source_x >= 0 && source_x < image.width() && source_y >= 0 && source_y < image.height() &&
                    image.pixelColor(source_x, source_y).alpha() != 0) {
                    result.setPixelColor(x, y, image.pixelColor(source_x, source_y));
                    continue;
                }
                for (int dx = -border_width; dx <= border_width; ++dx) {
                    for (int dy = -border_width; dy <= border_width; ++dy) {
                        const int neighbor_x = source_x + dx;
                        const int neighbor_y = source_y + dy;
                        if (neighbor_x >= 0 && neighbor_x < image.width() && neighbor_y >= 0 && neighbor_y < image.height() &&
                            image.pixelColor(neighbor_x, neighbor_y).alpha() != 0) {
                            result.setPixelColor(x, y, QColor(setting::current().actorBorderColor));
                            dx = border_width;
                            break;
                        }
                    }
                }
            }
        }
        return result;
    }
}  // namespace

void ResourceManager::initialize() {
    if (fallback_image_) return;

    QDirIterator it(QStringLiteral(":/bedrockmap/icons/entity"), QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QImage image(it.next());
        const QString key = it.fileName().replace(QStringLiteral(".png"), QString());
        actor_images_.insert_or_assign(key, std::make_unique<QImage>(addBorder(image, setting::current().actorBorderWidth)));
    }
    fallback_image_ = std::make_unique<QImage>(QStringLiteral(":/bedrockmap/icons/what.png"));
}

const QImage* ResourceManager::actorImage(const QString& key) const {
    const auto it = actor_images_.find(key);
    return it == actor_images_.end() ? fallback_image_.get() : it->second.get();
}

std::vector<const QImage*> ResourceManager::markerIcons() const {
    std::vector<const QImage*> result;
    result.reserve(static_cast<std::size_t>(actor_images_.size()) + 1);
    for (const auto& [key, image] : actor_images_) {
        Q_UNUSED(key);
        result.push_back(image.get());
    }
    if (fallback_image_) result.push_back(fallback_image_.get());
    return result;
}
