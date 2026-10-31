#ifndef BEDROCKMAP_RESOURCEMANAGER_H
#define BEDROCKMAP_RESOURCEMANAGER_H

#include <QImage>
#include <QString>
#include <map>
#include <memory>
#include <vector>

/// Owns the application's shared image resources.
class ResourceManager final {
   public:
    ResourceManager() = default;
    ~ResourceManager() = default;
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    void initialize();

    [[nodiscard]] const QImage* actorImage(const QString& key) const;

    /// Entity images and the unknown fallback, packed by the sprite overlay.
    [[nodiscard]] std::vector<const QImage*> markerIcons() const;

   private:
    std::map<QString, std::unique_ptr<QImage>> actor_images_;
    std::unique_ptr<QImage> fallback_image_;
};

#endif  // BEDROCKMAP_RESOURCEMANAGER_H
