#ifndef BEDROCKMAP_OVERLAYQUADLAYER_H
#define BEDROCKMAP_OVERLAYQUADLAYER_H

#include <QColor>
#include <QRectF>
#include <QVector>

class QSGGeometryNode;
class QSGNode;

struct OverlayQuad {
    /// Rectangle in overlay item pixels.
    QRectF rect;
    QColor fill;
    QColor outline;
    qreal outlineWidth{0.0};
};

/// Owns one reusable colored-quad scene-graph batch. The layer is render-thread
/// state; callers publish immutable snapshots from the GUI thread separately.
class OverlayQuadLayer final {
   public:
    void update(QSGNode* root, const QVector<OverlayQuad>& quads, QSGNode* before = nullptr, bool prepend = false);
    void resetSceneGraph();
    [[nodiscard]] QSGNode* node() const;

   private:
    QSGGeometryNode* node_{nullptr};
    int vertex_capacity_{0};
};

#endif  // BEDROCKMAP_OVERLAYQUADLAYER_H
