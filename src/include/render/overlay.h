#ifndef BEDROCKMAP_OVERLAY_H
#define BEDROCKMAP_OVERLAY_H

#include <QColor>

class QImage;

/// One entity/marker icon at a world position. Coordinates are in blocks, as
/// stored by ChunkRegion; the overlay converts them with the camera transform.
struct ActorSprite {
    const QImage* icon{nullptr};
    float block_x{0.0f};
    float block_z{0.0f};
    float scale{1.0f};
};

/// World-space rectangle used by the QML renderer's village and HSA overlays.
/// Coordinates are blocks and include the native renderer's edge conventions.
struct OverlayRect {
    float min_x{0.0f};
    float min_z{0.0f};
    float max_x{0.0f};
    float max_z{0.0f};
    int dimension{0};
    QColor outline;
    QColor fill;
};

#endif  // BEDROCKMAP_OVERLAY_H
