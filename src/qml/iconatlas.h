#ifndef BEDROCKMAP_ICONATLAS_H
#define BEDROCKMAP_ICONATLAS_H

#include <QHash>
#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <vector>

struct IconAtlasEntry {
    QRectF uv;
    QSizeF sourceSize;
};

/// Shared texture page for small map markers. Entries are keyed by the source
/// QImage pointer already carried by the actor data, so future marker types can
/// join the same atlas without changing the drawing node.
class IconAtlas {
   public:
    void build(const std::vector<const QImage*>& images);
    [[nodiscard]] const QImage& image() const { return image_; }
    [[nodiscard]] const IconAtlasEntry* entry(const QImage* image) const;
    [[nodiscard]] bool isEmpty() const { return image_.isNull() || entries_.isEmpty(); }

   private:
    QImage image_;
    QHash<const QImage*, IconAtlasEntry> entries_;
};

#endif  // BEDROCKMAP_ICONATLAS_H
